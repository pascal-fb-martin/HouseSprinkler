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
 */

void housesprinkler_program_refresh (void);

void housesprinkler_program_index (int state);
void housesprinkler_program_set_index
         (const char *origin, int value, time_t timestamp);

void   housesprinkler_program_start_manual    (const char *name);
time_t housesprinkler_program_start_scheduled (const char *name);
int    housesprinkler_program_running         (const char *name);

time_t housesprinkler_program_scheduled (const char *name, time_t scheduled);

void housesprinkler_program_periodic (time_t now);
int  housesprinkler_program_status (char *buffer, int size);

void housesprinkler_program_switch (void);

