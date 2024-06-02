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
 * housesprinkler_feed.c - Control the sprinkler system feeds.
 *
 * SYNOPSYS:
 *
 * A feed is meant to turn on and off various devices that are not
 * directly sprinkler valves, but are needed for the watering:
 *    water pumps,
 *    24 volt power supply for the solenoids,
 *    etc.
 *
 * void housesprinkler_feed_refresh (void);
 *
 *    This function must be called each time the configuration changes.
 *
 * void housesprinkler_feed_activate (const char *name,
 *                                    int pulse, const char *context);
 *
 *    Turn the feed on for the specified time. This activates the specified
 *    feed, and all feeds chained to this one.
 *
 *    The context string indicates what caused the feed to be turned on. If
 *    null, the default cause is "manual". The cause is typically a program.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "housesprinkler.h"
#include "housesprinkler_time.h"
#include "housesprinkler_feed.h"
#include "housesprinkler_config.h"
#include "housesprinkler_control.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define MAX_PROVIDER 64

static char *Providers[MAX_PROVIDER];
static int   ProvidersCount = 0;

typedef struct {
    const char *name;
    const char *next;
    char manual;
    char linger;
} SprinklerFeed;

static SprinklerFeed *Feed = 0;
static int            FeedCount = 0;

static SprinklerFeed *housesprinkler_feed_search (const char *name) {
    int i;
    for (i = 0; i < FeedCount; ++i) {
        if (!strcmp(name, Feed[i].name)) return Feed+i;
    }
    return 0;
}

void housesprinkler_feed_refresh (void) {

    int i;
    int count;
    int content;
    char path[128];
    int list[256];

    // Reload all feed items.
    //
    if (Feed) free (Feed);
    Feed = 0;
    FeedCount = 0;
    content = housesprinkler_config_array (0, ".feeds");
    if (content > 0) {
        FeedCount = housesprinkler_config_array_length (content);
        if (FeedCount > 0) {
            Feed = calloc (FeedCount, sizeof(SprinklerFeed));
            DEBUG ("Loading %d feed items\n", FeedCount);
        }
    }

    for (i = 0; i < FeedCount; ++i) {
        snprintf (path, sizeof(path), "[%d]", i);
        int item = housesprinkler_config_object (content, path);
        if (item > 0) {
            Feed[i].name = housesprinkler_config_string (item, ".name");
            Feed[i].next = housesprinkler_config_string (item, ".next");
            Feed[i].linger = housesprinkler_config_integer (item, ".linger");
            Feed[i].manual = housesprinkler_config_boolean (item, ".manual");
        }
        housesprinkler_control_declare (Feed[i].name, "FEED");
        housesprinkler_control_event (Feed[i].name, 0, 0);
        DEBUG ("\tFeed %s (manual=%s)\n",
               Feed[i].name, Feed[i].manual?"true":"false");
    }

    // Detect loops in chains. Having any is bad.
    for (i = 0; i < FeedCount; ++i) {
        int loop = 0;
        const char *name = Feed[i].next;
        const char *previous = Feed[i].name;
        while (name && name[0]) {
            SprinklerFeed *feed = housesprinkler_feed_search (name);
            if (!feed) {
                houselog_event
                    ("FEED", previous, "INVALID", "UNKNOWN NEXT %s", name);
                break;
            }
            previous = name;
            name = feed->next;
            if (++loop >= FeedCount) {
                houselog_event
                    ("FEED", Feed[i].name, "INVALID", "INFINITE LOOP IN CHAIN");
                break;
            }
        }
    }
}

void housesprinkler_feed_activate (const char *name,
                                   int pulse, const char *context) {

    const char *previous = 0;
    int loop = 0;

    while (name && name[0]) {
        SprinklerFeed *feed = housesprinkler_feed_search (name);
        if (!feed) {
            if (previous)
                houselog_event
                    ("FEED", previous, "INVALID", "UNKNOWN NEXT %s", name);
            else
                houselog_event ("FEED", name, "UNKNOWN", "");
            return;
        }
        if (!feed->manual) {
            // No context means manually operated, i.e. a zone test.
            // In this case we generate an event (once) to help with
            // testing. Otherwise feed events just add noise.
            //
            if ((!context) || (context[0] == 0))
                housesprinkler_control_event (name, 1, 1);
            housesprinkler_control_start (name, pulse + feed->linger, context);
        }
        previous = name;
        name = feed->next;

        if (++loop >= FeedCount) break; // We went through all feeds.
    }
}

