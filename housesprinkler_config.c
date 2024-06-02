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
 * housesprinkler_config.c - Access the sprinkler config.
 *
 * SYNOPSYS:
 *
 * const char *housesprinkler_config_load (int argc, const char **argv);
 *
 *    Load the configuration from the specified config option, or else
 *    from the default config file.
 *
 * int housesprinkler_config_file (void);
 * int housesprinkler_config_size (void);
 *
 *    Return a file descriptor (and the size) of the configuration file
 *    being used.
 *
 * const char *housesprinkler_config_save (const char *text);
 *
 *    Update both the live configuration and the configuration file with
 *    the provided text.
 *
 * int         housesprinkler_config_exists  (int parent, const char *path);
 * const char *housesprinkler_config_string  (int parent, const char *path);
 * int         housesprinkler_config_integer (int parent, const char *path);
 * int         housesprinkler_config_boolean (int parent, const char *path);
 *
 *    Access individual items starting from the specified parent
 *    (the config root is index 0).
 *
 * int housesprinkler_config_array (int parent, const char *path);
 * int housesprinkler_config_array_length (int array);
 *
 *    Retrieve an array.
 * 
 * int housesprinkler_config_enumerate (int parent, int *index);
 *
 *    Retrieve the elements of an array or object.
 *
 * int housesprinkler_config_object (int parent, const char *path);
 *
 *    Retrieve an object.
 * 
 * const char *housesprinkler_config_name (void);
 *
 *    Get the name of the current configuration file, for informational
 *    purpose (e.g. error messages).
 *
 * long housesprinkler_config_backup_get (const char *path);
 * void housesprinkler_config_backup_set (const char *path, long value);
 *
 *    Read from, and write to, the backup file. This backup file contains
 *    a handful of saved values that are system-wide and can be changed
 *    from the user interface. They are all integer type.
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp_json.h>

#include "houselog.h"

#include "housesprinkler.h"
#include "housesprinkler_config.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define CONFIGMAXSIZE 1024

static ParserToken ConfigParsed[CONFIGMAXSIZE];
static int   ConfigTokenCount = 0;
static char *ConfigText = 0;
static int   ConfigTextSize = 0;
static int   ConfigTextLength = 0;

static const char *ConfigFile = "/etc/house/sprinkler.json";

static const char FactoryDefaultsConfigFile[] =
                      "/usr/local/share/house/public/sprinkler/defaults.json";
static int UseFactoryDefaults = 0;

#define BACKUPMAXSIZE 32

static ParserToken BackupParsed[BACKUPMAXSIZE];
static int   BackupTokenCount = 0;
static char *BackupText = 0;

static const char *BackupFile = "/etc/house/sprinklerbkp.json";

static const char FactoryBackupFile[] =
                      "/usr/local/share/house/public/sprinkler/backup.json";


const char *housesprinkler_config_load (int argc, const char **argv) {

    struct stat filestat;
    int fd;
    char *newconfig;

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-config=", argv[i], &ConfigFile)) continue;
        if (echttp_option_match ("-backup=", argv[i], &BackupFile)) continue;
    }

    DEBUG ("Loading backup from %s\n", BackupFile);
    if (BackupText) echttp_parser_free (BackupText);
    BackupText = 0;
    BackupTokenCount = 0;
    newconfig = echttp_parser_load (BackupFile);
    if (!newconfig) {
        DEBUG ("Loading backup from %s\n", FactoryBackupFile);
        newconfig = echttp_parser_load (FactoryBackupFile);
        houselog_event ("SYSTEM", "BACKUP", "LOAD",
                        "FILE %s", FactoryBackupFile);
    } else {
        houselog_event ("SYSTEM", "BACKUP", "LOAD",
                        "FILE %s", BackupFile);
    }
    if (newconfig) {
        BackupText = newconfig;
        BackupTokenCount = BACKUPMAXSIZE;
        if (echttp_json_parse (BackupText, BackupParsed, &BackupTokenCount)) {
            echttp_parser_free (BackupText);
            BackupText = 0;
            BackupTokenCount = 0;
        }
    }

    DEBUG ("Loading config from %s\n", ConfigFile);

    UseFactoryDefaults = 0;
    newconfig = echttp_parser_load (ConfigFile);
    if (!newconfig) {
        DEBUG ("Loading config from %s\n", FactoryDefaultsConfigFile);
        UseFactoryDefaults = 1;
        newconfig = echttp_parser_load (FactoryDefaultsConfigFile);
        if (!newconfig) return "not accessible";
        houselog_event ("SYSTEM", "CONFIG", "LOAD",
                        "FILE %s", FactoryDefaultsConfigFile);

    } else {
        houselog_event ("SYSTEM", "CONFIG", "LOAD", "FILE %s", ConfigFile);
    }

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = newconfig;
    ConfigTextLength = strlen(ConfigText);

    ConfigTokenCount = CONFIGMAXSIZE;
    return echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
}

