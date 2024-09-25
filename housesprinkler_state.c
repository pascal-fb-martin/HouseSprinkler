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
 * housesprinkler_state.c - Backup and restore the sprinkler state.
 *
 * The sprinkler state covers data items that the program needs to
 * function as expected when it restarts, such as:
 * - Scheduling on/off.
 * - Watering index enabled/disabled.
 * - When each schedule was last activated (used for interval calculation).
 * - etc.
 *
 * These items are not considered part of the configuration, for one of the
 * following reasons:
 * - It should only impact one sprinkler instance, not all of them, or
 * - This is data generated by the application, not entered by the user, or
 * - This is a separate item that should not cause a complete reconfiguration.
 *
 * This version uses both the local backup file _and_ the data from the
 * depot repository. The later has priority. This scheme has two benefits:
 * - seamless transition from ocal storage only to deport repositories.
 * - keep working even if the depot is not accessible.
 *
 * SYNOPSYS:
 *
 * void housesprinkler_state_share (int on);
 *
 *    Turn the depot mechanism on and off. The intent is to turn it
 *    on only when the sprinkler system is on. That does mean that turning
 *    the sprinkler system off will not be recorded in the depot's
 *    repository. The logic is to record which instance is on, not that
 *    a specific instance is off (there can be multiple instances off,
 *    and that does not identify which one is on).
 *
 * void housesprinkler_state_listen (BackupListener *listener);
 *
 *    Listen to external changes to the state backup. Such changes typically
 *    come from the depot repository.
 *
 * void housesprinkler_state_register (BackupWorker *worker);
 *
 *    Register a worker function to export a module's internal state to JSON.
 *    Worker functions are called when the state must be saved.
 *
 *    Modules that need to backup data must use this to register a worker
 *    function that exports the module's internal state to a JSON structure
 *    that will be saved to disk (local or depot repository)..
 *
 * long housesprinkler_state_get (const char *path);
 * const char *housesprinkler_state_get_string (const char *path);
 *
 *    Retrieve items from the state backup file. This backup file contains
 *    saved live values that can be changed from the user interface and must
 *    survive a program restart. Supported data types are boolean, integer and
 *    string (for now). A boolean is reported as an integer (0 or 1).
 *
 * void housesprinkler_state_changed (void);
 *
 *    Report that the internal state has changed.
 *
 *    Note that saving the backup data is asynchronous: the client indicates
 *    that the data has changed, but saving the data will be decided later.
 *    The reason for this is that multiple clients might change their data
 *    at around the same time, but we do not want to save each time: it is
 *    better to delay and do the save only once.
 *
 * void housesprinkler_state_periodic (time_t now);
 *
 *    Background state activity (mostly: save data when changed).
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <echttp_json.h>

#include "houselog.h"
#include "housedepositor.h"

#include "housesprinkler.h"
#include "housesprinkler_state.h"

#define DEBUG if (sprinkler_isdebug()) printf

static ParserToken *BackupParsed = 0;
static int   BackupTokenAllocated = 0;
static int   BackupTokenCount = 0;
static char *BackupInText = 0;

static const char *BackupFile = "/etc/house/sprinklerbkp.json";

static const char FactoryBackupFile[] =
                      "/usr/local/share/house/public/sprinkler/backup.json";

static time_t StateDataHasChanged = 0;

static int ShareStateData = 1;

static int BackupOrigin = 0;
#define BACKUP_ORIGIN_FILE  1
#define BACKUP_ORIGIN_DEPOT 2

static char *BackupOutBuffer = 0;
static int BackupOutBufferSize = 0;

// The backup mechanism relies on collaboration from the modules that
// need to backup data: only these modules know what data is to be saved.
//
static BackupListener **BackupRegisteredListener = 0;
static int BackupListenerSize = 0;
static int BackupListenerCount = 0;

