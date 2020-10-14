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
 * housesprinkler_zone.c - Control the watering zones.
 *
 * SYNOPSYS:
 *
 * This module handles watering zones, including:
 * - Load the servers and zones configuration.
 * - Run periodic discoveries to find which server controls each zone.
 * - Run a queue of zone activation.
 *
 * The queue starts one zone at a time. If a pulse/pause duration was
 * defined, the zone is started only for the duration of the pulse. If
 * there is still some activation time left, the zone is scheduled for
 * re-activation pulse + pause seconds later.
 *
 * The goal of the pulse/pause mechanism is to avoid runoffs: the sprinklers
 * typically deliver water faster than the ground can absorb. After some
 * time poodles start to form. If there is a slope, a runoff may occur and
 * the water is lost. So the goal is to run the sprinklers for a limited
 * amount of time, and stop before a poodle or runoff has occurred.
 * The pause time is meant to let the water be absorbed, after which the
 * same sprinklers can be run again. This repeats until the whole requested
 * watering time has been delivered.
 *
 * Another zone can be started while the previous zone is paused. The
 * queue mechanism selects the first entry with the lowest start time,
 * alternating through all the zones present in the queue, and no time
 * is wasted doing no watering.
 *
 * A zone is removed from the queue once its last pulse has been completed.
 *
 * void housesprinkler_zone_refresh (void);
 *
 *    This function must be called each time the configuration changes.
 *
 * void housesprinkler_zone_activate (const char *name, int pulse, int manual);
 *
 *    Activate one zone for the duration set by pulse. If the zone is already
 *    present in the watering queue, this pulse's amount is added to the
 *    remaining runtime.
 *
 * void housesprinkler_zone_stop (void);
 *
 *    Stop all active zones.
 *
 * void housesprinkler_zone_periodic (time_t now);
 *
 *    The periodic function that runs the zones, one by one.
 *
 * int  housesprinkler_zone_idle (void);
 *
 *    Return true if at least one zone is active, false otherwise.
 *
 * int housesprinkler_zone_status (char *buffer, int size);
 *
 *    A function that populates a complete status in JSON.
 *
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_zone.h"
#include "housesprinkler_config.h"

#define DEBUG if (echttp_isdebug()) printf

#define DEFAULTSERVER "http://localhost/relay"

typedef struct {
    char url[256];
    char *type;
    char status;
} SprinklerControlServer;

static SprinklerControlServer *Servers = 0;
static int                     ServersCount = 0;

typedef struct {
    const char *name;
    int pulse;
    int pause;
    char manual;
    char status;
    SprinklerControlServer *server;
} SprinklerZone;

static SprinklerZone *Zones = 0;
static int            ZonesCount = 0;

typedef struct {
    int zone;
    int runtime;
    time_t nexton;
} SprinklerQueue;

static SprinklerQueue *Queue = 0;
static int             QueueNext = 0;

static int ZoneIndexValvePause = 1; // An optional pause for indexing valves.

