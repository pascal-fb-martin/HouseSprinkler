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
 * This module handles watering indexes from external sources. It accumulates
 * indexes from all detected sources, ready for consideration.
 *
 * Which index will be considered depends on the priority of each source:
 * the available source with the highest priority is selected.
 *
 * A default priority is provided by each source. The user can overwrite
 * this default by assigning an explicit priority to each source (user
 * preference). Note that the user may set a lower priority than the
 * default (source downgrade)..
 *
 * Thus this module considers two conceptual lists: a list of user preferences,
 * which might include sources that are not currently available, and a list of
 * available sources, which might include sources that have no user preference.
 *
 * For practical reasons, the module combines these two lists into one, with
 * a flag to distinguish if the priority is user defined or a default.
 *
 * If the configuration changes, the module maintains the live values for
 * those entries that are currently in the user preferences (even if these
 * were not in the user preferences before), and forget all other entries.
 * The "other" entries will be detected again, eventually.
 *
 * The same index source might be reported by multiple servers: as long as
 * the name of the source is the same, there will be a single entry here.
 * The duplication is simply a way to add redundancy.
 *
 * void housesprinkler_index_refresh (void);
 *
 *    This function must be called each time the configuration changes.
 *
 * int housesprinkler_index_preferred (void);
 *
 *    Return the index of the preferred index, or -1 if no index is valid.
 *
 * const char *housesprinkler_index_origin (int index);
 * int         housesprinkler_index_priority (int index);
 * int         housesprinkler_index_value (int index);
 *
 *    Return the origin, priority or value of the specified index (typically
 *    the index referenced by housesprinkler_index_preferred()). If no valid
 *    index is available return origin "default", priority 0 and value 100.
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
#include <echttp_libc.h>
#include <echttp_hash.h>
#include <echttp_json.h>

#include "houselog.h"
#include "houseconfig.h"
#include "housediscover.h"

#include "housesprinkler.h"
#include "housesprinkler_index.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define EXPIRATION   (3 * 86400)
#define DEFAULTINDEX   100

struct IndexSelection {
    char *name;
    unsigned int signature; // Faster search.
    int priority;
    int user;     // 1 if set by the user, 0 if default.

    // The following items are not saved
    char *origin;
    int value;
    time_t timestamp;
};

static struct IndexSelection *IndexPreferences = 0;
static int                    IndexPreferencesCount = 0;
static int                    IndexPreferencesSize = 0;

static int IndexWasDiscovered = 0;

int housesprinkler_index_preferred (void) {

    int preferred = -1; // No preferred source, yet.
    time_t expiration = time(0) - EXPIRATION;

    int i;
    for (i = 0; i < IndexPreferencesCount; ++i) {
        struct IndexSelection *preference = IndexPreferences + i;
        if (preference->timestamp < expiration) {
            preference->timestamp = 0; // Mark it as dead.
            continue;
        }

        if ((preferred < 0) ||
            (preference->priority > IndexPreferences[preferred].priority))
             preferred = i;
    }
    return preferred;
}

static int housesprinkler_index_search (const char *name,
                                        unsigned int signature) {

    int i;
    for (i = IndexPreferencesCount-1; i >= 0; --i) {
        struct IndexSelection *preference = IndexPreferences + i;
        if (preference->signature != signature) continue; // Faster
        if (!strsame (preference->name, name)) continue; // Slower
        return i;
    }
    return -1;
}

void housesprinkler_index_refresh (void) {

    struct IndexSelection *old = IndexPreferences;
    int oldcount = IndexPreferencesCount;
    int i;

    IndexPreferences = 0;
    IndexPreferencesCount = 0;

    int content = houseconfig_array (0, ".indexes");
    if (content <= 0) {
        // Compatibility with previous generation of HouseSprinkler configs.
        DEBUG ("No index choices, using default priorities\n");
    }

    int count = (content > 0)?houseconfig_array_length (content):0;
    if (count < 0) count = 0;
    DEBUG ("Loading %d index preferences\n", count);

    IndexPreferencesSize = count + 8; // More may be discovered later.
    IndexPreferences = calloc (IndexPreferencesSize,
                               sizeof(struct IndexSelection));

    if (count > 0) {
        int *list = calloc (count, sizeof(int));
        houseconfig_enumerate (content, list, count);
        for (i = 0; i < count; ++i) {
            int item = houseconfig_object (list[i], 0);
            if (item <= 0) continue;
            struct IndexSelection *preference =
                         IndexPreferences + IndexPreferencesCount;
            const char *name = houseconfig_string (item, ".name");
            if (!name) continue; // Invalid, skip.
            preference->name = strdup(name);
            preference->signature = echttp_hash_signature (preference->name);
            preference->priority = houseconfig_positive (item, ".priority");
            preference->user = 1;

            preference->origin = 0;
            preference->value = DEFAULTINDEX;
            preference->timestamp = 0;

            IndexPreferencesCount += 1;
        }
        free (list);
    }
    housesprinkler_index_periodic (0); // Force refresh
    if (!old) return;

    // Recover the live data from the old configuration, and then discard it.

    for (i = 0; i < oldcount; ++i) {
        int new = housesprinkler_index_search (old[i].name, old[i].signature);
        if (new >= 0) {
            struct IndexSelection *preference = IndexPreferences + new;
            preference->origin = old[i].origin;
            old[i].origin = 0; // Don't free it: it is being reused.
            preference->value = old[i].value;
            preference->timestamp = old[i].timestamp;
        }

        // Not needed anymore.
        if (old[i].name) free (old[i].name);
        if (old[i].origin) free (old[i].origin);
    }
    free (old);
}