void housesprinkler_state_listen (BackupListener *listener) {

    int i;
    if (! listener) return; // Just a check against gross error.
    for (i = 0; i < BackupListenerCount; ++i) {
        if (BackupRegisteredListener[i] == listener)
            return; // Already registered.
    }
    if (BackupListenerCount >= BackupListenerSize) {
        BackupListenerSize += 16;
        BackupRegisteredListener =
            realloc (BackupRegisteredListener,
                     sizeof(BackupListener *) * BackupListenerSize);
    }
    BackupRegisteredListener[BackupListenerCount++] = listener;
}

static BackupWorker **BackupRegisteredWorker = 0;
static int BackupWorkerSize = 0;
static int BackupWorkerCount = 0;

void housesprinkler_state_register (BackupWorker *worker) {

    int i;
    if (! worker) return; // Just a check against gross error.
    for (i = 0; i < BackupWorkerCount; ++i) {
        if (BackupRegisteredWorker[i] == worker) return; // Already registered.
    }
    if (BackupWorkerCount >= BackupWorkerSize) {
        BackupWorkerSize += 16;
        BackupRegisteredWorker =
            realloc (BackupRegisteredWorker,
                     sizeof(BackupWorker *) * BackupWorkerSize);
    }
    BackupRegisteredWorker[BackupWorkerCount++] = worker;
}

static void housesprinkler_state_clear (void) {
    if (BackupInText) {
        switch (BackupOrigin) {
            case BACKUP_ORIGIN_FILE: echttp_parser_free (BackupInText); break;
            case BACKUP_ORIGIN_DEPOT: free (BackupInText); break;
        }
        BackupInText = 0;
    }
    BackupOrigin = 0;
    BackupTokenCount = 0;
}

static const char *housesprinkler_state_new (int origin, char *data) {

    const char *error;

    BackupInText = data;
    BackupOrigin = origin;
    BackupTokenCount = echttp_json_estimate(BackupInText);
    if (BackupTokenCount > BackupTokenAllocated) {
        BackupTokenAllocated = BackupTokenCount+64;
        if (BackupParsed) free (BackupParsed);
        BackupParsed = calloc (BackupTokenAllocated, sizeof(ParserToken));
    }
    error = echttp_json_parse (BackupInText, BackupParsed, &BackupTokenCount);
    if (error) {
        DEBUG ("Backup config parsing error: %s\n", error);
        housesprinkler_state_clear ();
    } else {
        DEBUG ("Planned %d, read %d items of backup config\n", BackupTokenAllocated, BackupTokenCount);
    }
    return error;
}

static int housesprinkler_state_save (int size) {

    int fd = open (BackupFile, O_WRONLY|O_TRUNC|O_CREAT, 0777);
    if (fd < 0) {
        DEBUG ("Cannot open %s\n", BackupFile);
        return 0; // Failure.
    }
    int written = write (fd, BackupOutBuffer, size);
    if (written < 0) {
        DEBUG ("Cannot write to %s\n", BackupFile);
    } else {
        DEBUG ("Wrote %d characters to %s\n", written, BackupFile);
    }
    close(fd);
    return written;
}

static void housesprinkler_state_listener (const char *name, time_t timestamp,
                                           const char *data, int length) {

    houselog_event ("SYSTEM", "BACKUP", "LOAD", "FROM DEPOT %s", name);
    const char *error = housesprinkler_state_new (BACKUP_ORIGIN_DEPOT, strdup(data));
    if (error) {
        houselog_event ("SYSTEM", "BACKUP", "ERROR", "%s", error);
        return;
    }
    // We need to make a copy because we do not control the lifetime of
    // the caller's data buffer.
    //
    if (BackupOutBufferSize < length) {
        if (BackupOutBuffer) free (BackupOutBuffer);
        BackupOutBuffer = strdup (data);
        BackupOutBufferSize = length;
    } else {
        snprintf (BackupOutBuffer, BackupOutBufferSize, "%s", data);
    }
    housesprinkler_state_save (length); // Best effort only, ignore errors.

    int i;
    for (i = 0; i < BackupListenerCount; ++i) {
        BackupRegisteredListener[i] ();
    }
}

