/* housesprinkler - A simple home web server for sprinkler control
 *
 * Copyright 2023, Pascal Martin
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
 * housesprinkler_program.c - Manage the watering programs.
 *
 * SYNOPSYS:
 *
 * int housesprinkler_program_exists (const char *name);
 *
 *    Return 1 if the named program exists, 0 otherwise. This is used
 *    to validate any part of the HouseSprinkler's configuration that
 *    references a program.
 *
 * void housesprinkler_program_refresh (void);
 *
 *    This function must be called each time the sprinkler configuration
 *    has been changed. It converts the configured programs into activable
 *    ones.
 *
 * void housesprinkler_program_index (int state);
 *
 *    Enable/disable the index mechanism (independently of the index value).
 *
 * int housesprinkler_program_get_index (const char *name);
 *
 *    Get the current watering index value to apply to the specified program.
 *
 * void   housesprinkler_program_start_manual (const char *name);
 * time_t housesprinkler_program_start_scheduled (const char *name, int full);
 *
 *    The two methods for running a watering program: manual or automatic.
 *    The second method indicates if the program was effectively started:
 *    it returns the start time, or 0 if the program was not started.
 *
 * int housesprinkler_program_running (const char *name);
 *
 *    Indicate is the named program is currently running.
 *
 * time_t housesprinkler_program_scheduled (const char *name, time_t scheduled);
 *
 *    Update when a program was last scheduled. The update takes effect only
 *    if the scheduled time provided is more recent than the last time know.
 *    In other words, this can only moves the last scheduled time forward.
 *    This is used to restore the last known scheduled time after a
 *    configuration refresh.
 *
 *    This also returns the last known scheduled time. A way to query the
 *    current state without side effect is to call with scheduled set to 0.
 *
 * void housesprinkler_program_periodic (time_t now);
 *
 *    Periodic background processing. Detects when the running programs
 *    have completed their run.
 *
 * int housesprinkler_program_status (char *buffer, int size);
 *
 *    Report the status of this module as a JSON string.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_state.h"
#include "housesprinkler_config.h"
#include "housesprinkler_zone.h"
#include "housesprinkler_season.h"
#include "housesprinkler_index.h"

#define DEBUG if (sprinkler_isdebug()) printf

typedef struct {
    const char *name;
    int runtime;
} SprinklerProgramZone;

typedef struct {
    const char *name;
    const char *season;
    char running;
    short count;
    time_t scheduled;
    SprinklerProgramZone *zones;
} SprinklerProgram;

static SprinklerProgram *Programs = 0;
static int ProgramsCount = 0;

static int WateringIndexEnabled = 1;

static void housesprinkler_program_restore (void) {
    WateringIndexEnabled = housesprinkler_state_get (".useindex");
}

static int housesprinkler_program_backup (char *buffer, int size) {
    return snprintf (buffer, size,
                     "\"useindex\":%s", WateringIndexEnabled?"true":"false");
}

void housesprinkler_program_refresh (void) {

    int i;
    short count;
    int content;
    char path[128];

    housesprinkler_state_listen (housesprinkler_program_restore);
    housesprinkler_state_register (housesprinkler_program_backup);

    // Reload all watering programs.
    //
    if (Programs) {
        for (i = 0; i < ProgramsCount; ++i) {
            if (Programs[i].zones) free(Programs[i].zones);
        }
        free (Programs);
    } else {
        // Program start: restore the internal state.
        housesprinkler_program_restore();
    }
    Programs = 0;
    ProgramsCount = 0;
    content = housesprinkler_config_array (0, ".programs");
    if (content > 0) {
        ProgramsCount = housesprinkler_config_array_length (content);
        if (ProgramsCount > 0) {
            Programs = calloc (ProgramsCount, sizeof(SprinklerProgram));
            DEBUG ("Loading %d programs\n", ProgramsCount);
        }
    }

    for (i = 0; i < ProgramsCount; ++i) {
        Programs[i].name = 0;
        Programs[i].zones = 0;
        Programs[i].count = 0;
        Programs[i].season = 0;
        Programs[i].running = 0;
        Programs[i].scheduled = 0;

        snprintf (path, sizeof(path), "[%d]", i);
        int program = housesprinkler_config_object (content, path);
        if (program > 0) {
            Programs[i].name = housesprinkler_config_string (program, ".name");
            if (!Programs[i].name) continue;

            Programs[i].season = housesprinkler_config_string (program, ".season");
            int zones = housesprinkler_config_array (program, ".zones");
            if (zones <= 0) continue;

            count = housesprinkler_config_array_length (zones);
            if (count > 0) {
                int j;
                Programs[i].zones = calloc (count, sizeof(SprinklerProgramZone));
                for (j = 0; j < count; ++j) {
                    snprintf (path, sizeof(path), "[%d]", j);
                    int zone = housesprinkler_config_object (zones, path);
                    Programs[i].zones[j].name =
                        housesprinkler_config_string (zone, ".name");
                    Programs[i].zones[j].runtime =
                        housesprinkler_config_positive (zone, ".time");
                }
            }
            Programs[i].count = count;
            DEBUG ("\tProgram %s (%d zones)\n", Programs[i].name, count);
        }
    }
}

void housesprinkler_program_index (int state) {
    if (state != WateringIndexEnabled) {
        WateringIndexEnabled = state;
        housesprinkler_state_changed ();
    }
}

// Search which watering index to use for the specified program at this time.
// First consider the season index. Use an online index only if it has
// a higher priority and it has been updated recently.
// As an added condition, a season index of 0 overrides any online index:
// value 0 means that the program is disabled for that period of the year.
// The exception is manual mode: the user action takes precedence.
//
static int housesprinkler_program_currentindex (SprinklerProgram *program,
                                                int manual,
                                                const char **origin) {

    if (!WateringIndexEnabled) return 100;

    int priority = 0;
    int index = 100;

    *origin = 0;

    if (program->season) {
        index = housesprinkler_season_index (program->season);
        if ((index <= 0) && (!manual)) return 0; // Disabled at this time.

        *origin = program->season;
        priority = housesprinkler_season_priority (program->season);
    }

    // Uses the external index only if valid and of a higher priority.
    if (housesprinkler_index_priority () > priority) {
        index = housesprinkler_index_get ();
        *origin = housesprinkler_index_origin ();
    }
    if ((index <= 0) && manual) {
        *origin = 0;
        return 100; // The user action takes precedence.
    }
    return index;
}

static time_t housesprinkler_program_activate
                  (SprinklerProgram *program, int manual, int full) {

    // The first task during activation is to calculate the watering index
    // that applies at this time.

    int priority = 0;
    int index = 100;
    const char *indexname = 0;
    char context[256];

    if (program->running) {
        houselog_event ("PROGRAM", program->name, "IGNORED", "ALREADY RUNNING");
        return 0;
    }
    DEBUG ("Activate program %s\n", program->name);

    // First consider the season index. Use an online index only if it has
    // a higher priority and it has been updated recently.
    // Do not activate the program is the index is 0, as this means that
    // the program is disabled for that period of the year.
    // Manual activation is the exception: the user is always right.
    //
    time_t now = time(0);

    if (!full) {
        index = housesprinkler_program_currentindex (program, manual, &indexname);
        if (!index) {
            if (manual) {
                // The user has requested this program to be run now,
                // so we can ignore the disabling feature of index value 0.
                index = 100;
            } else {
                houselog_event ("PROGRAM", program->name,
                                "IGNORED", "NOT IN SEASON");
                return 0; // Disabled at this time of the year.
            }
        }
    }

    // Now that we know which index to apply, let's launch this program.
    //
    if (indexname) {
        houselog_event ("PROGRAM", program->name, "START",
                        "%s, INDEX %d%% FROM %s",
                        manual ? "USER ACTIVATED" : "SCHEDULED", index, indexname);
    } else {
        houselog_event ("PROGRAM", program->name, "START",
                        "%s, NO INDEX", manual ? "USER ACTIVATED" : "SCHEDULED");
    }

    snprintf (context, sizeof(context), "PROGRAM %s", program->name);
    int i;
    for (i = 0; i < program->count; ++i) {
        int runtime = (program->zones[i].runtime * index) / 100;
        housesprinkler_zone_activate
            (program->zones[i].name, runtime, context);
    }

    program->running = 1;
    return now;
}

static int housesprinkler_program_find (const char *name) {
    if (!name) return -1;
    int i;
    for (i = 0; i < ProgramsCount; ++i) {
        if (strcmp (name, Programs[i].name) == 0) return i;
    }
    return -1;
}

int housesprinkler_program_exists (const char *name) {
    return housesprinkler_program_find (name) >= 0;
}

int housesprinkler_program_get_index (const char *name) {

    const char *origin; // Dummy, not used.
    int i = housesprinkler_program_find (name);
    if (i < 0) return 0;
    return housesprinkler_program_currentindex (Programs+i, 0, &origin);
}

void housesprinkler_program_start_manual (const char *name) {

    int i = housesprinkler_program_find(name);
    if (i >= 0) housesprinkler_program_activate (Programs+i, 1, 0);
}

time_t housesprinkler_program_start_scheduled (const char *name, int full) {

    int i = housesprinkler_program_find(name);
    if (i < 0) return 0; // Cannot start an unknow program.

    time_t started = housesprinkler_program_activate (Programs+i, 0, full);
    if (started) Programs[i].scheduled = started;
    return started;
}

int housesprinkler_program_running (const char *name) {

    int i = housesprinkler_program_find(name);
    if (i >= 0) return Programs[i].running;
    return 1; // If it does not exist, it should not be activated.
}

time_t housesprinkler_program_scheduled (const char *name, time_t scheduled) {

    int i = housesprinkler_program_find(name);
    if (i < 0) return 0; // Unknown.
    if (scheduled > Programs[i].scheduled) { // Only move forward.
        Programs[i].scheduled = scheduled;
    }
    return Programs[i].scheduled;
}

void housesprinkler_program_periodic (time_t now) {

    if (housesprinkler_zone_idle()) {
        int i;
        for (i = 0; i < ProgramsCount; ++i) {
            if (Programs[i].running) {
                houselog_event ("PROGRAM", Programs[i].name, "STOP", "");
                Programs[i].running = 0;
            }
        }
    }
}

int housesprinkler_program_status (char *buffer, int size) {

    int i;
    const char *prefix = "";

    int cursor = housesprinkler_program_backup (buffer, size);
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, ",\"active\":[");
    if (cursor >= size) goto overflow;

    for (i = 0; i < ProgramsCount; ++i) {
        if (Programs[i].running) {
            cursor += snprintf (buffer+cursor, size-cursor,
                                "%s\"%s\"", prefix, Programs[i].name);
            prefix = ",";
        }
    }

    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "STATUS",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    buffer[0] = 0;
    return 0;
}

