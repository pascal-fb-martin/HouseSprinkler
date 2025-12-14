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
 * housesprinkler_season.c - Manage the watering seasons.
 *
 * SYNOPSYS:
 *
 * void housesprinkler_season_refresh (void);
 *
 *    This function must be called each time the sprinkler configuration
 *    has been changed. It converts the configured seasons into activable
 *    ones.
 *
 * int housesprinkler_season_priority (const char *name);
 *
 *    Return the priority of the specified season. or 0 if the season does
 *    not exist.
 *
 * int housesprinkler_season_index (const char *name);
 *
 *    Return the current watering index given the specific season setting,
 *    or 100 (full watering) if the season does not exist.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_config.h"
#include "housesprinkler_season.h"

#define DEBUG if (sprinkler_isdebug()) printf

typedef struct {
    const char *name;
    int priority;
    int unit;
    int index[52];
} SprinklerSeason;

#define SPRINKLER_SEASON_INVALID  0
#define SPRINKLER_SEASON_WEEKLY   1 // Weekly index (52 weeks).
#define SPRINKLER_SEASON_MONTHLY  2 // Monthly index (12 months).

static SprinklerSeason *Seasons = 0;
static int SeasonsCount = 0;

static int housesprinkler_season_find (const char *name) {
    int i;
    if (!name) return -1;
    for (i = 0; i < SeasonsCount; ++i) {
        if (strcmp (Seasons[i].name, name) == 0) return i;
    }
    return -1;
}

void housesprinkler_season_refresh (void) {

    int i, j;
    int count;
    int content;
    char path[128];

    // Reload all seasons.
    //
    if (Seasons) free (Seasons);
    Seasons = 0;
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

            Seasons[i].priority =
                housesprinkler_config_positive (season, ".priority");

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
                    housesprinkler_config_positive (index, path);
            }
            DEBUG ("\tSeason %s (%sly)\n",
                   Seasons[i].name,
                   (Seasons[i].unit==SPRINKLER_SEASON_WEEKLY)?"week":"month");
        }
    }
}

int housesprinkler_season_priority (const char *name) {

    int season = housesprinkler_season_find (name);
    if (season < 0) return 0; // No season, lowest priority.

    return Seasons[season].priority;
}

int housesprinkler_season_index (const char *name) {

    int season = housesprinkler_season_find (name);
    if (season < 0) return 100; // No season, no index. Go full water.

    int index = 100;

    // Week of the year. We do not care about getting the exact
    // result, just having something matching the period of the year.
    //
    time_t now = time(0);
    struct tm local = *localtime(&now);
    int week = (local.tm_yday - local.tm_wday + 4) / 7;
    if (week < 0) week = 51;
    else if (week >= 52) week -= 52;

    switch (Seasons[season].unit) {
        case SPRINKLER_SEASON_WEEKLY:
            index = Seasons[season].index[week];
            break;
        case SPRINKLER_SEASON_MONTHLY:
            index = Seasons[season].index[local.tm_mon];
            break;
        default:
            DEBUG ("Invalid season unit %d for %s\n",
                    Seasons[season].unit, Seasons[season].name);
    }
    return index;
}

