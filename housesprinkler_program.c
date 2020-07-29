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
 * void housesprinkler_program_rain (int enabled);
 *
 *    Enable or disable the rain delay feature. This is independent of
 *    the current rain delay value, which might be calculated automatically.
 *
 * void housesprinkler_program_set_rain (int delay);
 *
 *    Add to the rain delay used to cancel programs during rain periods.
 *
 * void housesprinkler_program_set_index
 *          (const char *origin, int value, time_t timestamp);
 *
 *    Set the current watering index for all upcoming programs.
 *
 * void housesprinkler_program_manual (const char *name);
 *
 * void housesprinkler_program_periodic (time_t now);
 *
 *    This is the heart of the sprinkler function: activate and execute
 *    watering schedules.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "housesprinkler.h"
#include "housesprinkler_config.h"
#include "housesprinkler_zone.h"

#define DEBUG if (echttp_isdebug()) printf

typedef struct {
    const char *name;
    int unit;
    int index[52];
} SprinklerSeason;

#define SPRINKLER_SEASON_INVALID  0
#define SPRINKLER_SEASON_WEEKLY   1 // Weekly index (52 weeks).
#define SPRINKLER_SEASON_MONTHLY  2 // Monthly index (12 months).

static SprinklerSeason *Seasons = 0;
static int SeasonsCount = 0;

typedef struct {
    const char *name;
    char enabled;
    time_t begin;
    time_t until;
    struct {
        short hour;
        short minute;
    } start;
    char repeat;
    char days[7];
    char interval;
    int season;
    int count;
    struct {
        const char *name;
        int runtime;
    } zones[256];
    int running;
    time_t lastlaunch;
} SprinklerProgram;

#define SPRINKLER_REPEAT_ONCE      0
#define SPRINKLER_REPEAT_DAILY     1
#define SPRINKLER_REPEAT_WEEKLY    2

static SprinklerProgram *Programs = 0;
static int ProgramsCount = 0;

static int         ProgramIndex = 100;
static const char *ProgramIndexOrigin = 0;
static int         ProgramIndexTimestamp = 0;

static int    ProgramRainEnabled = 1;
static time_t ProgramRainDelay = 0;

static int housesprinkler_program_season_find (const char *name) {
    int i;
    for (i = 0; i < SeasonsCount; ++i) {
        if (strcmp (Seasons[i].name, name) == 0) return i;
    }
    return -1;
}

static time_t housesprinkler_program_time (int index, const char *path) {

    struct tm local = {0};
    const char *p;

    const char *date = housesprinkler_config_string (index, path);
    if (date) {
        local.tm_mon = atoi(date) - 1;
        p = strchr (date, '/');
        if (p) {
            local.tm_mday = atoi (p+1);
            p = strchr (p+1, '/');
            if (p) {
                local.tm_year = atoi(p+1);
                if (local.tm_year < 100) {
                    local.tm_year += 100; // Post 2000.
                } else if (local.tm_year >= 2000) {
                    local.tm_year -= 1900; // 4 digits year.
                }
                local.tm_isdst = -1;
                return mktime (&local);
            }
        }
    }
    return 0;
}

