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
 * void housesprinkler_config_backup_register (BackupWorker *worker);
 * long housesprinkler_config_backup_get (const char *path);
 * const char *housesprinkler_config_backup_get_string (const char *path);
 * void housesprinkler_config_backup_changed (void);
 *
 *    Read from, and write to, the backup file. This backup file contains
 *    saved live values that can be changed from the user interface and must
 *    survive a program restart. Supported data types are boolean, integer and
 *    string (for now). A boolean is reported as an integer (0 or 1).
 *
 *    Modules that need to backup data must register a worker function
 *    That populates the JSON structure.
 *
 *    Note that saving the backup data is asynchronous: the client indicates
 *    that the data has changed, but saving the data will be decided later.
 *    The reason for this is that multiple clients might change their data
 *    at around the same time, but we do not want to save each time: it is
 *    better to delay and do the save only once.
 *
 * void housesprinkler_config_periodic (void);
 *
 *    Background config activity (mostly: save data when changed).
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

static ParserToken *ConfigParsed = 0;
static int   ConfigTokenAllocated = 0;
static int   ConfigTokenCount = 0;
static char *ConfigText = 0;
static int   ConfigTextSize = 0;
static int   ConfigTextLength = 0;

static const char *ConfigFile = "/etc/house/sprinkler.json";

static const char FactoryDefaultsConfigFile[] =
                      "/usr/local/share/house/public/sprinkler/defaults.json";
static int UseFactoryDefaults = 0;

static ParserToken *BackupParsed = 0;
static int   BackupTokenAllocated = 0;
static int   BackupTokenCount = 0;
static char *BackupText = 0;

static const char *BackupFile = "/etc/house/sprinklerbkp.json";

static const char FactoryBackupFile[] =
                      "/usr/local/share/house/public/sprinkler/backup.json";

static int BackupDataHasChanged = 0;


static const char *housesprinkler_config_parse (char *text) {
    int count = echttp_json_estimate(text);
    if (count > ConfigTokenAllocated) {
        if (ConfigParsed) free (ConfigParsed);
        ConfigTokenAllocated = count;
        ConfigParsed = calloc (ConfigTokenAllocated, sizeof(ParserToken));
    }
    ConfigTokenCount = ConfigTokenAllocated;
    const char *error = echttp_json_parse (text, ConfigParsed, &ConfigTokenCount);
    DEBUG ("Planned config for %d JSON tokens, got %d\n", ConfigTokenAllocated, ConfigTokenCount);
    return error;
}

static void housesprinkler_config_loadbackup (void) {

    char *newconfig;

    if (BackupText) echttp_parser_free (BackupText);
    BackupText = 0;
    BackupTokenCount = 0;

    const char *name = BackupFile;
    DEBUG ("Loading backup from %s\n", name);
    newconfig = echttp_parser_load (name);
    if (!newconfig) {
        name = FactoryBackupFile;
        DEBUG ("Loading backup from %s\n", name);
        newconfig = echttp_parser_load (name);
        BackupDataHasChanged = 1; // Force creation of the backup file.
    }

    if (newconfig) {
        const char *error;
        houselog_event ("SYSTEM", "BACKUP", "LOAD", "FILE %s", name);
        BackupText = newconfig;
        BackupTokenCount = echttp_json_estimate(BackupText);
        if (BackupTokenCount > BackupTokenAllocated) {
            BackupTokenAllocated = BackupTokenCount+64;
            if (BackupParsed) free (BackupParsed);
            BackupParsed = calloc (BackupTokenAllocated, sizeof(ParserToken));
        }
        error = echttp_json_parse (BackupText, BackupParsed, &BackupTokenCount);
        if (error) {
            DEBUG ("Backup config parsing error: %s\n", error);
            echttp_parser_free (BackupText);
            BackupText = 0;
            BackupTokenCount = 0;
        } else {
            DEBUG ("Planned %d, read %d items of backup config\n", BackupTokenAllocated, BackupTokenCount);
        }
    }
}

const char *housesprinkler_config_load (int argc, const char **argv) {

    struct stat filestat;
    int fd;
    char *newconfig;

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-config=", argv[i], &ConfigFile)) continue;
        if (echttp_option_match ("-backup=", argv[i], &BackupFile)) continue;
    }

    housesprinkler_config_loadbackup ();

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

    return housesprinkler_config_parse (ConfigText);
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

    error = housesprinkler_config_parse (newconfig);
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

const char *housesprinkler_config_backup_get_string (const char *path) {

    int i = echttp_json_search(BackupParsed, path);
    if (i < 0) return 0;
    switch (BackupParsed[i].type) {
        case PARSER_STRING: return BackupParsed[i].value.string;
    }
    return 0;
}

long housesprinkler_config_backup_get (const char *path) {

    // Support boolean and integer, all converted to integer.
    // Anything else: return 0
    //
    long value = 0;
    int i = echttp_json_search(BackupParsed, path);
    if (i < 0) return 0;
    switch (BackupParsed[i].type) {
        case PARSER_BOOL: value = BackupParsed[i].value.bool; break;
        case PARSER_INTEGER: value = BackupParsed[i].value.integer; break;
    }
    return value;
}

// The backup mechanism relies on collaboration from the modules that
// need to backup data: only these modules know what data is to be saved.
// For the time being, assume that there is no more than 16 of these modules..
//
static BackupWorker *BackupRegistered[16];
static int BackupRegisteredCount = 0;

void housesprinkler_config_backup_register (BackupWorker *worker) {

    int i;
    if (! worker) return; // Just a check against gross error.
    for (i = 0; i < BackupRegisteredCount; ++i) {
        if (BackupRegistered[i] == worker) return; // Already registered.
    }
    if (BackupRegisteredCount < 16) {
        BackupRegistered[BackupRegisteredCount++] = worker;
    }
}

void housesprinkler_config_backup_changed (void) {
    if (!BackupDataHasChanged)
        DEBUG("Backup data has changed.\n");
    BackupDataHasChanged = 1;
}

static int housesprinkler_config_backup_save (void) {

    static char *buffer = 0;
    static int size = 0;

    int status = 1;
    int cursor = 0;
    int i;
    if (!buffer) {
       size = 1024;
       buffer = malloc(size);
    }
    DEBUG("Saving backup data to %s\n", BackupFile);
    const char *sep = "{";
    for (i = 0; i < BackupRegisteredCount; ++i) {
        cursor += snprintf (buffer+cursor, size-cursor, sep);
        if (cursor >= size) goto abort;
        cursor += BackupRegistered[i] (buffer+cursor, size-cursor);
        if (cursor >= size) goto abort;
        sep = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "}");
    if (cursor >= size) goto abort;

    int fd = open (BackupFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd < 0) {
        DEBUG ("Cannot open %s\n", BackupFile);
        return 0; // Failure.
    }
    int written = write (fd, buffer, cursor);
    if (written < 0) {
        DEBUG ("Cannot write to %s\n", BackupFile);
        status = 0; // failure.
    } else {
        DEBUG ("Wrote %d characters to %s\n", written, BackupFile);
    }
    close(fd);
    return status;

abort:
    DEBUG ("Backup failed: buffer too small\n");
    size += 1024;
    free (buffer);
    buffer = malloc(size);
    return housesprinkler_config_backup_save (); // Retry.
}

void housesprinkler_config_periodic (void) {
    if (BackupDataHasChanged) {
        if (housesprinkler_config_backup_save()) BackupDataHasChanged = 0;
    }
}

