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
 * int         housesprinkler_index_priority (void);
 * time_t      housesprinkler_index_timestamp (void);
 *
 *    Return the origin, priority  or timestamp corresponding to the current
 *    index value. If no valid index is available (or the latest index has
 *    expired), return origin "default", priority and timestamp 0.
 *
 * int housesprinkler_index_get (void);
 *
 *    Get the current index value. Return 100 if no valid index is available.
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
 * This module searches for the services providing "waterindex", and requests
 * an index from each of them. It reports the index with the highest priority
 * that it knows of.
 *
 * The module does not query a service more often than every hour,
 * regardless of the schedule provided. A watering index does not change
 * frequently anyway.
 *
 * The most reasonable use of multiple services is to use one as the primary
 * source (highest priority) and set the others as backups (lower priority).
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "housesprinkler.h"
#include "housesprinkler_index.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define ONEDAY       86400
#define DEFAULTINDEX   100

static int         SprinklerIndex = DEFAULTINDEX;
static int         SprinklerIndexPriority = 0;
static time_t      SprinklerIndexTimestamp = 0;
static char       *SprinklerIndexOrigin = 0;
static int         SprinklerIndexOriginSize = 0;

static int housesprinkler_index_isvalid (void) {

    if (SprinklerIndexTimestamp <= 0) return 0; // No index.

    // Ignore the index if more than 3 days old.
    int isvalid = (SprinklerIndexTimestamp > (time(0) - (3 * 86400)));
    if (!isvalid) SprinklerIndexTimestamp = 0; // Do no repeat the same test.

    return isvalid;
}

void housesprinkler_index_refresh (void) {
    // No static configuration at this time: based on service discovery.
}

const char *housesprinkler_index_origin (void) {
    if (!housesprinkler_index_isvalid()) return "default";
    if (! SprinklerIndexOrigin ||
        SprinklerIndexTimestamp + ONEDAY < time(0)) return "default";
    return SprinklerIndexOrigin;
}

int housesprinkler_index_priority (void) {
    if (!housesprinkler_index_isvalid()) return 0;
    return SprinklerIndexPriority;
}

time_t housesprinkler_index_timestamp (void) {
    if (!housesprinkler_index_isvalid()) return 0;
    return SprinklerIndexTimestamp;
}

int housesprinkler_index_get (void) {
    if (!housesprinkler_index_isvalid()) return DEFAULTINDEX;
    return SprinklerIndex;
}

static void housesprinkler_index_response
               (void *origin, int status, char *data, int length) {

   const char *service = (const char *) origin;
   ParserToken tokens[100];
   int  count = 100;
   time_t now = time(0);

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housesprinkler_index_response, origin);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, service, "HTTP code %d", status);
       return;
   }

   const char *error;
   error = echttp_json_parse (data, tokens, &count);
   if (error) {
       houselog_trace (HOUSE_FAILURE, service, "syntax error, %s", error);
       return;
   }
   if (count <= 0) {
       houselog_trace (HOUSE_FAILURE, service, "no data");
       return;
   }

   int server = echttp_json_search (tokens, ".host");
   if (server < 0) {
       houselog_trace (HOUSE_FAILURE, service, "No host name");
       return;
   }
   int received = echttp_json_search (tokens, ".waterindex.status.received");
   int priority = echttp_json_search (tokens, ".waterindex.status.priority");
   if (received <= 0 || priority <= 0) {
       houselog_trace (HOUSE_FAILURE, service, "No timestamp or priority");
       return;
   }

   int    index = echttp_json_search (tokens, ".waterindex.status.index");
   int    name = echttp_json_search (tokens, ".waterindex.status.name");
   int    source = echttp_json_search (tokens, ".waterindex.status.origin");
   if (index <= 0 || name <= 0 || source <= 0) {
       houselog_trace (HOUSE_FAILURE, service, "No index or origin");
       return;
   }

   time_t timestamp = (time_t) tokens[received].value.integer;
   int    ipriority = (int)(tokens[priority].value.integer);
   int    rawindex  = (int)(tokens[index].value.integer);
   char  *urlsource = tokens[source].value.string;

   DEBUG ("Received index %d at priority %d from %s (service %s)\n",
          rawindex, ipriority, urlsource, service);

   // Ignore any new index if it is of lower priority, or too old.
   //
   if (ipriority < SprinklerIndexPriority ||
       timestamp < SprinklerIndexTimestamp - ONEDAY ) return;

   // Ignore any new index of the same priority if it is older.
   //
   if (ipriority == SprinklerIndexPriority &&
       timestamp <= SprinklerIndexTimestamp) return;

   // This index seems to be a better one than we currently have:
   // store the data.
   //
   SprinklerIndex = rawindex;
   SprinklerIndexPriority = ipriority;
   SprinklerIndexTimestamp = timestamp;

   int size = strlen(tokens[name].value.string) +
              strlen(tokens[server].value.string) + 2; // Size needed.
   if (size > SprinklerIndexOriginSize) {
       if (SprinklerIndexOrigin) free (SprinklerIndexOrigin);
       size += 32; // Avoid repeating the free/malloc sequence too often.
       SprinklerIndexOrigin = (char *) malloc (size);
       SprinklerIndexOriginSize = size;
   }
   snprintf (SprinklerIndexOrigin, SprinklerIndexOriginSize, "%s@%s",
             tokens[name].value.string, tokens[server].value.string);

   // Record this brand new index.
   //
   houselog_event ("INDEX", SprinklerIndexOrigin, "APPLY",
                   "%d%% FROM %s (PRIORITY %d)",
                   SprinklerIndex, urlsource, ipriority);
}

static void housesprinkler_index_query
                (const char *service, void *context, const char *provider) {

    char url[156];

    snprintf (url, sizeof(url), "%s/status", provider);

    DEBUG ("Requesting index from %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, url, "%s", error);
        return;
    }

    echttp_submit (0, 0, housesprinkler_index_response, (void *)service);
}

void housesprinkler_index_periodic (time_t now) {

    static time_t LastInquiry = 0;

    int i, h;
    struct tm local = *localtime(&now);

    if (!now) { // This is a manual reset (force refresh request).
        SprinklerIndexTimestamp = 0;
        LastInquiry = 0;
        return;
    }
    if (!SprinklerIndexTimestamp) {
        // We do not know any index yet: try to get an index fast.
        if (now < LastInquiry + 60) return; // Limit to one attempt per minute.
    } else {
        // We just want an update: go slower.
        if (now < LastInquiry + 3600) return;
    }
    LastInquiry = now;

    // Forget a stale index.
    //
    if (now > SprinklerIndexTimestamp + ONEDAY) {
        SprinklerIndexTimestamp = 0;
        snprintf (SprinklerIndexOrigin, SprinklerIndexOriginSize, "default");
    }

    housediscovered ("waterindex", 0, housesprinkler_index_query);
}

int housesprinkler_index_status (char *buffer, int size) {

    if (!housesprinkler_index_isvalid()) {
        return snprintf (buffer, size, "\"origin\":\"default\",\"value\":100");
    }
    return snprintf (buffer, size, "\"origin\":\"%s\",\"value\":%d",
                     SprinklerIndexOrigin?SprinklerIndexOrigin:"default",
                     SprinklerIndex);
}

