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
 * void housesprinkler_schedule_initialize (int argc, const char **argv);
 *
 *    Initialize the scheduler. Must be called only once.
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
 * void housesprinkler_schedule_once (const char *program, time_t start);
 *
 *    Add a new one-time schedule entry based on the specified program
 *    and time specified.
 *
 * void housesprinkler_schedule_again (const char *uid);
 *
 *    Add a new one-time schedule entry based on the specified schedule.
 *    The new one-time entry will get its program name from the regular
 *    schedule entry. The one-time entry will start at the next time of
 *    day specified for the normal schedule entry, either this same day
 *    or the next day if this is too late.
 *
 * void housesprinkler_schedule_cancel (const char *program);
 *
 *    Cancel the one-time schedule for the specified program.
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
#include "housesprinkler_state.h"
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

typedef struct {
    char *program;
    time_t start;
} SprinklerOneTimeSchedule;

static SprinklerOneTimeSchedule *OneTimeSchedules = 0;
static int OneTimeSchedulesCount = 0;

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

static int housesprinkler_schedule_new_onetime (void) {

    int i;
    for (i = 0; i < OneTimeSchedulesCount; ++i) {
        if (!OneTimeSchedules[i].start) {
            if (OneTimeSchedules[i].program) {
                free (OneTimeSchedules[i].program);
                OneTimeSchedules[i].program = 0;
            }
            DEBUG ("Reusing one-time schedule entry %d\n", i);
            return i;
        }
    }
    // No room left: add more entries.
    int newentry = OneTimeSchedulesCount;
    OneTimeSchedulesCount += 8;
    OneTimeSchedules = realloc (OneTimeSchedules,
                                sizeof(SprinklerOneTimeSchedule) * OneTimeSchedulesCount);
    for (i = newentry; i < OneTimeSchedulesCount; ++i) {
        OneTimeSchedules[i].program = 0;
        OneTimeSchedules[i].start = 0;
    }
    DEBUG ("Using new one-time schedule entry %d\n", newentry);
    return newentry;
}

static void housesprinkler_schedule_restore (void) {

    // Only one sprinkler controller can be active at a time.
    SprinklerOn = housesprinkler_state_get (".on");
    if (SprinklerOn) {
        const char *active = housesprinkler_state_get_string (".host");
        if (active && strcmp (active, sprinkler_host())) SprinklerOn = 0;
    }
    housesprinkler_state_share (SprinklerOn);

    RainDelay = (time_t)housesprinkler_state_get (".raindelay");
    if (RainDelay < time(0)) RainDelay = 0; // Expired.

    if (!Schedules) return; // To early for restoring.

    // Restore latest one-time schedules. Two steps: cancel whatever exist
    // and then reload the whole list.
    //
    DEBUG ("Restore all one-time schedules from state backup\n");
    int i;
    time_t now = time(0);
    for (i = 0; i < OneTimeSchedulesCount; ++i) {
        if (OneTimeSchedules[i].program) {
            free (OneTimeSchedules[i].program);
            OneTimeSchedules[i].program = 0;
        }
        OneTimeSchedules[i].start = 0;
    }
    i = 0;
    for (;;) {
        char path[128];
        snprintf (path, sizeof(path), ".once[%d].program", i);
        const char *pgm = housesprinkler_state_get_string (path);
        if (!pgm) break;
        snprintf (path, sizeof(path), ".once[%d].start", i);
        time_t start = (time_t)housesprinkler_state_get (path);
        if (start < now - (3 * 24 * 60 * 60)) continue; // Too old.
        int j = housesprinkler_schedule_new_onetime ();
        OneTimeSchedules[j].program = strdup (pgm);
        OneTimeSchedules[j].start = start;
        i += 1;
    }

    DEBUG ("Restore schedules live info from state backup\n");
    i = 0;
    for (;;) {
        uuid_t uuid;
        char path[128];
        snprintf (path, sizeof(path), ".schedule[%d].id", i);
        const char *id = housesprinkler_state_get_string (path);
        if (!id) break;
        uuid_parse (id, uuid);
        int j;
        for (j = 0; j < SchedulesCount; ++j) {
            if (uuid_compare (uuid, Schedules[j].id)) continue;
            snprintf (path, sizeof(path), ".schedule[%d].launched", i);
            Schedules[j].lastlaunch = housesprinkler_state_get (path);
            DEBUG ("Schedule %d (%s at %02d:%02d) recovers data from backup: lastlaunch = %ld\n", j, Schedules[j].program, Schedules[j].start.hour, Schedules[j].start.minute, (long)(Schedules[j].lastlaunch));
            housesprinkler_program_scheduled (Schedules[j].program,
                                              Schedules[j].lastlaunch);
            // If the name of the program launched has changed, also update
            // the program that was used at the time of the launch.
            snprintf (path, sizeof(path), ".schedule[%d].program", i);
            const char *pgrm = housesprinkler_state_get_string (path);
            if (pgrm && strcmp(pgrm, Schedules[j].program)) {
                housesprinkler_program_scheduled (pgrm,
                                                  Schedules[j].lastlaunch);
            }
            break;
        }
        i += 1;
    }
}

