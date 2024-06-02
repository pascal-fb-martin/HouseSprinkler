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
 * housesprinkler_control.c - Interface with the control servers.
 *
 * SYNOPSYS:
 *
 * This module handles detection of, and communication with, the control
 * servers:
 * - Run periodic discoveries to find which server handles each control.
 * - Handle the HTTP control requests (and redirects).
 *
 * Each control is independent of each other: see the zone and feed
 * modules for the application logic that applies to controls.
 *
 * This module remembers which controls are active, so that it does not
 * have to stop every known control on cancel.
 *
 * void housesprinkler_control_reset (void);
 *
 *    Erases the list of known control points.
 *    This function must be called before applying a new configuration.
 *
 * void housesprinkler_control_declare (const char *name, const char *type);
 *
 *    Declare a new control point to be discovered.
 *
 * void housesprinkler_control_event (const char *name, int enable, int once);
 *
 *    Enable or disable activation events for the specified control:
 *    - If enable and once are both true, events are automatically disabled
 *      after one event has been issued.
 *    - If enable is true and once is false, events are enabled until
 *      explicitly disabled.
 *    - If enable is false, once is ignored and events are disabled until
 *      explicitly enabled.
 *
 *    This has no impact on "unusual" events like discovery or stop.
 *
 * void housesprinkler_control_start (const char *name,
 *                                    int pulse, const char *context);
 *
 *    Activate one control for the duration set by pulse. The context is
 *    typically the name of the schedule, or 0 for manual activation.
 *
 * void housesprinkler_control_cancel (const char *name);
 *
 *    Immediately stop a control, or all active controls if name is null.
 *
 * char housesprinkler_control_state (const char *name);
 *
 *    Return the current state of the control.
 *
 * void housesprinkler_control_periodic (time_t now);
 *
 *    The periodic function that detects the control servers.
 *
 * int housesprinkler_control_status (char *buffer, int size);
 *
 *    Return the status of control points in JSON format.
 */

#include <string.h>
#include <stdlib.h>

#include <echttp.h>
#include <echttp_json.h>

#include "houselog.h"
#include "housediscover.h"

#include "housesprinkler.h"
#include "housesprinkler_time.h"
#include "housesprinkler_control.h"

#define DEBUG if (sprinkler_isdebug()) printf

#define MAX_PROVIDER 64

static char *Providers[MAX_PROVIDER];
static int   ProvidersCount = 0;

typedef struct {
    const char *name;
    const char *type;
    char status;
    char event;
    char once;
    time_t deadline;
    char url[256];
} SprinklerControl;

static SprinklerControl *Controls = 0;
static int               ControlsCount = 0;
static int               ControlsSize = 0;

static int ControlsActive = 0;

static SprinklerControl *housesprinkler_control_search (const char *name) {
    int i;
    if (!Controls) return 0;
    for (i = 0; i < ControlsCount; ++i) {
        if (!strcmp (name, Controls[i].name)) return Controls+i;
    }
    return 0;
}

void housesprinkler_control_reset (void) {
    ControlsCount = 0;
}

void housesprinkler_control_declare (const char *name, const char *type) {
    if (!housesprinkler_control_search(name)) {
        if (ControlsCount >= ControlsSize) {
            ControlsSize += 16;
            Controls = realloc (Controls, ControlsSize*sizeof(SprinklerControl));
            if (!Controls) {
                houselog_trace (HOUSE_FAILURE, name, "no more memory");
                ControlsSize = ControlsCount = 0;
                return;
            }
        }
        Controls[ControlsCount].name = name;
        Controls[ControlsCount].type = type;
        Controls[ControlsCount].status = 'u';
        Controls[ControlsCount].event = 1; // enabled.
        Controls[ControlsCount].once = 0; // .. until explicitly disabled.
        Controls[ControlsCount].deadline = 0;
        Controls[ControlsCount].url[0] = 0; // Need to (re)learn.
        ControlsCount += 1;
    }
}

void housesprinkler_control_event (const char *name, int enable, int once) {

    SprinklerControl *control = housesprinkler_control_search(name);
    if (control) {
        control->event = enable;
        control->once = once;
    }
}

