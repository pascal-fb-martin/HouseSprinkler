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
 * housesprinkler.c - Main loop of the housesprinkler program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>

#include "housesprinkler.h"

#include "echttp_cors.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "houselog.h"
#include "housediscover.h"

#include "housesprinkler_config.h"
#include "housesprinkler_index.h"
#include "housesprinkler_feed.h"
#include "housesprinkler_zone.h"
#include "housesprinkler_control.h"
#include "housesprinkler_season.h"
#include "housesprinkler_program.h"
#include "housesprinkler_schedule.h"

// Rain delay in 1 day increment:
#define RAINDELAYINTERVAL 86400

static int use_houseportal = 0;
static char hostname[128];

static int SprinklerDebug = 0;

static int SprinklerSimSpeed = 0;
static int SprinklerSimDelta = 0;
static time_t SprinklerSimStart = 0;

time_t sprinkler_schedulingtime (time_t now) {
    if (!SprinklerSimStart) return now;
    now += ((now - SprinklerSimStart) * SprinklerSimSpeed) + SprinklerSimDelta;
    // The scheduling logic is synchronized on the start of each minute.
    // This simulated time must match the beginning of each minute, or else
    // nothing will be started. (We have already enforced that the speed
    // must be a denominator of 60.)
    return now - (now % SprinklerSimSpeed);
}

static void sprinkler_reset (void) {
    housesprinkler_control_periodic (0);
    housesprinkler_index_periodic (0);
    housediscover (0);
}

static void sprinkler_refresh (void) {
    housesprinkler_control_reset ();
    housesprinkler_zone_refresh ();
    housesprinkler_index_refresh ();
    housesprinkler_feed_refresh ();
    housesprinkler_season_refresh ();
    housesprinkler_program_refresh ();
    housesprinkler_schedule_refresh ();
}

static const char *sprinkler_config (const char *method, const char *uri,
                                     const char *data, int length) {

    if (strcmp(method, "POST") == 0) {
       const char *error = housesprinkler_config_save (data);
       if (error) {
           echttp_error (500, error);
           return "";
       }
       sprinkler_refresh ();
       sprinkler_reset();
    } else if (strcmp(method, "GET") == 0) {
       int fd = housesprinkler_config_file();
       echttp_transfer (fd, housesprinkler_config_size());
    }
    echttp_content_type_json ();
    return "";
}

static const char *sprinkler_status (const char *method, const char *uri,
                                     const char *data, int length) {
    static char buffer[65537];
    int cursor;

    cursor = snprintf (buffer, sizeof(buffer),
                       "{\"host\":\"%s\",\"proxy\":\"%s\",\"timestamp\":%ld,\"sprinkler\":{\"zone\":{",
              hostname, houseportal_server(), time(0));
    cursor += housesprinkler_zone_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor,sizeof(buffer)-cursor, "},\"program\":{");
    cursor += housesprinkler_program_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor,sizeof(buffer)-cursor, "},\"schedule\":{");
    cursor += housesprinkler_schedule_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor,sizeof(buffer)-cursor, "},\"control\":{");
    cursor += housesprinkler_control_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor,sizeof(buffer)-cursor, "},\"index\":{");
    cursor += housesprinkler_index_status (buffer+cursor, sizeof(buffer)-cursor);
    cursor += snprintf (buffer+cursor,sizeof(buffer)-cursor, "}}}");

    echttp_content_type_json ();
    return buffer;
}