void housesprinkler_program_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];

    // Recalculate all seasons.
    //
    if (Seasons) free (Seasons);
    SeasonsCount = 0;
    content = housesprinkler_config_array (0, ".seasons");
    if (content > 0) {
        SeasonsCount = housesprinkler_config_array_length (content);
        if (SeasonsCount > 0) {
            Seasons = calloc (SeasonsCount, sizeof(SprinklerSeason));
            DEBUG ("Loading %d seasons\n", SeasonsCount);
        }
    }

    for (i = 0; i < SeasonsCount; ++i) {
        snprintf (path, sizeof(path), "[%d]", i);
        int season = housesprinkler_config_object (content, path);
        if (season > 0) {
            Seasons[i].unit = SPRINKLER_SEASON_INVALID; // Safe default.
            Seasons[i].name = housesprinkler_config_string (season, ".name");
            if (!Seasons[i].name) continue; // Bad entry.

            count = 0;
            int index = housesprinkler_config_array (season, ".weekly");
            if (index <= 0) {
                index = housesprinkler_config_array (season, ".monthly");
                if (index <= 0) continue; // Bad entry.
                Seasons[i].unit = SPRINKLER_SEASON_MONTHLY;
                count = 12;
            } else {
                Seasons[i].unit = SPRINKLER_SEASON_WEEKLY;
                count = 52;
            }
            if (count != housesprinkler_config_array_length(index)) {
                Seasons[i].unit = SPRINKLER_SEASON_INVALID;
                continue; // Bad entry.
            }
            for (j = 0; j < count; ++j) {
                snprintf (path, sizeof(path), "[%d]", j);
                Seasons[i].index[j] =
                    housesprinkler_config_integer(index, path);
            }
            DEBUG ("\tSeason %s (%sly)\n",
                   Seasons[i].name,
                   (Seasons[i].unit==SPRINKLER_SEASON_WEEKLY)?"week":"month");
        }
    }

    // Recalculate all watering programs.
    if (Programs) free (Programs);
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

            const char *s = housesprinkler_config_string (program, ".season");
            if (s) {
                Programs[i].season = housesprinkler_program_season_find(s);
            }

            s = housesprinkler_config_string (program, ".repeat");
            Programs[i].repeat = SPRINKLER_REPEAT_ONCE;
            if (s) {
                if (strcmp(s, "weekly") == 0) {

                    Programs[i].repeat = SPRINKLER_REPEAT_WEEKLY;
                    int days = housesprinkler_config_array (program, ".days");
                    j = 0;
                    if (days > 0) {
                        int daylist[8];
                        int daycount = housesprinkler_config_enumerate (days, daylist);
                        for (j = 0; j < daycount; ++j) {
                            Programs[i].days[j] =
                                housesprinkler_config_boolean (daylist[j], "");
                        }
                    }
                    for (; j < 8; ++j) Programs[i].days[j] = 0;

                } else if (strcmp(s, "daily") == 0) {

                    Programs[i].repeat = SPRINKLER_REPEAT_DAILY;
                    Programs[i].interval = housesprinkler_config_integer (program, ".interval");
                }
            }
            count = 0;
            int zones = housesprinkler_config_array (program, ".zones");
            if (zones <= 0) {
                Programs[i].count = 0;
                continue;
            }
            count = housesprinkler_config_array_length (zones);
            for (j = 0; j < count; ++j) {
                snprintf (path, sizeof(path), "[%d]", j);
                int zone = housesprinkler_config_object (zones, path);
                Programs[i].zones[j].name =
                    housesprinkler_config_string (zone, ".name");
                Programs[i].zones[j].runtime =
                    housesprinkler_config_integer (zone, ".time");
            }
            Programs[i].begin = housesprinkler_program_time (program, ".begin");
            Programs[i].until = housesprinkler_program_time (program, ".until");
            s = housesprinkler_config_string (program, ".start");
            if (s) {
                Programs[i].start.hour = atoi(s);
                const char *m = strchr (s, ':');
                if (m) Programs[i].start.minute = atoi(m+1);
            }
            Programs[i].count = count;
            Programs[i].running = 0;
            Programs[i].lastlaunch = 0;
            Programs[i].enabled = 1;
            DEBUG ("\tProgram %s at %02d:%02d (%d zones)\n",
                   Programs[i].name, Programs[i].start.hour, Programs[i].start.minute, count);
        }
    }
}

void housesprinkler_program_set_index 
         (const char *origin, int value, time_t timestamp) {

    ProgramIndex = value;
    ProgramIndexOrigin = origin;
    ProgramIndexTimestamp = timestamp;
}

void housesprinkler_program_rain (int enabled) {
    ProgramRainEnabled = enabled;
}

void housesprinkler_program_set_rain (int delay) {

    time_t now = time(0);

    if (ProgramRainDelay < now) {
        // This is a new rain delay period.
        ProgramRainDelay = now + delay;
    } else {
        // This is an extension to the ongoing rain delay period.
        ProgramRainDelay += delay;
    }
}

