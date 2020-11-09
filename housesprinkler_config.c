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
 * const char *housesprinkler_config_string  (int parent, const char *path);
 * int         housesprinkler_config_integer (int parent, const char *path);
 * double      housesprinkler_config_boolean (int parent, const char *path);
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
 */

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp_json.h>

#include "housesprinkler.h"
#include "housesprinkler_config.h"

#define DEBUG if (echttp_isdebug()) printf

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

static const char *housesprinkler_config_refresh (void) {

    char *newconfig;

    DEBUG ("Loading config from %s\n", ConfigFile);

    UseFactoryDefaults = 0;
    newconfig = echttp_parser_load (ConfigFile);
    if (!newconfig) {
        DEBUG ("Loading config from %s\n", FactoryDefaultsConfigFile);
        UseFactoryDefaults = 1;
        newconfig = echttp_parser_load (FactoryDefaultsConfigFile);
        if (!newconfig) return "not accessible";
    }

    if (ConfigText) echttp_parser_free (ConfigText);
    ConfigText = newconfig;
    ConfigTextLength = strlen(ConfigText);

    ConfigTokenCount = CONFIGMAXSIZE;
    return echttp_json_parse (ConfigText, ConfigParsed, &ConfigTokenCount);
}

const char *housesprinkler_config_load (int argc, const char **argv) {

    struct stat filestat;
    int fd;
    const char *config;

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-config=", argv[i], &config))
            ConfigFile = strdup(config);
    }
    return housesprinkler_config_refresh ();
}

const char *housesprinkler_config_save (const char *text) {

    int fd;

    DEBUG("Saving to %s: %s\n", ConfigFile, text);
    fd = open (ConfigFile, O_WRONLY+O_CREAT, 0777);
    if (fd < 0) {
        DEBUG("Cannot save to %s\n", ConfigFile);
        return "cannot save to file";
    }
    write (fd, text, strlen(text));
    close (fd);
    return housesprinkler_config_refresh ();
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