static const char *sprinkler_raindelay (const char *method, const char *uri,
                                        const char *data, int length) {
    static char buffer[65537];
    int duration;

    const char *amount = echttp_parameter_get ("amount");
    if (amount) duration = atoi(amount);
    else        duration = RAINDELAYINTERVAL;

    housesprinkler_schedule_set_rain (duration);
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_rain (const char *method, const char *uri,
                                   const char *data, int length) {
    static char buffer[65537];

    const char *active = echttp_parameter_get ("active");
    if (!active) active = "true";

    housesprinkler_schedule_rain (strcmp ("true", active) == 0);
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_index (const char *method, const char *uri,
                                    const char *data, int length) {
    static char buffer[65537];

    const char *active = echttp_parameter_get ("active");
    if (!active) active = "true";

    housesprinkler_program_index (strcmp ("true", active) == 0);
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_rescan (const char *method, const char *uri,
                                      const char *data, int length) {

    sprinkler_reset ();
    return sprinkler_status (method, uri, data, length);;
}

static const char *sprinkler_onoff (const char *method, const char *uri,
                                    const char *data, int length) {

    housesprinkler_schedule_switch ();
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_program_on (const char *method, const char *uri,
                                         const char *data, int length) {

    const char *program = echttp_parameter_get ("name");
    if (program) {
        housesprinkler_program_start_manual (program);
    }
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_zone_on (const char *method, const char *uri,
                                      const char *data, int length) {

    const char *zone = echttp_parameter_get ("name");
    const char *runtime = echttp_parameter_get ("pulse");
    if (zone) {
        housesprinkler_zone_activate (zone, runtime?atoi(runtime):30, 0);
    }
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_zone_off (const char *method, const char *uri,
                                       const char *data, int length) {

    housesprinkler_zone_stop ();
    housesprinkler_control_cancel (0);
    return sprinkler_status (method, uri, data, length);
}

static const char *sprinkler_weather (const char *method, const char *uri,
                                     const char *data, int length) {
    echttp_content_type_json ();
    return "";
}

static const char *sprinkler_weatheron (const char *method, const char *uri,
                                        const char *data, int length) {
    static char buffer[65537];

    echttp_content_type_json ();
    return "";
}

static const char *sprinkler_weatheroff (const char *method, const char *uri,
                                         const char *data, int length) {
    static char buffer[65537];

    echttp_content_type_json ();
    return "";
}

static void hs_background (int fd, int mode) {

    static time_t DelayConfigDiscovery = 0;
    static time_t LastCall = 0;
    static time_t LastRenewal = 0;
    time_t now = time(0);

    if (now == LastCall) return;
    LastCall = now;

    if (use_houseportal) {
        static const char *path[] = {"sprinkler:/sprinkler"};
        if (now >= LastRenewal + 60) {
            if (LastRenewal > 0)
                houseportal_renew();
            else
                houseportal_register (echttp_port(4), path, 1);
            LastRenewal = now;
        }
    }
    // Do not try to discover other service immediately: wait for two seconds
    // after the first request to the portal. No need to schedule any watering
    // until then, either.
    if (!DelayConfigDiscovery) DelayConfigDiscovery = now + 2;
    if (now >= DelayConfigDiscovery) {
        housesprinkler_control_periodic(now);
        housesprinkler_index_periodic (now);
        housesprinkler_zone_periodic(sprinkler_schedulingtime(now));
        housesprinkler_program_periodic(sprinkler_schedulingtime(now));
        housesprinkler_schedule_periodic(sprinkler_schedulingtime(now));
    }
    houselog_background (now);
    housediscover (now);
    housesprinkler_config_periodic();
}

static void sprinkler_protect (const char *method, const char *uri) {
    if (echttp_cors_protect(method, uri)) {
        houselog_event (method, uri, "BLOCKED", "%s: %s",
                        echttp_attribute_get("Origin"), echttp_reason());
    }
}

int sprinkler_isdebug (void) {
    return SprinklerDebug;
}

static void sprinkler_initialize (int argc, const char **argv) {
    int i;
    const char *sim = 0;
    for (i = 0; i < argc; ++i) {
        if (echttp_option_present ("-debug", argv[i])) {
            SprinklerDebug = 1;
        } else if (echttp_option_match ("-sim-speed=", argv[i], &sim)) {
            SprinklerSimSpeed = atoi(sim);
            if (SprinklerSimSpeed > 60) {
                // Not more than a minute per second.
                SprinklerSimSpeed = 60;
            } else {
                // 60 must be a multiple of SprinklerSimSpeed.
                while (60 % SprinklerSimSpeed) SprinklerSimSpeed -= 1;
            }
            if (sprinkler_isdebug()) printf ("Running at x%d speed\n", SprinklerSimSpeed);
        } else if (echttp_option_match ("-sim-delta=", argv[i], &sim)) {
            SprinklerSimDelta = atoi(sim);
            if (*sim == '-') sim += 1;
            while (isdigit(*sim)) sim += 1;
            switch (*sim) {
                case 'd': SprinklerSimDelta *= 86400; break;
                case 'h': SprinklerSimDelta *= 3600; break;
                case 'm': SprinklerSimDelta *= 60; break;
            }
        }
    }
    if (SprinklerSimSpeed || SprinklerSimDelta) SprinklerSimStart = time(0);
}

int main (int argc, const char **argv) {

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    gethostname (hostname, sizeof(hostname));

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        houseportal_initialize (argc, argv);
        use_houseportal = 1;
    }
    sprinkler_initialize (argc, argv);
    houselog_initialize ("sprinkler", argc, argv);

    const char *error = housesprinkler_config_load (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, housesprinkler_config_name(), "%s", error);
    }
    sprinkler_refresh ();

    echttp_cors_allow_method("GET");
    echttp_protect (0, sprinkler_protect);

    echttp_route_uri ("/sprinkler/config", sprinkler_config);
    echttp_route_uri ("/sprinkler/status", sprinkler_status);
    echttp_route_uri ("/sprinkler/raindelay", sprinkler_raindelay);
    echttp_route_uri ("/sprinkler/rain", sprinkler_rain);
    echttp_route_uri ("/sprinkler/index", sprinkler_index);
    echttp_route_uri ("/sprinkler/refresh", sprinkler_rescan);

    echttp_route_uri ("/sprinkler/program/on", sprinkler_program_on);
    echttp_route_uri ("/sprinkler/zone/on",    sprinkler_zone_on);
    echttp_route_uri ("/sprinkler/zone/off",   sprinkler_zone_off);
    echttp_route_uri ("/sprinkler/onoff",      sprinkler_onoff);

    echttp_route_uri ("/sprinkler/weather/on",  sprinkler_weatheron);
    echttp_route_uri ("/sprinkler/weather/off", sprinkler_weatheroff);
    echttp_route_uri ("/sprinkler/weather",     sprinkler_weather);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&hs_background);

    housesprinkler_index_register (housesprinkler_program_set_index);
    housediscover_initialize (argc, argv);

    houselog_event ("SERVICE", "sprinkler", "STARTED", "ON %s", houselog_host());
    echttp_loop();
}

