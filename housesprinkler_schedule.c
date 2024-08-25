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
 * housesprinkler_schedule.c - Manage the watering schedules.
 *
 * A watering schedule defines when each program is launched (automatic
 * start). That launch is subject to a few conditions: on/off and rain delay.
 *
 * SYNOPSYS:
 *
 * void housesprinkler_schedule_refresh (void);
 *
 *    This function must be called each time the sprinkler configuration
 *    has been changed. It converts the configured schedules into activable
 *    ones.
 *
 * void housesprinkler_schedule_switch (void);
 *
 *    Alternate the sprinkler system between on and off.
 *
 * void housesprinkler_schedule_rain (int enabled);
 *
 *    Enable or disable the rain delay feature. This is independent of
 *    the current rain delay value, which might be calculated automatically.
 *
 * void housesprinkler_schedule_set_rain (int delay);
 *
 *    Add to the rain delay used to cancel scheduled programs during rain
 *    periods.
 *
 * void housesprinkler_schedule_periodic (time_t now);
 *
 *    This is the heart of the sprinkler function: activate watering
 *    programs automatically, based on the schedule.
 *
 * int housesprinkler_schedule_status (char *buffer, int size);
 *
 *    Report the status of this module as a JSON string.
 */

#include <string.h>
#include <stdlib.h>
#include <uuid/uuid.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_time.h"
#include "housesprinkler_config.h"
#include "housesprinkler_program.h"
#include "housesprinkler_schedule.h"

#define DEBUG if (sprinkler_isdebug()) printf

static int SprinklerOn = 1;

typedef struct {
    uuid_t id;
    const char *program;
    char disabled;
    time_t begin;
    time_t until;
    struct {
        short hour;
        short minute;
    } start;
    char days[7];
    char interval;
    time_t lastlaunch;
} SprinklerSchedule;

static SprinklerSchedule *Schedules = 0;
static int SchedulesCount = 0;

static int         WateringIndexState = 1;
static int         WateringIndex = 100;
static const char *WateringIndexOrigin = 0;
static int         WateringIndexTimestamp = 0;

static int    RainDelayEnabled = 1;
static time_t RainDelay = 0;


