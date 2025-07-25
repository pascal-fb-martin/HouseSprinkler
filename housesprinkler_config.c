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
 * const char *housesprinkler_config_latest (void);
 *
 *    Return the JSON text corresponding to the latest config that was loaded,
 *    even if that config failed parsing..
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
#include "housedepositor.h"

#include "housesprinkler.h"
#include "housesprinkler_config.h"

#define DEBUG if (sprinkler_isdebug()) printf

static ParserToken *ConfigParsed = 0;
static int   ConfigTokenAllocated = 0;
static int   ConfigTokenCount = 0;
static char *ConfigText = 0;
static char *ConfigTextLatest = 0;

static int ConfigFileEnabled = 0; // Default: rely on the HouseDepot service.

static const char *ConfigFile = "/etc/house/sprinkler.json";

static const char FactoryDefaultsConfigFile[] =
                      "/usr/local/share/house/public/sprinkler/defaults.json";
static int UseFactoryDefaults = 0;

static void housesprinkler_config_clear (const char *reason) {

    if (ConfigText) {
        echttp_parser_free (ConfigText);
        ConfigText = 0;
    }
    ConfigTokenCount = 0;
    DEBUG ("Config cleared (%s).\n", reason);
}

static const char *housesprinkler_config_parse (char *text) {
    int count = echttp_json_estimate(text);
    if (count > ConfigTokenAllocated) {
        if (ConfigParsed) free (ConfigParsed);
        ConfigTokenAllocated = count;
        ConfigParsed = calloc (ConfigTokenAllocated, sizeof(ParserToken));
    }
    if (ConfigTextLatest) free (ConfigTextLatest);
    ConfigTextLatest = strdup (text);
    DEBUG ("New configuration: %s\n", ConfigTextLatest);

    ConfigTokenCount = ConfigTokenAllocated;
    const char *error = echttp_json_parse (text, ConfigParsed, &ConfigTokenCount);
    DEBUG ("Planned config for %d JSON tokens, got %d\n", ConfigTokenAllocated, ConfigTokenCount);
    if (error) {
        houselog_event ("SYSTEM", "CONFIG", "FAILED", "%s", error);
        DEBUG ("Config error: %s\n", error);
    }
    return error;
}

static const char *housesprinkler_config_write (const char *text, int length) {

    if (!ConfigFileEnabled) return 0; // No error.

    DEBUG("Saving to %s: %s\n", ConfigFile, text);
    int fd = open (ConfigFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd < 0) {
        char *desc = strdup(strerror(errno));
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "Cannot save to %s: %s", ConfigFile, desc);
        free (desc);
        return "cannot save to file";
    }
    write (fd, text, length);
    close (fd);
    return 0;
}

static void housesprinkler_config_listener (const char *name, time_t timestamp,
                                            const char *data, int length) {

    houselog_event ("SYSTEM", "CONFIG", "LOAD", "FROM DEPOT %s", name);

    housesprinkler_config_clear ("new depot config detected");
    ConfigText = echttp_parser_string (data);

    housesprinkler_config_write (data, length);
    housesprinkler_config_parse (ConfigText);
    UseFactoryDefaults = 0;
    sprinkler_refresh ();
}

const char *housesprinkler_config_load (int argc, const char **argv) {

    char *newconfig;

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-config=", argv[i], &ConfigFile)) continue;
        if (echttp_option_present ("-use-local-storage", argv[i])) {
            ConfigFileEnabled = 1;
            continue;
        }
        if (echttp_option_present ("-no-local-storage", argv[i])) {
            ConfigFileEnabled = 0;
            continue;
        }
    }

    housedepositor_subscribe ("config", "sprinkler.json",
                              housesprinkler_config_listener);

    if (!ConfigFileEnabled) return 0; // No error.

    DEBUG ("Loading config from %s\n", ConfigFile);

    housesprinkler_config_clear ("loading..");

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

    ConfigText = newconfig;

    return housesprinkler_config_parse (ConfigText);
}

const char *housesprinkler_config_save (const char *text) {

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

    housesprinkler_config_clear ("new configuration");
    newconfig = echttp_parser_string(text);

    error = housesprinkler_config_parse (newconfig);
    if (error) {
        houselog_trace (HOUSE_FAILURE, "CONFIG",
                        "JSON error %s on %-0.60s", error, text);
        free (newconfig);
        return error;
    }
    ConfigText = newconfig;

    houselog_event ("SYSTEM", "CONFIG", "SAVE", "TO DEPOT sprinkler.json");
    housedepositor_put ("config", "sprinkler.json", text, length);

    error = housesprinkler_config_write (text, length);
    if (error) return error;

    UseFactoryDefaults = 0;
    houselog_event ("SYSTEM", "CONFIG", "UPDATED", "FILE %s", ConfigFile);
    return 0;
}

const char *housesprinkler_config_latest (void) {
    return ConfigTextLatest;
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

void housesprinkler_config_periodic (void) {
}

