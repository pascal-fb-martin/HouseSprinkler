# HouseSprinkler

A sprinkler controller in C with a web interface

## Overview

This project is a small sprinkler controller with a web interface.

See the [gallery](https://github.com/pascal-fb-martin/housesprinkler/blob/master/gallery/README.md) for a view of the HouseSprinkler web UI.

This project is one piece of a complete system that also includes the following:

- [echttp](https://github.com/pascal-fb-martin/echttp),

- [houseportal](https://github.com/pascal-fb-martin/houseportal) and

- [houserelays](https://github.com/pascal-fb-martin/houserelays).

The following dependencies are optional, but highly recommended:

- [housedepot](https://github.com/pascal-fb-martin/housedepot) (A centralized configuration management service.)

- [housesaga](https://github.com/pascal-fb-martin/housesaga) (A log consolidation and archival service.)

- [housecimis](https://github.com/pascal-fb-martin/housecimis) (A water index service using data from the state of California CIMIS web site.),

- [waterwise](https://github.com/pascal-fb-martin/waterwise) (A water index service using the bewaterwise.com service of the Metropolitan Water District of Southern California.)

(If housedepot was not installed, you must add the --use-local-storage command line option when launching HouseSprinkler.)

The primary intent of this project is to create a sprinkler controller that:

- Is accessible from a computer or phone using a web interface, including manual controls for sprinkler test and maintenance.

- Controls the sprinkler valves through the network, to offer access to a distributed network of control computers. Valve can be controlled from multiple points on the network: the valve wiring does not have to be all run to a single point.

- Adjusts watering automatically, based on an online index (Southern California for now) or on a season-based index table (monthly or weekly).

- Splits the watering periods into short pulse separated by pauses, to avoid water poodles and run-off, facilitating water absorbtion.

- Support control of pumps and power supply (feeds) when a program or zone runs.

- Records a log of activity, so that one may monitor what happened on the previous days.

- Can be integrated as part of a suite of applications managed from a central point.

Instead of installing drivers for different types of relay/triac interfaces, the design relies on a generic control web API, and the specific interfaces themselves are implemented as web services. The [houserelays](https://github.com/pascal-fb-martin/houserelays) web service is the first implementation that follows this new design. A benefit is that a new interface can be developped, maintained and deployed independently of the HouseSprinkler code base, which is the essence of the microservice philosophy. This also makes it possible to support multiple interfaces simultaneously, both geographically distributed and possibly using different hardware interfaces.

The House family of projects provides other implementations of the control web API, typically intended for other types of devices, like TP-Link Kasa or Philips Wiz. These interfaces can be used to control feeds. For example a Kasa plug can be used to control the solenoid's 24 vold power supply, or to turn on and off a water pump.

Instead of implementing multiple weather and watering index interfaces, this project relies on a generic waterindex web API, and the specific weather/index interfaces themselves are implemented as web services. The [waterwise](https://github.com/pascal-fb-martin/waterwise) web service is a minimal implementation that relies on the bewaterwise.com web site maintained by the Metropolitan Water District of Southern California (and is therefore of interest only for those living in the Los Angeles and San Diego areas).

## Installation

The simplest case is that the sprinkler valves are all controlled from one single computer and the complete set of software needed to make the system work is installed there:

- Install git, openssl (libssl-dev), gpiod (libgpiod-dev).

- Install [echttp](https://github.com/pascal-fb-martin/echttp)

- Install [houseportal](https://github.com/pascal-fb-martin/houseportal)

- Install [housesaga](https://github.com/pascal-fb-martin/housesaga)

- Install [housedepot](https://github.com/pascal-fb-martin/housedepot)

- Install [waterwise](https://github.com/pascal-fb-martin/waterwise)

- Install [houserelays](https://github.com/pascal-fb-martin/houserelays)

- Clone this repository.

- make rebuild

- sudo make install

However this software was designed to support irrigation valves controlled from several locations, i.e. several computers. In that case there are two roles: one computer runs the sprinkler software and two or more computers run the valve control software. Thus there are two installation profiles:

Sprinkler control computer:

- Install git, icoutils, openssl (libssl-dev), gpiod (libgpiod-dev).

- Install [echttp](https://github.com/pascal-fb-martin/echttp)

- Install [houseportal](https://github.com/pascal-fb-martin/houseportal)

- Install [housesaga](https://github.com/pascal-fb-martin/housesaga)

- Install [housedepot](https://github.com/pascal-fb-martin/housedepot)

- Install [waterwise](https://github.com/pascal-fb-martin/waterwise) when in the Los Angeles area.

- Install [HouseCIMIS](https://github.com/pascal-fb-martin/housecimis) when located almost anywhere in California.

- Clone this repository.

- make rebuild

- sudo make install

Valve control computers:

- Install [echttp](https://github.com/pascal-fb-martin/echttp)

- Install [houseportal](https://github.com/pascal-fb-martin/houseportal)

- Install [houserelays](https://github.com/pascal-fb-martin/houserelays)

It is possible to run the sprinkler software on one of the valve controllers, i.e. use one computer as the main control system and the others as extensions. To do this just perform a full install on the chosen sprinkler controller computer, and the limited valve control install on the others.

All these computers must be on the same subnet (UDP broadcast is involved for discovery).

## Configuration

The proper way to configure HouseSprinkler is to access the Config page on the web UI. Do not forget to configure [houserelays](https://github.com/pascal-fb-martin/houserelays) first.

The HouseSprinkler configuration is mainly organized in layers:

- Seasons provides a fixed way of managing watering indexes, independent of weather information or availability of watering index provider sites. Which watering index is used depend on their priorities (higher is better).

> [!NOTE]
> The default configuration includes a high priority season named `Full` than can be used to force a fully static watering schedule, independent of the online watering indexes. This allows static schedules to coexist with others that are automatically adjusted.

- Intervals provides a way to adjust schedule intervals based on the current watering index value (regardless of its origin). See each schedule's Interval field.

> [!WARNING]
> When a program is launched by a schedule using an interval that varies based on the watering index value, then the watering durations are run at 100% regardless of the watering index. This is because you cannot apply the watering index twice: either it is used to modulate the interval or the durations, not both.

- Zones define how long are the pulses and pauses for each zone.

- Programs defines a list of zones to activate, and for how long. It also indicate an (optional) season index table to conform to.

- Schedules define which and when programs should be activated: time of day, days of the week and interval.

Note that zones can be activated manually from the web UI, bypassing any program configuration, and programs can be activated manually from the web UI, bypassing any schedule rules.

The HouseSprinkler configuration is defined in a JSON file, stored by default as home/sprinkler.json on the HouseDepot service. If local storage is enabled, the file is also locally available in /etc/house/sprinkler.json.

If the `-group=<name>` option is used, the name of the group is used as the path in HouseDepot, replacing the default `home`.

It is possible to specify an alternate HouseSprinkler configuration file using the `-config=<file>` option. When this is used, the HouseDepot interface is disabled and HouseSprinkler will only use the specified config file. This is mostly used for testing, or when HouseDepot was not installed.

Note that this service can receive online watering index from watering index services. Thoses services are automatically detected: to configure the system for a specific Internet watering index provider only involves installing the service for that provider. It is possible to run different watering index services simultaneously provided that they use different index priorities. A low priority index service can be useful as a backup when the higher priority index service fails. It is also possible to run the same watering index service on multiple machines, providing redundancy to protect against a computer failure.

## Watering Index Concepts

The goal of irrigation is to replace the water lost because of the plant's evapotranspiration, i.e. water lost due to ground evaporation and the plant's own transpiration. There are multiple formula used to estimate how much water is lost every hour, based on the Sun radiations, temperature, humidity, etc. That calculation depends on the type of plants (some plants transpirate more than others). To simplify calculations, the evapotranspiration is calculated for grass (named `et0`), and then relative coefficients are used for other plants.

Professionals use the estimated evapotranspiration value to determine the exact amount of water to provide through irrigation. That system is quite complicated for us city dwellers who have backyards populated with different types of plants. This is where the watering index comes in.

> [!NOTE]
> Some high end controllers do use the evapotranspiration value directly. The user must indicate a type of plant for each zone, and the controller does the rest. This irrigation controller does not work that way.

A watering index provider selects a summer et0 value as a reference, and calculates a watering index value as a ratio compared to this reference. For example, if the reference et0 is 10 and the current et0 is 3, then the watering index is 30%, i.e. the plants needs 30% of the amount of water compared to the summer reference. A daily, weekly or monthly average of the index can then be calculated. A watering index value can be above 100 if a summer is hotter or drier than the reference.

All it takes now is to program watering times on the irrigation controller that keep the plants healthy during summer, and then apply the watering index. Many cheap controllers support setting the watering index manually, while intelligent controllers can fetch the index automatically from Internet sites.

The HouseSprinkler program is an irrigation controller that can fetch a watering index from various Internet sites, or else uses a static seasonal adjustment from its local configuration. The goal is to apply a watering index without requiring the end user to manually enter an index value every day or week.

This controller does not implement any Internet watering index provider API itself. This job is outsourced to separate services, like `Waterwise` or `HouseCIMIS`, which implement the API for their respective provider and provide the result in an uniform format. Adding a new watering index provider is simply to implement and run a new watering index service.

There are two ways to adjust the amount of water provided:

- Adjust the duration of each watering.
- Adjust how frequently the watering is activated.

The first type of adjustment is the most common. Some gardeners do not recommend it: their point is that a short watering does not allow the water to penetrate deep in the soil, plants roots do not grow deep and more water is lost to evaporation. They recommend adjusting the watering interval instead, for example twice a week in summer, once a week in the fall and every other week in winter. Larger intervals also starve new weed growths of the shallow moisture they need.

This irrigation controller allows selecting either mode on a per schedule basis:

- If the schedule references an interval table, the schedule's interval will be estimated based on the current watering index for the launched program and that named interval table.
- Otherwise the program will uses its current watering index to adjust the watering durations.

Note that which watering index is used for each program is the highest priority index among the online indexes available and the static seasonal index referenced by the program.

## Watering Program Execution

When a program starts, either based on schedule or manually, zones are activated in an order calculated to maximize the soak time and minimize the elapsed program execution time:

- All zones are activable when the program starts.

- A zone becomes activable again once its soack period has completed.

- Zones are activated one at a time.

- Zone activation is based on which zone was activable the earliest. This typically represents the zone that waited the most after its soak period.

- If multiple zones were activable at the same time (for example when the program starts), the zones are activated in an order based on their remaining runtimes, where the zone with the longest remaining runtime is activated first.

The rationale here is that the zones with the longest runtime will likely execute the highest number of run/soak cycles. These are on the critical path when it comes to the program complete execution, and starting them first will likely reduce the program's elapsed execution time. This criteria is of a lower priority for subsequent activations because increasing the soak time was deemed more important.

Zones are activated only at the start of a minute. This is meant to synchronize with the sampling period of a flow monitoring system, like the [Flume](https://flumewater.com/) device. This way the amount of water consumed by each zone is clearly separated zone by zone. The goal is to calculate the water consumption zone by zone, but also to detect when a zone pipe, or a valve, is broken: alert when the flow is anormally high, of when the water does not flow.

(The integration with Flume is a work-in-progress. This time synchronization makes it easier to visually reconcile zone activations from the event log with the water consumption as reported by the Flume application.)

## Panel

The web interface includes a Panel page (/sprinkler/panel.html) that has no menu and only shows the current sprinkler zones, each as one big button to turn the device on and off. This page is meant for a phone screen, typically a shortcut on the phone's home screen. (Because HousePortal redirects the URL, it is recommended to turn the phone in airplane mode when creating the shortcut from the web browser.)

Note that each zone turns itself off after 60 seconds, to avoid wasting water by mistake: this panel is intended for testing the sprinklers, not for normal irrigation.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network host` option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

