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

const char *housesprinkler_config_load (int argc, const char **argv);

const char *housesprinkler_config_latest (void);

const char *housesprinkler_config_save (const char *text);

const char *housesprinkler_config_string  (int parent, const char *path);
int         housesprinkler_config_integer (int parent, const char *path);
int         housesprinkler_config_positive (int parent, const char *path);
int         housesprinkler_config_boolean (int parent, const char *path);

int housesprinkler_config_array        (int parent, const char *path);
int housesprinkler_config_array_length (int array);
int housesprinkler_config_enumerate    (int parent, int *index, int size);
int housesprinkler_config_object       (int parent, const char *path);

const char *housesprinkler_config_name (void);

typedef int BackupWorker (char *buffer, int size);
void housesprinkler_config_backup_register (BackupWorker *worker);

long housesprinkler_config_backup_get (const char *path);
const char *housesprinkler_config_backup_get_string (const char *path);
void housesprinkler_config_backup_changed (void);

void housesprinkler_config_periodic (void);