const char *housesprinkler_config_save (const char *text) {

    int fd;
    int length = strlen(text);
    char *newconfig;
    const char *error;

    // Protect against bugs leading to the wrong string being used.
    //
    if (length < 10 || text[0] != '{') {
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "Invalid config string: %-0.60s (length %d)",
                        text, length);
        return "invalid string";
    }

    newconfig = echttp_parser_string(text);

    ConfigTokenCount = CONFIGMAXSIZE;
    error = echttp_json_parse (newconfig, ConfigParsed, &ConfigTokenCount);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "JSON error %s on %-0.60s", error, text);
        free (newconfig);
        return error;
    }

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = newconfig;
    ConfigTextLength = length;

    DEBUG("Saving to %s: %s\n", ConfigFile, text);
    fd = open (ConfigFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd < 0) {
        char *desc = strdup(strerror(errno));
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "Cannot save to %s: %s", ConfigFile, desc);
        free (desc);
        return "cannot save to file";
    }
    write (fd, text, length);
    close (fd);

    UseFactoryDefaults = 0;
    houselog_event ("SYSTEM", "CONFIG", "UPDATED", "FILE %s", ConfigFile);
    return 0;
}

int housesprinkler_config_file (void) {
    if (UseFactoryDefaults)
        return open(FactoryDefaultsConfigFile, O_RDONLY);
    return open(ConfigFile, O_RDONLY);
}

int housesprinkler_config_size (void) {
    return ConfigTextLength;
}

int housesprinkler_config_find (int parent, const char *path, int type) {
    int i;
    if (parent < 0 || parent >= ConfigTokenCount) return -1;
    i = echttp_json_search(ConfigParsed+parent, path);
    if (i >= 0 && ConfigParsed[parent+i].type == type) return parent+i;
    return -1;
}

int housesprinkler_config_exists  (int parent, const char *path) {
    if (parent < 0 || parent >= ConfigTokenCount) return 0;
    return (echttp_json_search(ConfigParsed+parent, path) >= 0);
}

const char *housesprinkler_config_string (int parent, const char *path) {
    int i = housesprinkler_config_find(parent, path, PARSER_STRING);
    return (i >= 0) ? ConfigParsed[i].value.string : 0;
}

int housesprinkler_config_integer (int parent, const char *path) {
    int i = housesprinkler_config_find(parent, path, PARSER_INTEGER);
    return (i >= 0) ? ConfigParsed[i].value.integer : 0;
}

int housesprinkler_config_boolean (int parent, const char *path) {
    int i = housesprinkler_config_find(parent, path, PARSER_BOOL);
    return (i >= 0) ? ConfigParsed[i].value.bool : 0;
}

int housesprinkler_config_array (int parent, const char *path) {
    return housesprinkler_config_find(parent, path, PARSER_ARRAY);
}

int housesprinkler_config_array_length (int array) {
    if (array < 0
            || array >= ConfigTokenCount
            || ConfigParsed[array].type != PARSER_ARRAY) return 0;
    return ConfigParsed[array].length;
}

int housesprinkler_config_enumerate (int parent, int *index) {

    int i, length;

    if (parent < 0 || parent >= ConfigTokenCount) return 0;
    const char *error = echttp_json_enumerate (ConfigParsed+parent, index);
    if (error) {
        fprintf (stderr, "Cannot enumerate %s: %s\n",
                 ConfigParsed[parent].key, error);
        return -1;
    }
    length = ConfigParsed[parent].length;
    for (i = 0; i < length; ++i) index[i] += parent;
    return length;
}

int housesprinkler_config_object (int parent, const char *path) {
    return housesprinkler_config_find(parent, path, PARSER_OBJECT);
}

const char *housesprinkler_config_name (void) {
    return ConfigFile;
}

long housesprinkler_config_backup_get (const char *path) {

    int i = echttp_json_search(BackupParsed, path);
    return (i >= 0) ? BackupParsed[i].value.integer : 0;
}

void housesprinkler_config_backup_set (const char *path, long value) {

    char buffer[1024];
    int i = echttp_json_search(BackupParsed, path);

    if (i < 0) return;
    BackupParsed[i].value.integer = value;

    if (echttp_json_format (BackupParsed, BackupTokenCount,
                            buffer, sizeof(buffer), 0)) return;

    int fd = open (BackupFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd < 0) return;
    write (fd, buffer, strlen(buffer));
    close(fd);
}