void housesprinkler_zone_refresh (void) {

    int i;
    int count;
    int content;
    char path[128];
    int list[256];

    // Reload all servers.
    //
    if (Servers) free (Servers);
    ServersCount = 0;
    content = housesprinkler_config_array (0, ".servers");
    if (content > 0) {
        ServersCount = housesprinkler_config_enumerate (content, list);
    }

    ZoneIndexValvePause = housesprinkler_config_integer (0, "pause");
    if (ZoneIndexValvePause < 1) ZoneIndexValvePause = 1;

    Servers = calloc (ServersCount, sizeof(SprinklerControlServer));
    DEBUG ("Loading %d servers\n", ServersCount);

    for (i = 0; i < ServersCount; ++i) {
        int child = list[i];
        const char *name = housesprinkler_config_string (child, "");
        if (name) {
            strncpy (Servers[i].url, name, sizeof(Servers[i].url));
            char *type = strrchr (Servers[i].url, '/');
            if (type) Servers[i].type = type + 1;
            Servers[i].status = 'u';
            DEBUG ("\tServer %s (type %s)\n", Servers[i].url, Servers[i].type);
        } else {
            houselog_trace (HOUSE_FAILURE, "config",
                            "Invalid server at index %d", i);
            Servers[i].url[0] = 0;
            Servers[i].status = 'f';
        }
    }

    // Reload all zones.
    //
    if (Zones) free (Zones);
    ZonesCount = 0;
    content = housesprinkler_config_array (0, ".zones");
    if (content > 0) {
        ZonesCount = housesprinkler_config_array_length (content);
        if (ZonesCount > 0) {
            Zones = calloc (ZonesCount, sizeof(SprinklerZone));
            DEBUG ("Loading %d zones\n", ZonesCount);
        }
    }

    for (i = 0; i < ZonesCount; ++i) {
        snprintf (path, sizeof(path), "[%d]", i);
        int zone = housesprinkler_config_object (content, path);
        if (zone > 0) {
            Zones[i].name = housesprinkler_config_string (zone, ".name");
            Zones[i].pulse = housesprinkler_config_integer (zone, ".pulse");
            Zones[i].pause = housesprinkler_config_integer (zone, ".pause");
            Zones[i].manual = housesprinkler_config_boolean (zone, ".manual");
            Zones[i].server = 0;
            Zones[i].status = 'u';
        }
        DEBUG ("\tZone %s (pulse=%d, pause=%d, manual=%s)\n",
               Zones[i].name, Zones[i].pulse, Zones[i].pause,
               Zones[i].manual?"true":"false");
    }

    // We support at max 1 activation per zone..
    // (If the same zone is activated more than once, the runtimes simply
    // accumulate.)
    //
    if (Queue) free (Queue);
    QueueNext = 0;
    Queue = calloc (ZonesCount, sizeof(SprinklerQueue));
}

static int housesprinkler_zone_search (const char *name) {
    int i;
    for (i = 0; i < ZonesCount; ++i) {
        if (!strcmp (name, Zones[i].name)) return i;
    }
    return -1;
}

void housesprinkler_zone_activate (const char *name, int pulse, int manual) {

    if (QueueNext < ZonesCount) {
        int zone = housesprinkler_zone_search (name);
        if (zone >= 0) {
            int i;
            if (Zones[zone].manual && !manual) return;
            for (i = 0; i < QueueNext; ++i) {
                if (Queue[i].zone == zone) {
                    // This zone was already queued. Add this pulse
                    // to the total remaining runtime.
                    Queue[i].runtime += pulse;
                    if (Queue[i].nexton == 0) Queue[i].nexton = time(0);
                    return;
                }
            }
            // This zone was not queued yet: create a new entry.
            Queue[QueueNext].zone = zone;
            Queue[QueueNext].runtime = pulse;
            Queue[QueueNext].nexton = time(0);
            DEBUG ("Activated zone %s for %d seconds (%s, queue entry %d)\n",
                   name, pulse, manual?"manual":"auto", QueueNext);
            QueueNext += 1;
        }
    }
}

void housesprinkler_zone_stop (void) {

    int i;
    time_t now = time(0);

    DEBUG ("%ld: Stop all zones\n", now);
    houselog_event (now, "ZONE", "ALL", "STOP", "manual");
    for (i = 0; i < QueueNext; ++i) {
        Queue[i].runtime = 0;
    }
    QueueNext = 0;
}

static void housesprinkler_zone_controlled
               (void *origin, int status, char *data, int length) {

    SprinklerZone *zone = (SprinklerZone *)origin;

   if (status == 302) {
       static char url[256];
       const char *redirect = echttp_attribute_get ("Location");
       if (!redirect) {
           houselog_trace (HOUSE_FAILURE, zone->name, "invalid redirect");
           return;
       }
       strncpy (url, redirect, sizeof(url));
       const char *error = echttp_client ("GET", url);
       if (error) {
           houselog_trace (HOUSE_FAILURE, zone->name, "%s: %s", url, error);
           return;
       }
       DEBUG ("Redirected to %s\n", url);
       echttp_submit (0, 0, housesprinkler_zone_controlled, origin);
       return;
   }

   // TBD: add an event to record that the command was processed. Too verbose?
   if (status != 200) {
       if (zone->status != 'e')
           houselog_trace (HOUSE_FAILURE, zone->name,
                           "HTTP code %d for zone %s", status, zone->name);
       zone->status  = 'e';
   }
   zone->status  = 'a';
}

