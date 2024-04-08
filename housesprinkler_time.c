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
 * housesprinkler_time.c - Time operations for the sprinkler controller.
 *
 * SYNOPSYS:
 *
 * const char *housesprinkler_time_delta_printable (time_t start, time_t end);
 *
 *    This function generates a string showing the time delta from start to
 *    end in a user friendly way (days and hours, or hours and minutes, or
 *    minutes and seconds).
 *
 *    The returned string is a static location: every call erase the previous
 *    result.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_time.h"

#define DEBUG if (echttp_isdebug()) printf


const char *housesprinkler_time_delta_printable (time_t start, time_t end) {
    static char Printable[128];
    int delta = (int)(end - start);

    if (delta <= 0) return "NOW";
    if (delta > 86400) {
        int days = delta / 86400;
        int hours = (delta % 86400) / 3600;
        if (hours > 0) {
            snprintf (Printable, sizeof(Printable),
                      "%d DAYS, %d HOURS", days, hours);
        } else {
            snprintf (Printable, sizeof(Printable), "%d DAYS", days);
        }
    } else if (delta > 3600) {
        int hours = delta / 3600;
        int minutes = (delta % 3600) / 60;
        if (minutes > 0) {
            snprintf (Printable, sizeof(Printable),
                      "%d HOURS, %d MINUTES", hours, minutes);
        } else {
            snprintf (Printable, sizeof(Printable), "%d HOURS", hours);
        }
    } else if (delta > 60) {
        int minutes = delta / 60;
        int seconds = delta % 60;
        if (seconds > 0) {
            snprintf (Printable, sizeof(Printable),
                      "%d MINUTES, %d SECONDS", minutes, seconds);
        } else {
            snprintf (Printable, sizeof(Printable), "%d MINUTES", minutes);
        }
    } else {
        snprintf (Printable, sizeof(Printable), "%d SECONDS", delta);
    }
    return Printable;
}