static void housesprinkler_program_activate
                (SprinklerProgram *program, int manual) {

    // The first task during activation is to calculate the watering index
    // that applies at this time.

    int index = 100;

    DEBUG ("Activate %s\n", program->name);

    // Use an online index only if it has been updated recently.
    // Otherwise use the season static schedule, if any.
    //
    time_t now = time(0);
    if (ProgramIndexTimestamp > now - (3 * 86400)) {

        index = ProgramIndex; // A recent explicit index takes priority.
        DEBUG ("Activate %s using index %d from %s%s\n",
               program->name, index, ProgramIndexOrigin, manual?" (manual)":"");

    } else if (program->season >= 0) {

        // Week of the year. We do not care about getting the exact
        // result, just having something matching the period of the year.
        //
        struct tm local = *localtime(&now);
        int week = (local.tm_yday - local.tm_wday + 4) / 7;
        if (week < 0) week = 51;
        else if (week >= 52) week -= 52;

        switch (Seasons[program->season].unit) {
            case SPRINKLER_SEASON_WEEKLY:
                index = Seasons[program->season].index[week];
                break;
            case SPRINKLER_SEASON_MONTHLY:
                index = Seasons[program->season].index[local.tm_mon];
                break;
            default:
                index = 0;
        }
        if (index == 0) {
            DEBUG ("Season %s index is 0\n", Seasons[program->season].name);
            if (!manual) return;
            index = 100;
        }
        DEBUG ("Activate %s using index %d from %s%s\n",
               program->name, index, Seasons[program->season].name, manual?" (manual)":"");

    } else {

        DEBUG ("Activate %s%s\n", program->name, manual?" (manual)":"");
    }

    int i;
    for (i = 0; i < program->count; ++i) {
        int runtime = (program->zones[i].runtime * index) / 100;
        housesprinkler_zone_activate (program->zones[i].name, runtime, 0);
    }
}

void housesprinkler_program_manual (const char *name) {

    int i;
    for (i = 0; i < ProgramsCount; ++i) {
        if (strcmp (name, Programs[i].name) == 0) {
            housesprinkler_program_activate (Programs+i, 1);
            return;
        }
    }
}

void housesprinkler_program_periodic (time_t now) {

    static char lasthour = -1;
    static char lastminute = -1;

    int i;
    struct tm local = *localtime(&now);

    if (local.tm_hour == lasthour && local.tm_min == lastminute) return;
    lasthour = local.tm_hour;
    lastminute = local.tm_min;

    if (ProgramRainEnabled && ProgramRainDelay > now) return;

    DEBUG ("Checking schedule at %02d:%02d\n", local.tm_hour, local.tm_min);

    for (i = 0; i < ProgramsCount; ++i) {

        SprinklerProgram *program = Programs + i;

        if (!program->running && program->enabled)  {

            DEBUG ("Checking schedule for program %s\n", program->name);

            // Start only at the time specified.
            //
            if (local.tm_hour != program->start.hour
                || local.tm_min != program->start.minute) continue;

            DEBUG ("Program %s: time of day matches\n", program->name);

            // Start only if the program is active.
            //
            if (program->begin > now) continue;
            if (program->until > 0 && program->until < now) continue;

            DEBUG ("Program %s: active\n", program->name);

            // Start only on the days specified, or at the daily interval
            // specified.
            //
            int delta;
            int match = 0;
            switch (program->repeat) {
                case SPRINKLER_REPEAT_WEEKLY:
                    match = program->days[local.tm_wday];
                    break;
                case SPRINKLER_REPEAT_DAILY:
                    match = (((now - program->lastlaunch) / 86400) >= program->interval);
                    break;
                case SPRINKLER_REPEAT_ONCE:
                    delta = program->begin - now;
                    match = (program->lastlaunch == 0 && delta > 0 && delta < 60);
                    break;
                default:
                    match = 0;
            }
            if (!match) {
                DEBUG ("This program not active this day\n");
                continue;
            }

            housesprinkler_program_activate (program, 0);
            program->running = 1;
            program->lastlaunch = now;
        }
    }

    if (housesprinkler_zone_idle()) {
        for (i = 0; i < ProgramsCount; ++i) {
            Programs[i].running = 0;
        }
    }
}

