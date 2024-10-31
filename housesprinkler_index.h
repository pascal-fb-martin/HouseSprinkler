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
 * housesprinkler_index.h - Access watering index services.
 */

void housesprinkler_index_refresh (void);

const char *housesprinkler_index_origin (void);
int    housesprinkler_index_priority (void);
time_t housesprinkler_index_timestamp (void);
int    housesprinkler_index_get (void);

void housesprinkler_index_periodic (time_t now);

int housesprinkler_index_status (char *buffer, int size);

