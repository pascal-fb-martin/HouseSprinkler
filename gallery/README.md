# HouseSprinkler Gallery

This gallery is meant to give an idea of the HouseSprinkler web UI.

The Sprinkler main page acts as a sort of status dashboard for the sprinkler control system:

![HouseSprinkler Main Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/main-page.png)

The top line is a navigation bar that gives access to the HouseSprinkler major pages.

The block on the left (which appears on most pages) provides an overview of the current systen status:
* The name of the controller machine.
* The name of the current program running (if any), IDLE (no program is running), MANUAL (zone was activated manually) or OFF (idle and scheduler turned off).
* The name of the zone currently active.
* The current duration of the rain delay, or NONE if there is no pending rain delay.
* The value (and origin) of the current watering index.

The buttons on the right side are mostly self-explanatory. The Refresh button forces a rediscovery of the control and water index services. This is mostly used when one of these services was started (or restarted) and one does not want to wait for the periodic discovery. Disabling the Index mechanism actually disables all of them, including the season index (see later for the description of seaseason index).

The Zones page allow manual control of individual zones, bypassing programs:

![HouseSprinkler Event Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/zones-page.png)

That page is typically used to test sprinklers or valves.

The Program page allow manual control of individual programs, bypassing schedules:

![HouseSprinkler Event Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/programs-page.png)

That page is typically used to test programs.

The Event page shows a record of the major changes detected by the HouseSprinkler service. Its main purpose is to monitor the execution of the watering schedules as well as help troubleshoot issues with device configuration and control.

![HouseSprinkler Event Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/events-page.png)

The controls page is mostly a maintenance page: it shows the result of the discovery (i.e., which server was detected for each control) as well as the current status of each control (IDLE, ACTIVE, ERROR or UNKNOWN).

![HouseSprinkler Control Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/controls-page.png)

The configuration page is used to configure the zones, feeds, programs, schedules and seasons:

![HouseSprinkler Config Page](https://raw.githubusercontent.com/pascal-fb-martin/housesprinkler/master/gallery/config-page.png)

The list of zones identifies the control points used for irrigation and the length of the pulse and pause values for each (pulse and pause are used to allow the water to penetrate the soil without causing puddles or runoff). Each zone may refer to a feed, independently of each other (see below).

The list of feeds identifies the control points used to turn on power or start pumps when activating zones. Each zone may refer to a feed, and each feed may refer to a next feed. When a zone is activated, all the feeds along the chain are activated.

It is possible to create feed chains that merge, for example a first chain lists feed1, feed2 and feed3 while a second chain lists feed4 and feed3. The "shared" feeds should be listed last, but the order in the chain is not otherwise significant.

The list of programs define which zones are activated, and for how long, for each program. A program may also reference a season, which provide a local, static, monthly watering index. Seasons can be used when no online watering index is available.

The list of schedules define when to activate a specific program. The same program may be activated at various times per week or day, but no activation can take place while the same program is active. Two different programs can be activated simultaneously: the application will silently combine them.

A schedule combines the capability of a weekly or daily schedule. A weekly schedule defines which days of the week the program shall be run. A daily schedule defines how many days beween two runs of the program (like every other day or ever 5 days). Here, the two modes can be defined in combination. A weekly schedule will normally have a blank interval. A daily schedule normally have all days of the week selected, but it is possible to exclude days of the week. For example, a daily schedule might be setup to run the program every 3 days, but not on a Saturday; if the activation falls on a Saturday, excluding this day will cause the program to be run on Sunday (the next selected day). The Begin and End dates make it possible to enable the schedule for a time period only; it is valid to specify only one date and leave the other blank.

The list of seasons provide a local mean to configure watering indexes. One may define multiple seasons to handle various ground coverage situations (for example an area under the shadow of trees may be less sensitive than an area exposed to the sun).

If the season index for one month is 0, then a program that references this season is disabled for this month, even if a live index is available. Otherwise, a season index is only used if no live index service is available. If the index mechanism is disabled, then both the live and season indexes are disabled and all programs run at 100%.