const void housesprinkler_state_load (int argc, const char **argv) {

    char *newconfig;

    int i;
    for (i = 1; i < argc; ++i) {
        if (echttp_option_match ("-backup=", argv[i], &BackupFile)) continue;
    }

    housesprinkler_state_clear ();

    const char *name = BackupFile;
    DEBUG ("Loading backup from %s\n", name);
    newconfig = echttp_parser_load (name);
    if (!newconfig) {
        name = FactoryBackupFile;
        DEBUG ("Loading backup from %s\n", name);
        newconfig = echttp_parser_load (name);
        StateDataHasChanged = time(0); // Force creation of the backup file.
    }

    if (newconfig) {
        const char *error;
        houselog_event ("SYSTEM", "BACKUP", "LOAD", "FILE %s", name);
        housesprinkler_state_new (BACKUP_ORIGIN_FILE, newconfig);
    }

    housedepositor_subscribe ("state", "sprinkler.json",
                              housesprinkler_state_listener);
}

void housesprinkler_state_share (int on) {
    ShareStateData = on;
}

const char *housesprinkler_state_get_string (const char *path) {

    int i = echttp_json_search(BackupParsed, path);
    if (i < 0) return 0;
    switch (BackupParsed[i].type) {
        case PARSER_STRING: return BackupParsed[i].value.string;
    }
    return 0;
}

long housesprinkler_state_get (const char *path) {

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

void housesprinkler_state_changed (void) {
    if (!StateDataHasChanged) {
        DEBUG("State data has changed.\n");
        StateDataHasChanged = time(0);
    }
}

static int housesprinkler_state_format (void) {

    int cursor = 0;
    int i;
    if (!BackupOutBuffer) {
       BackupOutBufferSize = 1024;
       BackupOutBuffer = malloc(BackupOutBufferSize);
    }
    DEBUG("Saving backup data to %s\n", BackupFile);
    cursor = snprintf (BackupOutBuffer, BackupOutBufferSize,
                       "{\"host\":\"%s\"", sprinkler_host());
    for (i = 0; i < BackupWorkerCount; ++i) {
        cursor += snprintf (BackupOutBuffer+cursor, BackupOutBufferSize-cursor, ",");
        if (cursor >= BackupOutBufferSize) goto retry;
        cursor += BackupRegisteredWorker[i] (BackupOutBuffer+cursor, BackupOutBufferSize-cursor);
        if (cursor >= BackupOutBufferSize) goto retry;
    }
    cursor += snprintf (BackupOutBuffer+cursor, BackupOutBufferSize-cursor, "}");
    if (cursor >= BackupOutBufferSize) goto retry;

    return cursor;

retry:
    DEBUG ("Backup failed: buffer too small\n");
    houselog_trace (HOUSE_WARNING, "BACKUP",
                    "BUFFER TOO SMALL (NEED %d bytes)", cursor);
    BackupOutBufferSize += 1024;
    free (BackupOutBuffer);
    BackupOutBuffer = malloc(BackupOutBufferSize);
    return housesprinkler_state_format ();
}

void housesprinkler_state_periodic (time_t now) {

    static time_t LastCall = 0;
    if (now == LastCall) return; // Run the logic once per second.
    LastCall = now;

    if (StateDataHasChanged) {
        if (StateDataHasChanged < now - 10) {
            StateDataHasChanged = 0;
            return; // We tried 10 times, no point to try again.
        }
        if (StateDataHasChanged < now) {
            int size = housesprinkler_state_format();
            if (ShareStateData) {
                houselog_event ("SYSTEM", "BACKUP", "SAVE", "TO DEPOT /state/sprinkler.json");
                housedepositor_put ("state", "sprinkler.json", BackupOutBuffer, size);
            }
            if (housesprinkler_state_save (size) == size)
                StateDataHasChanged = 0;
        }
    }
}