static void housesprinkler_control_result
               (void *origin, int status, char *data, int length) {

   SprinklerControl *control = (SprinklerControl *)origin;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housesprinkler_control_result, origin);
       return;
   }

   if (status != 200) {
       if (control->status != 'e')
           houselog_trace (HOUSE_FAILURE, control->name, "HTTP code %d", status);
       control->status  = 'e';
       control->deadline  = 0;
   }
}

int housesprinkler_control_start (const char *name,
                                  int pulse, const char *context) {
    time_t now = time(0);

    SprinklerControl *control = housesprinkler_control_search (name);
    if (!control) {
        houselog_event ("CONTROL", name, "UNKNOWN", "");
        return 0;
    }
    DEBUG ("%ld: Start %s %s for %d seconds\n", now, control->type, name, pulse);
    if (control->url[0]) {
        if (!context || context[0] == 0) context = "MANUAL";
        if (control->event) {
            houselog_event (control->type, name, "ACTIVATED",
                            "FOR %s USING %s (%s)",
                            housesprinkler_time_period_printable(pulse),
                            control->url, context);
            if (control->once) {
                control->event = 0;
                control->once = 0;
            }
        }
        static char url[256];
        static char cause[256];
        int l = snprintf (cause, sizeof(cause), "%s", "SPRINKLER%20");
        echttp_escape (context, cause+l, sizeof(cause)-l);
        snprintf (url, sizeof(url),
                  "%s/set?point=%s&state=on&pulse=%d&cause=%s",
                  control->url, name, pulse, cause);
        const char *error = echttp_client ("GET", url);
        if (error) {
            houselog_trace (HOUSE_FAILURE, name, "cannot create socket for %s, %s", url, error);
            return 0;
        }
        DEBUG ("GET %s\n", url);
        echttp_submit (0, 0, housesprinkler_control_result, (void *)control);
        control->deadline = now + pulse;
        control->status = 'a';
        ControlsActive = 1;
        return 1;
    }
    return 0;
}

static void housesprinkler_control_stop (SprinklerControl *control) {
    if (control->url[0]) {
        static char url[256];
        snprintf (url, sizeof(url),
                  "%s/set?point=%s&state=off", control->url, control->name);
        const char *error = echttp_client ("GET", url);
        if (error) {
            houselog_trace (HOUSE_FAILURE, control->name, "cannot create socket for %s, %s", url, error);
            return;
        }
        DEBUG ("GET %s\n", url);
        echttp_submit (0, 0, housesprinkler_control_result, (void *)control);
        control->status  = 'i';
    }
}

void housesprinkler_control_cancel (const char *name) {

    int i;
    time_t now = time(0);

    if (name) {
        SprinklerControl *control = housesprinkler_control_search (name);
        if (control) {
            houselog_event (control->type, name, "CANCEL", "MANUAL");
            housesprinkler_control_stop (control);
            control->deadline = 0;
        }
        return;
    }
    DEBUG ("%ld: Cancel all zones and feeds\n", now);
    for (i = 0; i < ControlsCount; ++i) {
        if (Controls[i].deadline) {
            housesprinkler_control_stop ( Controls + i);
            Controls[i].deadline = 0;
        }
    }
    ControlsActive = 0;
}

char housesprinkler_control_state (const char *name) {
    SprinklerControl *control = housesprinkler_control_search (name);
    if (!control) return 'e';
    return control->status;
}

static void housesprinkler_control_discovered
               (void *origin, int status, char *data, int length) {

   const char *provider = (const char *) origin;
   ParserToken tokens[100];
   int  innerlist[100];
   char path[256];
   int  count = 100;
   int  i;

   status = echttp_redirected("GET");
   if (!status) {
       echttp_submit (0, 0, housesprinkler_control_discovered, origin);
       return;
   }

   if (status != 200) {
       houselog_trace (HOUSE_FAILURE, provider, "HTTP error %d", status);
       return;
   }

   // Analyze the answer and retrieve the control points matching ours.
   const char *error = echttp_json_parse (data, tokens, &count);
   if (error) {
       houselog_trace
           (HOUSE_FAILURE, provider, "JSON syntax error, %s", error);
       return;
   }
   if (count <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no data");
       return;
   }

   int controls = echttp_json_search (tokens, ".control.status");
   if (controls <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "no control data");
       return;
   }

   int n = tokens[controls].length;
   if (n <= 0) {
       houselog_trace (HOUSE_FAILURE, provider, "empty control data");
       return;
   }

   error = echttp_json_enumerate (tokens+controls, innerlist);
   if (error) {
       houselog_trace (HOUSE_FAILURE, path, "%s", error);
       return;
   }

   for (i = 0; i < n; ++i) {
       ParserToken *inner = tokens + controls + innerlist[i];
       SprinklerControl *control = housesprinkler_control_search (inner->key);
       if (!control) continue;
       if (strcmp (control->url, provider)) {
           snprintf (control->url, sizeof(control->url), provider);
           control->status = 'i';
           houselog_event (control->type, control->name, "ROUTE",
                           "TO %s", control->url);
       }
   }
}

