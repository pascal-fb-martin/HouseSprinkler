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


static const char *housesprinkler_time_print (int h, const char *hlabel,
                                              int l, const char *llabel) {
    static char Printable[128];
    if (l > 0) {
        snprintf (Printable, sizeof(Printable),
                  "%d %s%s, %d %s%s", h, hlabel, (h>1)?"S":"",
                                      l, llabel, (l>1)?"S":"");
    } else {
        snprintf (Printable, sizeof(Printable),
                  "%d %s%s", h, hlabel, (h>1)?"S":"");
    }
    return Printable;
}

const char *housesprinkler_time_delta_printable (time_t start, time_t end) {
    static char Printable[128];
    int delta = (int)(end - start);

    if (delta <= 0) return "NOW";
    if (delta > 86400) {
        return housesprinkler_time_print (delta / 86400, "DAY", (delta % 86400) / 3600, "HOUR");
    } else if (delta > 3600) {
        return housesprinkler_time_print (delta / 3600, "HOUR", (delta % 3600) / 60, "MINUTE");
    } else if (delta > 60) {
        return housesprinkler_time_print (delta / 60, "MINUTE", delta % 60, "SECOND");
    }
    return housesprinkler_time_print (delta, "SECOND", 0, "");
}

