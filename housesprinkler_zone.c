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
 * void housesprinkler_zone_activate (const char *name,
 *                                    int pulse, const char *context);
 *
 *    Activate one zone for the duration set by pulse. If the zone is already
 *    present in the watering queue, this pulse's amount is added to the
 *    remaining runtime. The context is typically the name of the schedule,
 *    or 0 for manual activation.
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
#include "housediscover.h"

#include "housesprinkler.h"
#include "housesprinkler_time.h"
#include "housesprinkler_zone.h"
#include "housesprinkler_feed.h"
#include "housesprinkler_config.h"
#include "housesprinkler_control.h"

#define DEBUG if (sprinkler_isdebug()) printf

typedef struct {
    const char *name;
    const char *feed;
    int hydrate;
    int pulse;
    int pause;
    char manual;
    char status;
} SprinklerZone;

static SprinklerZone *Zones = 0;
static int            ZonesCount = 0;

static time_t ZonesBusy = 0; // Do not schedule while a zone is running.
static time_t PulseEnd = 0;
static SprinklerZone *ZoneActive = 0; // One zone active at a time.

typedef struct {
    int zone;
    int hydrate;
    int runtime;
    time_t nexton;
    char context[32];
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

    // Reload all zones.
    //
    if (Zones) free (Zones);
    Zones = 0;
    ZoneActive = 0;
    ZonesBusy = 0;
    PulseEnd = 0;
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
            Zones[i].feed = housesprinkler_config_string (zone, ".feed");
            Zones[i].hydrate = housesprinkler_config_integer (zone, ".hydrate");
            Zones[i].pulse = housesprinkler_config_integer (zone, ".pulse");
            Zones[i].pause = housesprinkler_config_integer (zone, ".pause");
            Zones[i].manual = housesprinkler_config_boolean (zone, ".manual");
            Zones[i].status = 'i';
            housesprinkler_control_declare (Zones[i].name, "ZONE");
            DEBUG ("\tZone %s (hydrate=%d, pulse=%d, pause=%d, manual=%s)\n",
                   Zones[i].name, Zones[i].hydrate, Zones[i].pulse, Zones[i].pause,
                   Zones[i].manual?"true":"false");
        }
    }

    // We support at max 1 activation per zone..
    // (If the same zone is activated more than once, the runtimes simply
    // accumulate.)
    //
    if (Queue) free (Queue);
    Queue = 0;
    QueueNext = 0;
    if (ZonesCount)
         Queue = calloc (ZonesCount, sizeof(SprinklerQueue));
}

static int housesprinkler_zone_search (const char *name) {
    int i;
    for (i = 0; i < ZonesCount; ++i) {
        if (!strcmp (name, Zones[i].name)) return i;
    }
    return -1;
}

void housesprinkler_zone_activate (const char *name,
                                   int pulse, const char *context) {

    int zone = housesprinkler_zone_search (name);
    if (zone >= 0) {
        int i;
        time_t now = sprinkler_schedulingtime(time(0));
        if (Zones[zone].manual && context) {
            houselog_event ("ZONE", Zones[zone].name, "IGNORE", "MANUAL MODE ONLY");
            return;
        }
        houselog_trace (HOUSE_INFO, name,
                        "queued (%s) for a %d seconds pulse",
                        context?"scheduled":"manually", pulse);
        for (i = 0; i < QueueNext; ++i) {
            if (Queue[i].zone == zone) {
                // This zone was already queued. Add this pulse
                // to the total remaining runtime.
                Queue[i].runtime += pulse;
                if (Queue[i].nexton == 0) Queue[i].nexton = now;
                return;
            }
        }
        if (QueueNext < ZonesCount) {
            // This zone was not queued yet: create a new entry.
            Queue[QueueNext].zone = zone;
            Queue[QueueNext].hydrate = Zones[zone].hydrate;
            Queue[QueueNext].runtime = pulse;
            if (context)
                snprintf (Queue[QueueNext].context, sizeof(Queue[0].context),
                          "%s", context);
            else
                Queue[QueueNext].context[0] = 0;
            Queue[QueueNext].nexton = now;
            DEBUG ("Activated zone %s for %d seconds (%s, queue entry %d)\n",
                   name, pulse, context?context:"manual", QueueNext);
            QueueNext += 1;
        }
    }
}

void housesprinkler_zone_stop (void) {

    int i;
    time_t now = time(0);

    DEBUG ("%ld: Stop all zones\n", now);
    houselog_event ("ZONE", "ALL", "STOP", "MANUAL");
    for (i = 0; i < QueueNext; ++i) {
        Queue[i].hydrate = 0;
        Queue[i].runtime = 0;
        Queue[i].nexton = 0;
    }
    QueueNext = 0;
    ZonesBusy = 0;
    PulseEnd = 0;
}

static int housesprinkler_zone_elapsed (int queued) {
    int zone = Queue[queued].zone;
    int soaks = Queue[queued].runtime / Zones[zone].pulse;
    if (Queue[queued].runtime % Zones[zone].pulse == 0) soaks -= 1;
    return Queue[queued].runtime + (Zones[zone].pause * soaks);
}