static void housesprinkler_control_scan_server
                (const char *service, void *context, const char *provider) {

    char url[256];

    Providers[ProvidersCount++] = strdup(provider); // Keep the string.

    snprintf (url, sizeof(url), "%s/status", provider);

    DEBUG ("Attempting discovery at %s\n", url);
    const char *error = echttp_client ("GET", url);
    if (error) {
        houselog_trace (HOUSE_FAILURE, provider, "%s", error);
        return;
    }
    echttp_submit (0, 0, housesprinkler_control_discovered, (void *)provider);
}

static void housesprinkler_control_discover (time_t now) {

    static time_t latestdiscovery = 0;

    if (!now) { // This is a manual reset (force a discovery refresh)
        latestdiscovery = 0;
        return;
    }

    // If any new service was detected, force a scan now.
    //
    if ((latestdiscovery > 0) &&
        housediscover_changed ("control", latestdiscovery)) {
        latestdiscovery = 0;
    }

    // Even if nothing new was detected, still scan every minute, in case
    // the configuration of a service was changed.
    //
    if (now <= latestdiscovery + 60) return;
    latestdiscovery = now;

    // Rebuild the list of control servers, and then launch a discovery
    // refresh. This way we don't walk a stale cache while doing discovery.
    //
    DEBUG ("Reset providers cache\n");
    int i;
    for (i = 0; i < ProvidersCount; ++i) {
        if (Providers[i]) free(Providers[i]);
        Providers[i] = 0;
    }
    ProvidersCount = 0;
    DEBUG ("Proceeding with discovery\n");
    housediscovered ("control", 0, housesprinkler_control_scan_server);
}

void housesprinkler_control_periodic (time_t now) {

    int i;
    if (ControlsCount <= 0) return;

    if (ControlsActive) {
        ControlsActive = 0;
        for (i = 0; i < ControlsCount; ++i) {
            if (Controls[i].deadline) {
                if (Controls[i].deadline < now) {
                    // No request: it automatically stops on end of pulse.
                    Controls[i].deadline = 0;
                    Controls[i].status  = 'i';
                } else {
                    ControlsActive = 1;
                }
            }
        }
    }
    housesprinkler_control_discover (now);
}

int housesprinkler_control_status (char *buffer, int size) {

    int i;
    int cursor = 0;
    const char *prefix = "";

    cursor = snprintf (buffer, size, "\"servers\":[");
    if (cursor >= size) goto overflow;

    for (i = 0; i < ProvidersCount; ++i) {
        cursor += snprintf (buffer+cursor, size-cursor,
                            "%s\"%s\"", prefix, Providers[i]);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }
    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    cursor += snprintf (buffer+cursor, size-cursor, ",\"controls\":[");
    if (cursor >= size) goto overflow;
    prefix = "";

    time_t now = time(0);

    for (i = 0; i < ControlsCount; ++i) {
        int remaining =
            (Controls[i].status == 'a')?(int)(Controls[i].deadline - now):0;
        cursor += snprintf (buffer+cursor, size-cursor, "%s[\"%s\",\"%s\",\"%c\",\"%s\",%d]",
                            prefix, Controls[i].name, Controls[i].type, Controls[i].status, Controls[i].url, remaining);
        if (cursor >= size) goto overflow;
        prefix = ",";
    }

    cursor += snprintf (buffer+cursor, size-cursor, "]");
    if (cursor >= size) goto overflow;

    return cursor;

overflow:
    houselog_trace (HOUSE_FAILURE, "BUFFER", "overflow");
    buffer[0] = 0;
    return 0;
}