static time_t housesprinkler_schedule_time (int index, const char *path) {

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

void housesprinkler_schedule_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];
    const char *programname = ".program";

    housesprinkler_config_backup_register (housesprinkler_schedule_status);

    // Keep the old schedule set on the side, to recover some live data.
    SprinklerSchedule *oldschedules = Schedules;
    int oldschedulescount = SchedulesCount;

    // Recalculate all watering schedules.
    Schedules = 0;
    SchedulesCount = 0;
    content = housesprinkler_config_array (0, ".schedules");
    if (content <= 0) {
        // Compatibility with previous generation of HouseSprinkler configs.
        DEBUG ("No schedules, loading from programs\n");
        content = housesprinkler_config_array (0, ".programs");
        programname = ".name";
    }
    if (content > 0) {
        SchedulesCount = housesprinkler_config_array_length (content);
        if (SchedulesCount > 0) {
            Schedules = calloc (SchedulesCount, sizeof(SprinklerSchedule));
            DEBUG ("Loading %d schedules\n", SchedulesCount);
        }
    }

    for (i = 0; i < SchedulesCount; ++i) {
        snprintf (path, sizeof(path), "[%d]", i);
        int schedule = housesprinkler_config_object (content, path);
        if (schedule <= 0) {
            DEBUG ("\tNo schedule at index %d\n", i);
            continue;
        }
        Schedules[i].program = housesprinkler_config_string (schedule, programname);
        if (!Schedules[i].program) {
            DEBUG ("\tSchedule with no name at index %d\n", i);
            continue;
        }

        // Retrieve the schedule's ID. If none can be recovered, just
        // generate a new ID so that there is always one.
        //
        const char *id = housesprinkler_config_string (schedule, ".id");
        if (id) {
            if (uuid_parse (id, Schedules[i].id)) {
                uuid_generate_random (Schedules[i].id);
            }
        } else {
            uuid_generate_random (Schedules[i].id);
        }

        int days = housesprinkler_config_array (schedule, ".days");
        j = 0;
        if (days > 0) {
            int daylist[8];
            int daycount = housesprinkler_config_enumerate (days, daylist);
            for (j = 0; j < daycount; ++j) {
                Schedules[i].days[j] =
                    housesprinkler_config_boolean (daylist[j], "");
            }
        }
        for (; j < 8; ++j) Schedules[i].days[j] = 0;

        Schedules[i].interval = housesprinkler_config_integer (schedule, ".interval");
        Schedules[i].begin = housesprinkler_schedule_time (schedule, ".begin");
        Schedules[i].until = housesprinkler_schedule_time (schedule, ".until");
        const char *s = housesprinkler_config_string (schedule, ".start");
        if (s) {
            Schedules[i].start.hour = atoi(s);
            const char *m = strchr (s, ':');
            if (m) Schedules[i].start.minute = atoi(m+1);
        } else {
            Schedules[i].start.hour = -1; // Will never start, basically.
        }
        Schedules[i].lastlaunch = 0;
        Schedules[i].disabled =
            housesprinkler_config_boolean (schedule, ".disabled");
        DEBUG ("\tSchedule program %s at %02d:%02d\n",
               Schedules[i].program, Schedules[i].start.hour, Schedules[i].start.minute);
    }

    SprinklerOn = housesprinkler_config_backup_get (".on");
    RainDelay = (time_t)housesprinkler_config_backup_get (".raindelay");
    if (RainDelay < time(0)) RainDelay = 0; // Expired.

    // Last step: recover the last launch time of already existing schedules.
    //
    if (oldschedules) {
        for (i = 0; i < oldschedulescount; ++i) {
            if (oldschedules[i].disabled) continue; // Nothing to recover.
            for (j = 0; j < SchedulesCount; ++j) {
                if (uuid_compare (oldschedules[i].id, Schedules[j].id)) continue;

                Schedules[j].lastlaunch = oldschedules[i].lastlaunch;

                DEBUG ("Schedule %d (%s at %02d:%02d) recovers data from %d (%s at %02d:%02d)\n", j, Schedules[j].program, Schedules[j].start.hour, Schedules[j].start.minute, i, oldschedules[i].program, oldschedules[i].start.hour, oldschedules[i].start.minute);
                break;
            }
        }
        free (oldschedules);
        return;
    }

    // Program start (this is the first time the configuration is loaded):
    // recover the last launch times from the backup.
    //
    i = 0;
    for (;;) {
        uuid_t uuid;
        snprintf (path, sizeof(path), ".schedule[%d].id", i);
        const char *id = housesprinkler_config_backup_get_string (path);
        if (!id) break;
        uuid_parse (id, uuid);
        for (j = 0; j < SchedulesCount; ++j) {
            if (uuid_compare (uuid, Schedules[j].id)) continue;
            snprintf (path, sizeof(path), ".schedule[%d].launched", i);
            Schedules[j].lastlaunch = housesprinkler_config_backup_get (path);
            DEBUG ("Schedule %d (%s at %02d:%02d) recovers data from backup: lastlaunch = %ld\n", j, Schedules[j].program, Schedules[j].start.hour, Schedules[j].start.minute, (long)(Schedules[j].lastlaunch));
            break;
        }
        i += 1;
    }
}

void housesprinkler_schedule_switch (void) {
    time_t now = time(0);
    SprinklerOn = !SprinklerOn;
    houselog_event ("PROGRAM", "SWITCH", SprinklerOn?"ON":"OFF", "");
    housesprinkler_config_backup_changed();
}

void housesprinkler_schedule_rain (int enabled) {
    if (RainDelayEnabled == enabled) return; // No change.
    RainDelayEnabled = enabled;
    if (!enabled) {
        if (RainDelay > time(0)) {
           RainDelay = 0;
           housesprinkler_config_backup_changed();
        }
    }
    houselog_event ("SYSTEM", "RAIN DELAY", enabled?"ENABLED":"DISABLED", "");
}