void housesprinkler_schedule_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];
    const char *programname = ".program";

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

    if (oldschedules) {
        // This is a configuration change, not a program start.
        // recover the schedule start times from the old configuration.
        //
        DEBUG ("Restore from old schedule\n");
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

    // Program start (this is the first time the configuration is loaded),
    // restore the last known state:
    // - on/off state,
    // - rain delay,
    // - launch time of already existing schedules.

    housesprinkler_schedule_restore ();
}

void housesprinkler_schedule_switch (void) {

    SprinklerOn = !SprinklerOn;
    houselog_event ("PROGRAM", "SWITCH", SprinklerOn?"ON":"OFF", "");
    housesprinkler_state_share (SprinklerOn);
    housesprinkler_state_changed();
}

void housesprinkler_schedule_rain (int enabled) {
    if (RainDelayEnabled == enabled) return; // No change.
    RainDelayEnabled = enabled;
    if (!enabled) {
        if (RainDelay > time(0)) {
           RainDelay = 0;
           housesprinkler_state_changed();
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
    housesprinkler_state_changed();
}

void housesprinkler_schedule_once (const char *program, time_t start) {

    if (!SprinklerOn) return;

    time_t now = time(0);
    if (start < now) return; // Past start is invalid.
    if (start > now + (3 * 24 * 60 * 60)) return; // Too far in the future.

    int i = housesprinkler_schedule_new_onetime ();
    OneTimeSchedules[i].program = strdup(program);
    if (start < now - 70) start += (24 * 60 * 60); // Too close: next day.
    OneTimeSchedules[i].start = start;
    housesprinkler_state_changed();
}

void housesprinkler_schedule_again (const char *id) {

    if (!SprinklerOn) return;

    uuid_t uuid;
    uuid_parse (id, uuid);
    int j;
    for (j = 0; j < SchedulesCount; ++j) {
        if (!uuid_compare (uuid, Schedules[j].id)) break;
    }
    if (j >= SchedulesCount) {
        DEBUG ("Cannot run again non-existent schedule %s\n", id);
        return; // This schedule does not exist.
    }

    int i = housesprinkler_schedule_new_onetime ();
    OneTimeSchedules[i].program = strdup (Schedules[j].program);

    time_t now = time(0);
    struct tm local = *localtime (&now);
    local.tm_sec = 0;
    local.tm_min = Schedules[j].start.minute;
    local.tm_hour = Schedules[j].start.hour;
    time_t start = mktime (&local);
    if (start < now - 70) start += (24 * 60 * 60); // Past or close: next day.
    OneTimeSchedules[i].start = start;
    housesprinkler_state_changed();
}

void housesprinkler_schedule_cancel (const char *program) {

    if (!SprinklerOn) return;

    int i;
    for (i = 0; i < OneTimeSchedulesCount; ++i) {
        if (!OneTimeSchedules[i].start) continue; // Inactive entry.
        if (!OneTimeSchedules[i].program) continue; // Better safe than sorry.
        if (!strcmp (OneTimeSchedules[i].program, program)) {
            OneTimeSchedules[i].start = 0;
            housesprinkler_state_changed();
            return;
        }
    }
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

    DEBUG ("== Checking one time schedules at %02d:%02d\n", local.tm_hour, local.tm_min);
    int i;
    for (i = 0; i < OneTimeSchedulesCount; ++i) {
        time_t start = OneTimeSchedules[i].start;
        if (!start) continue; // Not an active entry.
        if (start > now) continue; // Not yet..

        const char *program = OneTimeSchedules[i].program;
        time_t started = housesprinkler_program_start_scheduled (program);
        if (!started) continue; // Something prevented it from starting now.

        DEBUG ("== Program %s activated once at %lld\n", program, (long long)started);
        // TBD: this new "started" time is not saved?
        OneTimeSchedules[i].start = 0; // Don't do it again.
        housesprinkler_state_changed();
    }

    DEBUG ("== Checking schedules at %02d:%02d\n", local.tm_hour, local.tm_min);

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
        // We use a 6 hours (21600 sec) leniency to account for changes
        // to the schedule start time, for example when the start time is
        // changed to be a few hours early.
        // The last launch time used is the highest of two informations:
        // - The last launch time for this schedule.
        // - The last launch time for the program referenced by the schedule.
        // Doing it this way handles the cases when multiple schedules refer
        // to the same program, and when the program name referenced in
        // the schedule has changed (a corner case if there is one).
        //
        if (schedule->interval > 1) {
            time_t t0 = housesprinkler_program_scheduled (schedule->program, 0);
            if (schedule->lastlaunch > t0)
                t0 = schedule->lastlaunch;
            if (((now - t0 + 21600) / 86400) < schedule->interval) continue;
        }
        DEBUG ("== Program %s: interval %d has passed\n", schedule->program, schedule->interval);

        time_t started =
            housesprinkler_program_start_scheduled (schedule->program);
        if (started) {
            DEBUG ("== Program %s activated at %ld\n", schedule->program, (long)started);
            schedule->lastlaunch = started;
            housesprinkler_state_changed();
        }
    }
}

int housesprinkler_schedule_status (char *buffer, int size) {

    int cursor = snprintf (buffer, size,
                           "\"on\":%s", SprinklerOn?"true":"false");
    if (cursor >= size) goto overflow;

    if (RainDelayEnabled) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            ",\"raindelay\":%d", RainDelay);
        if (cursor >= size) goto overflow;
    }

    int i;
    char ascii[40];

    const char *sep = ",\"once\":[";
    for (i = 0; i < OneTimeSchedulesCount; ++i) {
        if (!OneTimeSchedules[i].start) continue; // Not an active entry.
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"program\":\"%s\",\"start\":%lld}",
                            sep,
                            OneTimeSchedules[i].program,
                            (long long)(OneTimeSchedules[i].start));
        if (cursor >= size) goto overflow;
        sep = ",";
    }
    if (sep[1] == 0)
        cursor += snprintf (buffer+cursor,size-cursor,"]");

    sep = ",\"schedules\":[";
    for (i = 0; i < SchedulesCount; ++i) {
        uuid_unparse (Schedules[i].id, ascii);
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s{\"id\":\"%s\",\"program\":\"%s\",\"start\":\"%02d:%02d\",\"launched\":%ld}",
                            sep, ascii, Schedules[i].program, Schedules[i].start.hour, Schedules[i].start.minute, (long)(Schedules[i].lastlaunch));
        if (cursor >= size) goto overflow;
        sep = ",";
    }
    if (sep[1] == 0)
        cursor += snprintf (buffer+cursor,size-cursor,"]");
    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "STATUS",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    buffer[0] = 0;
    return 0;
}

void housesprinkler_schedule_initialize (int argc, const char **argv) {

    housesprinkler_state_listen (housesprinkler_schedule_restore);
    housesprinkler_state_register (housesprinkler_schedule_status);
}

