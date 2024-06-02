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
 * void housesprinkler_program_set_index
 *          (const char *origin, int value, time_t timestamp);
 *
 *    Set the current watering index for all upcoming programs.
 *
 * void housesprinkler_program_start_manual (const char *name);
 * void housesprinkler_program_start_scheduled (const char *name);
 *
 *    The two methods for running a watering program: manual or automatic.
 *
 * int housesprinkler_program_running (const char *name);
 *
 *    Indicate is the named program is currently running.
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
#include "housesprinkler_config.h"
#include "housesprinkler_zone.h"
#include "housesprinkler_season.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define SPRINKLER_MAX_ZONES 256

typedef struct {
    const char *name;
    const char *season;
    char running;
    short count;
    struct {
        const char *name;
        int runtime;
    } zones[SPRINKLER_MAX_ZONES];
} SprinklerProgram;

static SprinklerProgram *Programs = 0;
static int ProgramsCount = 0;

static int WateringIndexEnabled = 1;

static int         WateringIndex = 100;
static const char *WateringIndexOrigin = 0;
static int         WateringIndexTimestamp = 0;


void housesprinkler_program_refresh (void) {

    int i, j;
    short count;
    int content;
    char path[128];

    // Reload all watering programs.
    //
    if (Programs) free (Programs);
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
        snprintf (path, sizeof(path), "[%d]", i);
        int program = housesprinkler_config_object (content, path);
        if (program > 0) {
            Programs[i].name = housesprinkler_config_string (program, ".name");
            if (!Programs[i].name) continue;

            Programs[i].season = housesprinkler_config_string (program, ".season");
            int zones = housesprinkler_config_array (program, ".zones");
            count = 0;
            if (zones <= 0) {
                Programs[i].count = 0;
                continue;
            }
            count = housesprinkler_config_array_length (zones);
            if (count > SPRINKLER_MAX_ZONES) {
                houselog_event ("PROGRAM", Programs[i].name, "TRUNCATED",
                                "TOO MANY ZONES (%d), USE ONLY THE FIRST %d",
                                count, SPRINKLER_MAX_ZONES);
                count = SPRINKLER_MAX_ZONES;
            }
            for (j = 0; j < count; ++j) {
                snprintf (path, sizeof(path), "[%d]", j);
                int zone = housesprinkler_config_object (zones, path);
                Programs[i].zones[j].name =
                    housesprinkler_config_string (zone, ".name");
                Programs[i].zones[j].runtime =
                    housesprinkler_config_integer (zone, ".time");
            }
            Programs[i].count = count;
            Programs[i].running = 0;
            DEBUG ("\tProgram %s (%d zones)\n", Programs[i].name, count);
        }
    }
    WateringIndexEnabled = housesprinkler_config_backup_get (".useindex");
}

void housesprinkler_program_index (int state) {
    WateringIndexEnabled = state;
    housesprinkler_config_backup_set (".useindex", state);
}

void housesprinkler_program_set_index 
         (const char *origin, int value, time_t timestamp) {

    WateringIndex = value;
    WateringIndexOrigin = origin;
    WateringIndexTimestamp = timestamp;
}

static void housesprinkler_program_activate
                (SprinklerProgram *program, int manual) {

    // The first task during activation is to calculate the watering index
    // that applies at this time.

    int index = 100;
    const char *indexname = 0;
    char context[256];

    if (program-> running) {
        houselog_event ("PROGRAM", program->name, "IGNORED", "ALREADY RUNNING");
        return;
    }
    DEBUG ("Activate program %s\n", program->name);

    // Use an online index only if it has been updated recently.
    // Otherwise use the season static schedule, if any.
    // As an added condition, do not automatically activate if the season
    // index is 0: this means that the program is disabled for that period
    // of the year. Manual activation is fine: the user is always right.
    //
    time_t now = time(0);

    if (WateringIndexEnabled) {

        if (program->season) {
            index = housesprinkler_season_index (program->season);
            if (!index) {
                if (!manual) {
                    houselog_event ("PROGRAM", program->name,
                                    "IGNORED", "NOT IN SEASON");
                    return; // Disabled at this time of the year.
                }
                index = 100; // The user did override the season index.
            }
            indexname = program->season;
        }

        // A recent external index takes priority over the static season.
        if (WateringIndexTimestamp > now - (3 * 86400)) {
            index = WateringIndex;
            indexname = WateringIndexOrigin;
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
}

static int housesprinkler_program_find (const char *name) {
    int i;
    for (i = 0; i < ProgramsCount; ++i) {
        if (strcmp (name, Programs[i].name) == 0) return i;
    }
    return -1;
}

void housesprinkler_program_start_manual (const char *name) {

    int i = housesprinkler_program_find(name);
    if (i >= 0) housesprinkler_program_activate (Programs+i, 1);
}

void housesprinkler_program_start_scheduled (const char *name) {

    int i = housesprinkler_program_find(name);
    if (i >= 0) housesprinkler_program_activate (Programs+i, 0);
}

int housesprinkler_program_running (const char *name) {

    int i = housesprinkler_program_find(name);
    if (i >= 0) return Programs[i].running;
    return 1; // If it does not exist, it should not be activated.
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
    int cursor = 0;
    const char *prefix = "";

    cursor += snprintf (buffer+cursor, size-cursor,
                        "\"useindex\":%s", WateringIndexEnabled?"true":"false");
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
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

