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
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>
#include <echttp_xml.h>

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
        Servers[i].enabled = housesprinkler_config_boolean (child, ".enable");
        Servers[i].name = housesprinkler_config_string (child, ".provider");
        Servers[i].url = housesprinkler_config_string (child, ".url");
        const char *format = housesprinkler_config_string (child, ".format");
        Servers[i].format = INDEX_FORMAT_UNKNOWN;
        if (format) {
            if (strcmp (format, "JSON") == 0)
                Servers[i].format = INDEX_FORMAT_JSON;
            else if (strcmp (format, "XML") == 0)
                Servers[i].format = INDEX_FORMAT_XML;
        }
        Servers[i].path = housesprinkler_config_string (child, ".path");
        Servers[i].adjust[0] = housesprinkler_config_integer (child, ".adjust.min");
        Servers[i].adjust[1] = housesprinkler_config_integer (child, ".adjust.max");
        int times[8];
        int refresh = housesprinkler_config_array (child, ".refresh");
        int timescount = housesprinkler_config_enumerate (refresh, times);
        if (timescount > 24) {
            fprintf (stderr, "More than 24 refresh times per day??\n");
            exit (1);
        }
        for (j = 0; j < timescount; ++j) {
            Servers[i].refresh[j] =
                housesprinkler_config_integer (times[j], "");
        }
        for (; j < timescount; ++j) {
            Servers[i].refresh[j] = -1;
        }
        Servers[i].lastquery = 0;
        if (!Servers[i].name || !Servers[i].url || !Servers[i].path) {
            Servers[i].enabled = 0;
            continue;
        }
        DEBUG ("\tIndex server %s (url=%s, format = %s, min=%d, max=%d)\n",
               Servers[i].name, Servers[i].url, format, Servers[i].adjust[0], Servers[i].adjust[1]);
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

   if (status == 302) {
       static char url[256];
       const char *redirect = echttp_attribute_get ("Location");
       if (!redirect) {
           fprintf (stderr, "%s: invalid redirect\n", server->url);
           return;
       }
       strncpy (url, redirect, sizeof(url));
       const char *error = echttp_client ("GET", url);
       if (error) {
           fprintf (stderr, "%s: %s\n", url, error);
           return;
       }
       echttp_submit (0, 0, housesprinkler_index_response, (void *)server);
       DEBUG ("Redirected to %s\n", url);
       return;
   }

   if (status != 200) return;

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
       fprintf (stderr, "%s: syntax error, %s\n", server->url, error);
       return;
   }
   if (count <= 0) return;

   int index = echttp_json_search (tokens, server->path);
   if (index <= 0) return;

   if (tokens[index].type == PARSER_INTEGER) {
       SprinklerIndex = tokens[index].value.integer;
   } else if (tokens[index].type == PARSER_STRING) {
       // This happens with XML..
       SprinklerIndex = atoi(tokens[index].value.string);
   } else {
       return; // Invalid.
   }
   if (SprinklerIndex < server->adjust[0])
       SprinklerIndex = server->adjust[0];
   else if (SprinklerIndex > server->adjust[1])
       SprinklerIndex = server->adjust[1];
   SprinklerIndexTimestamp = time(0);
   SprinklerIndexOrigin = server->name;

   DEBUG ("Received index %d from %s\n", SprinklerIndex, server->name);

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
        fprintf (stderr, "%s: %s\n", server->url, error);
        return;
    }

    echttp_submit (0, 0, housesprinkler_index_response, (void *)server);
}

void housesprinkler_index_periodic (time_t now) {

    int i, h;
    struct tm local = *localtime(&now);

    for (i = 0; i < ServersCount; ++i) {
        if (Servers[i].enabled) {
            if (Servers[i].lastquery >= now - 3600) continue;
            for (h = 0 ; h < 24; ++h)
                if (local.tm_hour == Servers[i].refresh[h]) break;
            if (h < 24) {
                housesprinkler_index_query (Servers+i);
                Servers[i].lastquery = now;
            }
        }
    }
}