static int housesprinkler_zone_start (int zone, int pulse) {
    time_t now = time(0);
    DEBUG ("%ld: Start zone %s for %d seconds\n",
           now, Zones[zone].name, pulse);
    houselog_event (now, "ZONE", Zones[zone].name, "START",
                    "for %d seconds", pulse);
    if (Zones[zone].server) {
        static char url[256];
        snprintf (url, sizeof(url), "%s/set?point=%s&state=on&pulse=%d",
                  Zones[zone].server->url, Zones[zone].name, pulse);
        const char *error = echttp_client ("GET", url);
        if (error) {
            houselog_trace (HOUSE_FAILURE, Zones[zone].name, "cannot create socket for %s, %s", url, error);
            return 0;
        }
        DEBUG ("GET %s\n", url);
        echttp_submit (0, 0, housesprinkler_zone_controlled, (void *)Zones+zone);
        return 1;
    }
    return 0;
}

static void housesprinkler_zone_schedule (time_t now) {

    static SprinklerZone *ZoneActive = 0; // One zone active at a time.
    static time_t ZonesBusy = 0; // Do not schedule while a zone is running.

    int i;
    int    nextzone = -1;
    time_t nexttime = now + 1;

    // Prune the queue once there is no time left and the zone has completed
    // its pulse.
    //
    while (QueueNext > 0 &&
           Queue[QueueNext-1].runtime == 0 &&
           Queue[QueueNext-1].nexton < now) {
        QueueNext -= 1;
        DEBUG ("%ld: Prune queue entry %d\n", now, QueueNext);
        Queue[QueueNext].nexton = 0;
    }

    if (now <= ZonesBusy) return;

    if (ZoneActive) {
        if (ZoneActive->status == 'a') ZoneActive->status = 'i';
        ZoneActive = 0;
    }

    // Search for the first zone to be started next.
    for (i = 0; i < QueueNext; ++i) {
        if (Queue[i].runtime == 0) continue;
        time_t nexton = Queue[i].nexton;
        if (nexton > 0 && nexton < nexttime) {
            nextzone = i;
            nexttime = nexton;
        }
    }

    if (nextzone >= 0) {
        int zone = Queue[nextzone].zone;
        int pulse = Zones[zone].pulse;
        if (pulse == 0 || Queue[nextzone].runtime <= pulse) {
            pulse = Queue[nextzone].runtime;
            Queue[nextzone].runtime = 0;
        } else {
            Queue[nextzone].runtime -= pulse;
        }
        // Always waits until the end of the pause, even if this is the
        // last pulse: if the same zone is activated again, we don't want
        // to ever skip the pause.
        //
        Queue[nextzone].nexton = now + pulse + Zones[zone].pause;
        if (housesprinkler_zone_start (zone, pulse)) {
            // Schedule the next zone after the pulse and the optional index
            // valve pause have been exhausted.
            ZonesBusy = now + pulse + ZoneIndexValvePause;
            ZoneActive = Zones + zone;
        }
    }
}

