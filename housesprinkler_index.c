/* housesprinkler - A simple home web server for sprinkler control
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * housesprinkler_index.c - Access watering index services.
 *
 * SYNOPSYS:
 *
 * This module handles watering index from external sources.
 *
 * void housesprinkler_index_refresh (void);
 *
 *    This function must be called each time the configuration changes.
 *
 * const char *housesprinkler_index_origin (void);
 * time_t      housesprinkler_index_timestamp (void);
 *
 *    Return the origin or timestamp corresponding to the current index value.
 *
 * int housesprinkler_index_get (void);
 *
 *    Get the current index value.
 *
 * void housesprinkler_index_register
 *          (housesprinkler_index_listener *listener);
 *
 *    Register a new listener. A listener is called when a new index value
 *    is made available.
 *
 * void housesprinkler_index_periodic (time_t now);
 *
 *    The periodic function that schedule index requests.
 *
 * int housesprinkler_index_status (char *buffer, int size);
 *
 *    Populate the buffer with a JSON object that represents the state
 *    of the watering index.
 *
 * CONFIGURATION
 *
 * This module is driven by the list of index providers provided in the
 * JSON configuration. Each provider is defined by:
 * - a provider name.
 * - an enabled flag (a way to keep the configuration's data without
 *   activating it).
 * - an URL to get the index data.
 * - An indication if the data is XML or JSON format.
 * - A path to the index value, in JavaScript syntax.
 * - the minimum and maximum adjustment bounds for the index.
 * - A list of times of day when to query the server.
 *
 * The module does not query a server more often than every six hours,
 * regardless of the schedule provided. A watering index does not change
 * every hour anyway.
 *
 * The module query the enabled servers from that list, in the order provided,
 * until one has answered with a valid index value. For the purpose of that
 * poll logic, an index value becomes stale after six hours.
 *
 * The most reasonable use of multiple servers is to use one as the primary
 * source (listed first) and then schedule a second one at the same time, or
 * an hour later, as a backup.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>
#include <echttp_xml.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_index.h"
#include "housesprinkler_config.h"

#define DEBUG if (echttp_isdebug()) printf

typedef struct {
    int  enabled;
    const char *name;
    const char *url;
    int  format;
    const char *path;
    int  adjust[2];
    int  refresh[24];
    time_t lastquery;
} SprinklerIndexServer;

#define INDEX_FORMAT_UNKNOWN 0
#define INDEX_FORMAT_JSON    1
#define INDEX_FORMAT_XML     2

static SprinklerIndexServer *Servers = 0;
static int                   ServersCount = 0;

static int    SprinklerIndex = 100;
static time_t SprinklerIndexTimestamp = 0;
static const char *SprinklerIndexOrigin = "";

#define INDEX_MAX_LISTENER  16
static housesprinkler_index_listener *SprinklerIndexListener[INDEX_MAX_LISTENER];

void housesprinkler_index_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];
    int list[256];

    // Reload all servers.
    //
    if (Servers) free (Servers);
    ServersCount = 0;
    content = housesprinkler_config_array (0, ".wateringindex");
    if (content > 0) {
        ServersCount = housesprinkler_config_enumerate (content, list);
    }

    Servers = calloc (ServersCount, sizeof(SprinklerIndexServer));
    DEBUG ("Loading %d index servers\n", ServersCount);

    for (i = 0; i < ServersCount; ++i) {
        int child = list[i];
        SprinklerIndexServer *server = Servers + i;

        server->enabled = housesprinkler_config_boolean (child, ".enable");
        server->name = housesprinkler_config_string (child, ".provider");
        server->url = housesprinkler_config_string (child, ".url");
        const char *format = housesprinkler_config_string (child, ".format");
        server->path = housesprinkler_config_string (child, ".path");

        if (!server->name || !server->url || !server->path || !format) {
            server->enabled = 0;
            DEBUG ("\tIndex server at %d disabled (incomplete record)\n", i);
            continue;
        }

        server->lastquery = 0;
        server->format = INDEX_FORMAT_JSON; // Default.
        if (format) {
            if (strcmp (format, "XML") == 0) server->format = INDEX_FORMAT_XML;
            if (strcmp (format, "JSON") == 0) server->format = INDEX_FORMAT_JSON;
            else {
                DEBUG ("\tIndex server at %d disabled (invalid format %s)\n",
                       i, format);
            }
        }

        server->adjust[0] = housesprinkler_config_integer (child, ".adjust.min");
        server->adjust[1] = housesprinkler_config_integer (child, ".adjust.max");
        // If no limits provided, use reasonable numbers.
        if (server->adjust[0] <= 0) server->adjust[0] = 0;
        if (server->adjust[1] <= 0) server->adjust[1] = 150;

        int refresh = housesprinkler_config_array (child, ".refresh");
        if (refresh < 0) {
            server->refresh[0] = 0; // Midnight.
            for (j = 1; j < 24; ++j) server->refresh[j] = -1;
            continue;
        }
        int times[24];
        int timescount = housesprinkler_config_enumerate (refresh, times);
        if (timescount > 24) {
            houselog_trace (HOUSE_FAILURE, server->name,
                            "More than 24 refresh times per day?");
            exit (1); // Array overflow has occurred.
        }
        for (j = 0; j < timescount; ++j) {
            server->refresh[j] = housesprinkler_config_integer (times[j], "");
            if (server->refresh[j] < 0 || server->refresh[j] >= 24) {
                DEBUG ("\tIndex server %s invalid time %d\n",
                       server->refresh[j]);
                server->refresh[j] = -1; // Ignore invalid time.
            }
        }
        for (; j < 24; ++j) server->refresh[j] = -1;

        DEBUG ("\tIndex server %s (url=%s, format = %s, min=%d, max=%d)\n",
               server->name, server->url, format, server->adjust[0], server->adjust[1]);
    }
}

const char *housesprinkler_index_origin (void) {
    return SprinklerIndexOrigin;
}

time_t housesprinkler_index_timestamp (void) {
    return SprinklerIndexTimestamp;
}

int housesprinkler_index_get (void) {
    return SprinklerIndex;
}

void housesprinkler_index_register (housesprinkler_index_listener *listener) {

    int i;

    for (i = 0; i < INDEX_MAX_LISTENER; ++i) {
        if (SprinklerIndexListener[i] == 0) {
            SprinklerIndexListener[i] = listener;
            break;
        }
    }
}

static void housesprinkler_index_response
               (void *origin, int status, char *data, int length) {

   SprinklerIndexServer *server = (SprinklerIndexServer *) origin;
   ParserToken tokens[100];
   int  count = 100;
   time_t now = time(0);
   int rawindex;

   if (SprinklerIndexTimestamp > now - 3600) {
       DEBUG ("Ignoring response from %s, received recent index already\n",
              server->url);
       return;
   }

   if (status == 302) {
       static char url[256];
       const char *redirect = echttp_attribute_get ("Location");
       if (!redirect) {
           houselog_trace (HOUSE_FAILURE, server->name,
                           "%s: invalid redirect", url);
           return;
       }
       strncpy (url, redirect, sizeof(url));
       const char *error = echttp_client ("GET", url);
       if (error) {
           houselog_trace (HOUSE_FAILURE, server->name, "%s: %s", url, error);
           return;
       }
       echttp_submit (0, 0, housesprinkler_index_response, (void *)server);
       DEBUG ("Redirected to %s\n", url);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, server->name, "HTTP code %d", status);
       return;
   }

   // Analyze the answer and retrieve the control points matching our zones.
   const char *error;
   switch (server->format) {
       case INDEX_FORMAT_JSON:
           error = echttp_json_parse (data, tokens, &count); break;
       case INDEX_FORMAT_XML:
           error = echttp_xml_parse (data, tokens, &count); break;
       default: return;
   }
   if (error) {
       houselog_trace (HOUSE_FAILURE, server->name,
                       "%s: syntax error, %s", server->url, error);
       return;
   }
   if (count <= 0) return;

   int index = echttp_json_search (tokens, server->path);
   if (index <= 0) return;

   if (tokens[index].type == PARSER_INTEGER) {
       rawindex = tokens[index].value.integer;
   } else if (tokens[index].type == PARSER_STRING) {
       // This happens with XML..
       rawindex = atoi(tokens[index].value.string);
   } else {
       return; // Invalid.
   }
   if (SprinklerIndex < server->adjust[0])
       SprinklerIndex = server->adjust[0];
   else if (SprinklerIndex > server->adjust[1])
       SprinklerIndex = server->adjust[1];
   else
       SprinklerIndex = rawindex;

   SprinklerIndexTimestamp = now;
   SprinklerIndexOrigin = server->name;

   DEBUG ("Received index %d from %s\n", SprinklerIndex, server->name);
   if (SprinklerIndex == rawindex)
       houselog_event (now, "INDEX", server->name, "RECEIVED",
                       "%d%% from %s", rawindex, server->url);
   else
       houselog_event (now, "INDEX", server->name, "RECEIVED",
                       "%d%% (adjusted to %d%%) from %s",
                       rawindex, SprinklerIndex, server->url);

   int i;
   for (i = 0; i < INDEX_MAX_LISTENER; ++i) {
       if (SprinklerIndexListener[i])
           SprinklerIndexListener[i]
               (SprinklerIndexOrigin, SprinklerIndex, SprinklerIndexTimestamp);
   }
}

static void housesprinkler_index_query (SprinklerIndexServer *server) {

    if (server->url[0] == 0) return;

    DEBUG ("Requesting index from %s at %s\n", server->name, server->url);
    const char *error = echttp_client ("GET", server->url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, server->name, "%s", error);
        return;
    }

    echttp_submit (0, 0, housesprinkler_index_response, (void *)server);
}

void housesprinkler_index_periodic (time_t now) {

    static time_t LastCall = 0;

    int i, h;
    struct tm local = *localtime(&now);

    if (LastCall > now - 60) return; // Limit to one attempt per minute.
    LastCall = now;

    // Do not query any more server if we obtained an answer recently.
    if (SprinklerIndexTimestamp > now - (6 * 3600)) return;

    // Query the first server we did not ask yet.
    for (i = 0; i < ServersCount; ++i) {
        if (Servers[i].enabled) {
            if (Servers[i].lastquery >= now - (6 * 3600)) continue;
            for (h = 0 ; h < 24; ++h) {
                if (local.tm_hour == Servers[i].refresh[h]) {
                    housesprinkler_index_query (Servers+i);
                    Servers[i].lastquery = now;
                    return;
                }
            }
        }
    }
}

int housesprinkler_index_status (char *buffer, int size) {

    return snprintf (buffer, size, "\"origin\":\"%s\",\"value\":%d",
                     SprinklerIndexOrigin, SprinklerIndex);
}