void housesprinkler_schedule_set_rain (int delay) {

    if (!RainDelayEnabled) return;

    time_t now = time(0);

    if (delay == 0) {

        RainDelay = 0; // Cancel.
        houselog_event ("SYSTEM", "RAIN DELAY", "OFF", "");

    } else if (RainDelay < now) {

        // This is a new rain delay period.
        RainDelay = now + delay;
        houselog_event ("SYSTEM", "RAIN DELAY", "ON",
                        housesprinkler_time_delta_printable (now, RainDelay));

    } else {
        // This is an extension to the ongoing rain delay period.
        RainDelay += delay;
        houselog_event ("SYSTEM", "RAIN DELAY", "EXTENDED",
                        housesprinkler_time_delta_printable (now, RainDelay));
    }
    housesprinkler_config_backup_changed();
}

void housesprinkler_schedule_periodic (time_t now) {

    static char lasthour = -1;
    static char lastminute = -1;

    if (!SprinklerOn) return;

    struct tm local = *localtime(&now);
    if (local.tm_hour == lasthour && local.tm_min == lastminute) return;
    lasthour = local.tm_hour;
    lastminute = local.tm_min;

    if ((RainDelay > 0) && (RainDelay < now)) {
        RainDelay = 0; // No need to save: what was saved is an expired value anyway.
        houselog_event ("SYSTEM", "RAIN DELAY", "EXPIRED", "");
    }
    if (RainDelay > 0) return;

    DEBUG ("== Checking schedule at %02d:%02d\n", local.tm_hour, local.tm_min);

    int i;

    for (i = 0; i < SchedulesCount; ++i) {

        SprinklerSchedule *schedule = Schedules + i;

        DEBUG ("== Checking schedule for program %s\n", schedule->program);

        if (schedule->disabled) continue;
        DEBUG ("== program %s is enabled\n", schedule->program);

        if (housesprinkler_program_running(schedule->program)) continue;

        DEBUG ("== Program %s is not running\n", schedule->program);

        // Start only at the time specified.
        //
        DEBUG ("== Program %s: must start at %02d:%02d\n", schedule->program, schedule->start.hour, schedule->start.minute);
        if (local.tm_hour != schedule->start.hour
            || local.tm_min != schedule->start.minute) continue;

        DEBUG ("== Program %s: time of day matches\n", schedule->program);

        // Start only if the schedule entry is active at that time.
        //
        if (schedule->begin > now) continue;
        if (schedule->until > 0 && schedule->until < now) continue;

        DEBUG ("== Program %s: schedule is active\n", schedule->program);

        // Start only on the days specified.
        //
        if (!schedule->days[local.tm_wday]) continue;

        DEBUG ("== Program %s: schedule is for this day\n", schedule->program);

        // Start only after the specified day interval has passed.
        //
        if (schedule->interval > 1) {
            if (((now - schedule->lastlaunch + 3) / 86400) < schedule->interval) continue;
        }
        DEBUG ("== Program %s: interval  %d has passed\n", schedule->program, schedule->interval);

        DEBUG ("== Program %s activated at %ld\n", schedule->program, (long)now);
        housesprinkler_program_start_scheduled (schedule->program);
        schedule->lastlaunch = now;
        housesprinkler_config_backup_changed();
    }
}

int housesprinkler_schedule_status (char *buffer, int size) {

    int cursor = 0;

    cursor = snprintf (buffer, size,
                       "\"on\":%s", SprinklerOn?"true":"false");
    if (cursor >= size) goto overflow;

    if (RainDelayEnabled) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"raindelay\":%d", RainDelay);
        if (cursor >= size) goto overflow;
    }

    int i;
    char ascii[40];
    const char *sep = ",\"schedule\":[";
    for (i = 0; i < SchedulesCount; ++i) {
        if (!Schedules[i].lastlaunch) continue;
        uuid_unparse (Schedules[i].id, ascii);
        cursor += snprintf (buffer+cursor, size-cursor, "%s{\"id\":\"%s\",\"launched\":%ld}",
                            sep, ascii, (long)(Schedules[i].lastlaunch));
        if (cursor >= size) return cursor;
        sep = ",";
    }
    if (sep[1] == 0)
        cursor += snprintf (buffer+cursor,size-cursor,"]");
    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}