static int housesprinkler_index_set (const char *name,
                                     const char *origin,
                                     int priority,
                                     int value, time_t timestamp) {

    unsigned int signature = echttp_hash_signature (name);

    int i = housesprinkler_index_search (name, signature);
    if (i >= 0) {
        struct IndexSelection *preference = IndexPreferences + i;
        if (!preference->user) preference->priority = priority;

        // Update the live portion.
        if (!preference->origin) preference->origin = strdup (origin);
        if (preference->timestamp < timestamp) {
            preference->value = value;
            preference->timestamp = timestamp;

            return 1; // Found and updated.
        }
    } else {
        // This is a new index
        if (IndexPreferencesCount >= IndexPreferencesSize) {
           IndexPreferencesSize += 8;
           IndexPreferences =
               realloc (IndexPreferences,
                        IndexPreferencesSize * sizeof(struct IndexSelection));
        }
        struct IndexSelection *preference =
                     IndexPreferences + IndexPreferencesCount++;
        preference->name = strdup (name);
        preference->signature = signature;
        preference->priority = priority; // Use the default
        preference->user = 0;

        // Update the live portion.
        preference->origin = strdup (origin);
        preference->value = value;
        preference->timestamp = timestamp;

        return 1; // New.
    }
    return 0; // Not a worthy update.
}

static struct IndexSelection *housesprinkler_index_access (int index) {
    if ((index > 0) && (index < IndexPreferencesCount))
        return IndexPreferences + index;
    return 0;
}

int housesprinkler_index_priority (int index) {
    struct IndexSelection *preference = housesprinkler_index_access (index);
    return preference?preference->priority:0;
}

const char *housesprinkler_index_origin (int index) {
    struct IndexSelection *preference = housesprinkler_index_access (index);
    return preference?preference->origin:0;
}

int housesprinkler_index_value (int index) {
    struct IndexSelection *preference = housesprinkler_index_access (index);
    return preference?preference->value:DEFAULTINDEX;
}

static void housesprinkler_index_response
               (void *origin, int status, char *data, int length) {

   const char *service = (const char *) origin;
   ParserToken tokens[100];
   int  count = 100;

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
   int stateref = echttp_json_search (tokens, ".waterindex.status.state");
   if (stateref < 0) return; // No hope.
   const char *state = tokens[stateref].value.string;
   if (!state || (state[0] != 'a')) return; // This index service is not ready.

   int received = echttp_json_search (tokens, ".waterindex.status.received");
   int priorityref = echttp_json_search (tokens, ".waterindex.status.priority");
   if (received <= 0 || priorityref <= 0) {
       houselog_trace (HOUSE_FAILURE, service, "No timestamp or priority");
       return;
   }
   time_t timestamp = (time_t) tokens[received].value.integer;
   int    priority = (int)(tokens[priorityref].value.integer);

   int    indexref = echttp_json_search (tokens, ".waterindex.status.index");
   int    nameref = echttp_json_search (tokens, ".waterindex.status.name");
   int    sourceref = echttp_json_search (tokens, ".waterindex.status.origin");
   if (indexref <= 0 || nameref <= 0 || sourceref <= 0) {
       houselog_trace (HOUSE_FAILURE, service, "No index or origin");
       return;
   }
   const char *name = tokens[nameref].value.string;
   int value = (int)(tokens[indexref].value.integer);
   char *url = tokens[sourceref].value.string;

   DEBUG ("Received index %s = %d at priority %d from %s (service %s)\n",
          name, value, priority, url, service);

   if (housesprinkler_index_set (name, url, priority, value, timestamp)) {
       houselog_event ("INDEX", name, "APPLY",
                       "%d%% FROM %s (PRIORITY %d)", value, url, priority);
       IndexWasDiscovered = 1;
   }
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

    static time_t LastDiscovery = 0;

    if (!now) { // This is a manual reset (force refresh request).
        LastDiscovery = 0;
        IndexWasDiscovered = 0;
        return;
    }
    if (IndexWasDiscovered) {
        // We just want an update: go slower.
        if (now < LastDiscovery + 3600) return;
    } else {
        // We do not know any index yet: try to get an index fast.
        if (now < LastDiscovery + 5) return;
    }
    LastDiscovery = now;

    housediscovered ("waterindex", 0, housesprinkler_index_query);
}

int housesprinkler_index_status (char *buffer, int size) {

    int preferred = housesprinkler_index_preferred ();
    int cursor = 0;
    if (preferred >= 0) {
        struct IndexSelection *preference = IndexPreferences + preferred;
        cursor = snprintf (buffer, size, "\"name\":\"%s\",\"value\":%d,",
                           preference->name, preference->value);
    }
    cursor += snprintf (buffer+cursor, size-cursor, "\"preferences\":[");
    int empty = cursor;

    const char *separator = "";
    int i;
    for (i = 0; i < IndexPreferencesCount; ++i) {
        struct IndexSelection *preference = IndexPreferences + i;
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"name\":\"%s\",\"origin\":\"%s\""
                                ",\"user\":%s,\"priority\":%d,\"active\":%s"
                                ",\"value\":%d,\"timestamp\":%lld}",
                     separator,
                     preference->name,
                     preference->origin?preference->origin:"",
                     preference->user?"true":"false",
                     preference->priority,
                     (i == preferred)?"true":"false",
                     preference->value,
                     (long long)(preference->timestamp));
        if (cursor >= size) return 0;
        separator = ",";
    }
    if (cursor <= empty) return 0;

    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) return 0;
    return cursor;
}

