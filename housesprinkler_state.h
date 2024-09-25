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
 */

void housesprinkler_state_load (int argc, const char **argv);

void housesprinkler_state_share (int on);

typedef void BackupListener (void);
void housesprinkler_state_listen (BackupListener *listener);

typedef int BackupWorker (char *buffer, int size);
void housesprinkler_state_register (BackupWorker *worker);

long housesprinkler_state_get (const char *path);
const char *housesprinkler_state_get_string (const char *path);
void housesprinkler_state_changed (void);

void housesprinkler_state_periodic (time_t now);

