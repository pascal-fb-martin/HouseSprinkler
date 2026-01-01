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
 * housesprinkler_interval.c - Manage variable schedule intervals.
 *
 * SYNOPSYS:
 *
 * void housesprinkler_interval_refresh (void);
 *
 *    This function must be called each time the sprinkler configuration
 *    has been changed. It converts the configured interval scales into
 *    activable ones.
 *
 * int housesprinkler_interval_exists (const char *name);
 *
 *    Return 1 if the named interval scale exists, 0 otherwise. This is
 *    used to validate any part of the HouseSprinkler's configuration that
 *    references an interval scale.
 *
 * int housesprinkler_interval_get (const char *name, int index);
 *
 *    Return the current interval value based on the named scale and the
 *    current watering index value.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_config.h"
#include "housesprinkler_interval.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define SCALE_LIMIT 11 // from 0 to 100, in steps of 10.

typedef struct {
    const char *name;
    int count;
    int byindex[SCALE_LIMIT];
} SprinklerIntervals;

static SprinklerIntervals *Intervals = 0;
static int IntervalsCount = 0;

static int housesprinkler_interval_find (const char *name) {
    int i;
    if (!name) return -1;
    for (i = 0; i < IntervalsCount; ++i) {
        if (strcmp (Intervals[i].name, name) == 0) return i;
    }
    return -1;
}

int housesprinkler_interval_exists (const char *name) {
    return housesprinkler_interval_find (name) >= 0;
}

void housesprinkler_interval_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];

    // Reload all intervals.
    //
    if (Intervals) free (Intervals);
    Intervals = 0;
    IntervalsCount = 0;
    content = housesprinkler_config_array (0, ".intervals");
    if (content > 0) {
        IntervalsCount = housesprinkler_config_array_length (content);
        if (IntervalsCount > 0) {
            Intervals = calloc (IntervalsCount, sizeof(SprinklerIntervals));
        }
    }

    DEBUG ("Loading %d interval scales\n", IntervalsCount);

    for (i = 0; i < IntervalsCount; ++i) {
        snprintf (path, sizeof(path), "[%d]", i);
        int scale = housesprinkler_config_object (content, path);
        if (scale > 0) {
            Intervals[i].name = housesprinkler_config_string (scale, ".name");
            if (!Intervals[i].name) continue; // Bad entry.

            int index = housesprinkler_config_array (scale, ".byindex");
            if (index <= 0) {
                DEBUG ("Bad interval scale array\n");
                continue;
            }
            count = housesprinkler_config_array_length(index);
            if (count > SCALE_LIMIT) {
                DEBUG ("Interval scale %s: array of %d truncated to %d\n",
                       Intervals[i].name, count, SCALE_LIMIT);
                count = SCALE_LIMIT;
            }
            for (j = 0; j < count; ++j) {
                snprintf (path, sizeof(path), "[%d]", j);
                Intervals[i].byindex[j] =
                    housesprinkler_config_positive (index, path);
            }
            Intervals[i].count = count;
            DEBUG ("\tInterval %s loaded (%d items).\n", Intervals[i].name, count);
        }
    }
}

int housesprinkler_interval_get (const char *name, int index) {

    int scale = housesprinkler_interval_find (name);
    if (scale < 0) return 0; // No interval scale, just assume every day.

    index /= 10;
    if (index >= SCALE_LIMIT) index = SCALE_LIMIT - 1;
    else if (index < 0) index = 0; // Better safe than sorry.

    return Intervals[scale].byindex[index];
}