static void housesprinkler_zone_schedule (time_t now) {

    int i;

    // Prune the queue once there is no time left and the zone has completed
    // its pulse (including the pause period).
    //
    while (QueueNext > 0 &&
           Queue[QueueNext-1].runtime == 0 &&
           Queue[QueueNext-1].nexton < now) {

        Queue[QueueNext-1].nexton = 0;
        QueueNext -= 1;
        DEBUG ("%ld: Prune queue entry %d\n", now, QueueNext);
    }

    if (now <= ZonesBusy) return;

    if (ZoneActive) {
        if (ZonesBusy == 0) {
            // Clear sign that a stop was requested: cancel the zone.
            housesprinkler_control_cancel (ZoneActive->name);
        }
        if (ZoneActive->status == 'a') ZoneActive->status = 'i';
        ZoneActive = 0;
        PulseEnd = 0;
    }

    // Search for the next zone to be started.
    // Because nexttime is initialized to current time, only zones that
    // have exhausted their pulse and pause period are considered here.
    // So this loop searches for a zone that meet two conditions: ready
    // to start, and the "oldest" to be so. This is done to maximize
    // the soak time, beyond the minimum as configured.
    // If there are multiple zones of the same "age", then the one with
    // the longest elapsed runtime is selected: this is done to prioritize
    // the longest running zones, especially when the program starts,
    // because these long running zones are on the critical path and
    // define when the program will end.
    //
    int    remaining = 0;
    int    nextzone = -1;
    time_t nexttime = now + 1;
    for (i = 0; i < QueueNext; ++i) {
        if (Queue[i].runtime == 0) continue;
        if (Queue[i].context[0]) {
            // Activate a zone that is part of a program only at the start of
            // the minute.
            // The reason for doing so it to make it easier to calculate
            // water usage: we can sample water flow sensor on a minute basis.
            // We don't do this synchronization for manual controls.
            // We accept to be late by one second, as this is the time
            // precision used by the periodic mechanism anyway.
            //
            if (now % 60 > 1) continue;
        }
        time_t nexton = Queue[i].nexton;
        if (nexton > 0) {
            if (nexton > nexttime) continue;
            int elapsed = housesprinkler_zone_elapsed(i);
            DEBUG ("queue %s has elapse time %d\n", Zones[Queue[i].zone].name, elapsed);
            if (nexton < nexttime) {
                nextzone = i;
                nexttime = nexton;
                remaining = elapsed;
            } else if (elapsed > remaining) {
                nextzone = i;
                remaining = elapsed;
            }
        }
    }

    if (nextzone >= 0) {
        int zone = Queue[nextzone].zone;
        int pulse = 0;
        if (Queue[nextzone].context[0] == 0) {
            // This is a manual zone control: just use the runtime as provided
            // by the user without any adjustment or cycle.
            //
            pulse = Queue[nextzone].runtime;
            Queue[nextzone].runtime = 0;
            Queue[nextzone].hydrate = 0;
            Queue[nextzone].nexton = now + pulse;
        } else {
            // This zone control is part of a program: apply adjustments
            // and follow the configured cycle.
            //
            pulse = Zones[zone].pulse;
            if (Queue[nextzone].hydrate > 0) {
                // The first pulse is meant to hydrate the soil (clay).
                pulse = Queue[nextzone].hydrate;
                Queue[nextzone].hydrate = 0; // Don't do it again.
            }
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
        }
        if (Zones[zone].feed) {
            housesprinkler_feed_activate
                (Zones[zone].feed, pulse, Queue[nextzone].context);
        }
        if (housesprinkler_control_start
                (Zones[zone].name, pulse, Queue[nextzone].context)) {
            // Schedule the next zone after the pulse and the optional index
            // valve pause have been exhausted.
            ZonesBusy = now + pulse + ZoneIndexValvePause;
            ZoneActive = Zones + zone;
            PulseEnd = now + pulse;
            ZoneActive->status = 'a';
        }
    }
}

void housesprinkler_zone_periodic (time_t now) {

    if (!ZonesCount) return;
    if (now) housesprinkler_zone_schedule  (now);
}

int housesprinkler_zone_idle (void) {

    if (!QueueNext) return 1; // Nothing in the queue: most common case.

    // There is something in the queue, but this might be a leftover
    // pause. The system is active only if one zone is active, or if
    // there are other zones to be activated later. If it is only
    // waiting for the pause periods to complete, it is already idle.
    // This avoids declaring a program as "complete" only 30 minutes
    // or so after the last watering.
    //
    int i;
    time_t now = sprinkler_schedulingtime(time(0));

    if (PulseEnd >= now) return 0; // One zone is active.
    for (i = 0; i < QueueNext; ++i) {
        if (Queue[i].runtime > 0) return 0; // One zone will be active.
    }
    return 1;
}

int housesprinkler_zone_status (char *buffer, int size) {

    int i;
    int cursor;
    const char *prefix = "";

    cursor = snprintf (buffer+cursor, size-cursor, "\"zones\":[");
    if (cursor >= size) goto overflow;
    prefix = "";

    for (i = 0; i < ZonesCount; ++i) {
        int state = housesprinkler_control_state (Zones[i].name);
        if ((state != 'e') && (state != 'u')) state = Zones[i].status;
        cursor += snprintf (buffer+cursor, size-cursor, "%s[\"%s\",\"%c\"]",
                            prefix, Zones[i].name, state);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }

    cursor += snprintf (buffer+cursor, size-cursor, "],\"queue\":[");
    if (cursor >= size) goto overflow;
    prefix = "";

    for (i = 0; i < QueueNext; ++i) {
        if (Queue[i].runtime > 0) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s[\"%s\",%d]",
                                prefix,
                                Zones[Queue[i].zone].name,
                                Queue[i].runtime);
            if (cursor >= size) goto overflow;
            prefix = ",";
        }
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    if (ZoneActive) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"active\":\"%s\"", ZoneActive->name);
        if (cursor >= size) goto overflow;
    }

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "STATUS",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    buffer[0] = 0;
    return 0;
}