static void housesprinkler_zone_discovered
               (void *origin, int status, char *data, int length) {

   SprinklerControlServer *server = (SprinklerControlServer *) origin;
   ParserToken tokens[100];
   int  innerlist[100];
   char path[256];
   int  count = 100;
   int  i;

   if (status == 302) {
       static char url[256];
       const char *redirect = echttp_attribute_get ("Location");
       if (!redirect) {
           houselog_trace (HOUSE_FAILURE, server->url, "invalid redirect");
           return;
       }
       strncpy (url, redirect, sizeof(url));
       const char *error = echttp_client ("GET", url);
       if (error) {
           houselog_trace (HOUSE_FAILURE, url,
                           "cannot open socket, %s", error);
           return;
       }
       echttp_submit (0, 0, housesprinkler_zone_discovered, (void *)server);
       DEBUG ("Redirected to %s\n", url);
       return;
   }

   if (status != 200) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, server->url, "HTTP error %d", status);
       server->status = 'e';
       return;
   }

   // Analyze the answer and retrieve the control points matching our zones.
   const char *error = echttp_json_parse (data, tokens, &count);
   if (error) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, server->url,
                           "JSON syntax error, %s", error);
       server->status = 'e';
       return;
   }
   if (count <= 0) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, server->url, "no data");
       server->status = 'e';
       return;
   }

   snprintf (path, sizeof(path), ".%s.status", server->type);
   int controls = echttp_json_search (tokens, path);
   if (controls <= 0) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, server->url, "no zone data");
       return;
   }

   int n = tokens[controls].length;
   if (n <= 0) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, server->url, "empty zone data");
       return;
   }

   error = echttp_json_enumerate (tokens+controls, innerlist);
   if (error) {
       if (server->status != 'e')
           houselog_trace (HOUSE_FAILURE, path, "%s", error);
       server->status = 'e';
       return;
   }

   for (i = 0; i < n; ++i) {
       ParserToken *inner = tokens + controls + innerlist[i];
       int zone = housesprinkler_zone_search (inner->key);
       if (zone < 0) continue;
       Zones[zone].server = server;
       Zones[zone].status = 'i';
       DEBUG ("Zone %s discovered on %s, type %s\n",
              Zones[zone].name, server->url, server->type);
   }
   server->status = 'a';
}

static void housesprinkler_zone_discovery (time_t now) {

    static time_t starting = 0;
    static time_t latestdiscovery = 0;

    int scan = 0;

    if (starting == 0) starting = now;

    // Start fast for the first 2 minutes, then slow down.
    // The fast start is to make the whole network recover fast from an outage.
    // Later on, there is no need to create more traffic.
    //
    if (now - latestdiscovery > 15) {
        scan = (now - latestdiscovery > 120 || now - starting < 120);
    }

    if (scan) {
        int i;
        for (i = 0; i < ServersCount; ++i) {
            char url[256];
            if (Servers[i].url[0] == 0) continue;
            snprintf (url, sizeof(url), "%s/status", Servers[i].url);

            DEBUG ("Attempting discovery at %s (server %d)\n", url, i);
            const char *error = echttp_client ("GET", url);
            if (error) {
                if (Servers[i].status != 'f')
                   houselog_trace (HOUSE_FAILURE, Servers[i].url, "%s", error);
                Servers[i].status = 'f';
                continue;
            }

            echttp_submit (0, 0, housesprinkler_zone_discovered, (void *)(Servers+i));
        }
        latestdiscovery = now;
    }
}

void housesprinkler_zone_periodic (time_t now) {

    housesprinkler_zone_discovery (now);
    housesprinkler_zone_schedule  (now);
}

int housesprinkler_zone_idle (void) {
    return QueueNext == 0;
}

int housesprinkler_zone_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    snprintf (buffer, size, "\"servers\":[");
    cursor = strlen(buffer);
    if (cursor >= size) goto overflow;

    for (i = 0; i < ServersCount; ++i) {
        snprintf (buffer+cursor, size-cursor, "%s[\"%s\",\"%c\"]",
                  prefix, Servers[i].url, Servers[i].status);
        prefix = ",";
        cursor += strlen(buffer+cursor);
        if (cursor >= size) goto overflow;
    }

    snprintf (buffer+cursor, size-cursor, "],\"zones\":[");
    cursor += strlen(buffer+cursor);
    prefix = "";

    for (i = 0; i < ZonesCount; ++i) {
        snprintf (buffer+cursor, size-cursor, "%s[\"%s\",\"%c\"]",
                  prefix, Zones[i].name, Zones[i].status);
        prefix = ",";
        cursor += strlen(buffer+cursor);
        if (cursor >= size) goto overflow;
    }

    snprintf (buffer+cursor, size-cursor, "]");
    cursor += strlen(buffer+cursor);
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

