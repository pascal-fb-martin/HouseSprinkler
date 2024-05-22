# HouseSprinkler
A sprinkler controller in C with a web interface

## Overview
This project is a small sprinkler controller with a web interface.

See the [gallery](https://github.com/pascal-fb-martin/housesprinkler/blob/master/gallery/README.md) for a view of the HouseSprinkler web UI.

This project is one piece of a complete system that also includes the following:
* [echttp](https://github.com/pascal-fb-martin/echttp),
* [houseportal](https://github.com/pascal-fb-martin/houseportal),
* [waterwise](https://github.com/pascal-fb-martin/waterwise) and
* [houserelays](https://github.com/pascal-fb-martin/houserelays).

The primary intend is to create a sprinkler controller that:
* Is accessible from a computer or phone using a web interface, including manual controls for sprinkler test and maintenance.
* Controls the sprinkler valves through the network, to offer access to a distributed network of control computers. Valve can be controlled from multiple points on the network: the valve wiring does not have to be all run to a single point.
* Adjusts watering automatically, based on an online index (Southern California for now) or on a season-based index table (monthly or weekly).
* Splits the watering periods into short pulse separated by pauses, to avoid water poodles and run-off.
* Support control of pumps and power supply (feeds) when a program or zone runs.
* Records a log of activity, so that one may monitor what happened the previous days.
* Can be integrated as part of a suite of applications managed from a central point.

This project replaces and obsoletes the [Sprinkler](https://github.com/pascal-fb-martin/sprinkler) project. This project is a complete rewrite in the spirit of micro-services: it relies on other web services to implement some interfaces. This makes it a more decentralized and flexible setup.

Instead of installing drivers for different types of relay/triac interfaces, the design relies on a generic web API, and the specific interface itself is implemented as a web service. The [houserelays](https://github.com/pascal-fb-martin/houserelays) web service is the first implementation that follows this new design. A benefit is that a new interface can be developped and maintained independently of the HouseSprinkler code base, which is the essence of the web service philosophy. This also makes it possible to support multiple interfaces simultaneously, both geographically distributed and possibly using different hardware interfaces.

The House family of projects provides other control interfaces, typically intended for other types of devices, like TP-Link Kasa or Philips Wiz. These interfaces can be used to control feeds. For example a Kasa plug can be used to control the solenoid's 24 vold power supply, or to turn on and off a water pump.

Instead of implementing multiple weather and watering index interfaces, this project relies on a generic web API, and the specific weather/index interface itself is implemented as a web service. The [waterwise](https://github.com/pascal-fb-martin/waterwise) web service is a minimal implementation that relies on the bewaterwise.com web site maintained by the Metropolitan Water District of Southern California (and is therefore of interest only for those living in the Los Angeles and San Diego areas).

Future plans are:
* Create other web services for weather information and index calculation, starting with an interface to the state of California's CIMIS web site and data.
* Create a separate web service for log storage and archival.
* Create a separate web service for configuration storage.
* Create a modular whole-house dashboard page, to give an overview of all the services on one page.

## Installation

The simplest case is that the sprinkler valves are all controlled from one single computer and the complete set of software needed to make the system work is installed there:
* Install git, openssl (libssl-dev), gpiod (libgpiod-dev).
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Install [waterwise](https://github.com/pascal-fb-martin/waterwise)
* Install [houserelays](https://github.com/pascal-fb-martin/houserelays)
* Clone this repository.
* make rebuild
* sudo make install

However this software was designed to support irrigation valves controlled from several locations, i.e. computers. In that case there are two roles: one computer runs the sprinkler software and two or more computers run the valve control software. Thus there are two installation profiles:

Sprinkler control computer:
* Install git, icoutils, openssl (libssl-dev), gpiod (libgpiod-dev).
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Install [waterwise](https://github.com/pascal-fb-martin/waterwise)
* Clone this repository.
* make rebuild
* sudo make install

Valve control computers:
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Install [houserelays](https://github.com/pascal-fb-martin/houserelays)

It is possible to run the sprinkler software on one of the valve controllers, i.e. use one computer as the main control system and the others as extensions. To do this just perform a full install on the chosen sprinkler controller computer, and the limited valve control install on the others.

All these computers must be on the same subnet (UDP broadcast is involved for discovery).

## Configuration

The HouseSprinkler configuration is defined in a JSON file, by default /etc/house/sprinkler.json. However the proper way to configure is to access the Config page on the sprinkler's web UI. Do not forget to configure [houserelays](https://github.com/pascal-fb-martin/houserelays) first.

The HouseSprinkler configuration is mainly organized in layers:
* Seasons provides a fixed way of managing watering indexes, independent of weather information or availability of watering index provider sites.
* Zones define how long are the pulses and pauses for each zone.
* Programs defines a list of zones to activate, and for how long. It also indicate an (optional) season index table to conform to.
* Schedules define when programs should be activated.

Note that zones can be activated manually from the web UI, bypassing any program configuration, and programs can be activated manually from the web UI, bypassing any schedule rules.

## Program Execution

When a program starts, either based on schedule or manually, zones are activated in an order calculated to maximize the soak time and minimize the elapsed program execution time:
* All zones are activable when the program starts.
* A zone becomes activable again once its soack period has completed.
* Zones are activated one at a time.
* Zone activation is based on which zone was activable the earliest. This typically represents the zone that waited the most after its soak period.
* If multiple zones were activable at the same time (like happens when the program starts), the zone with the longest remaining runtime is activated.

The rationale here is that the zones with the longest runtime will likely execute the highest number of run/soak cycles. These are on the critical path when it comes to the program complete execution, and starting them first will likely reduce the program's elapsed execution time. This criteria is of a lower priority for subsequent activations because increasing the soak time was deemed more important.

Zones are activated only at the start of a minute. This is meant to synchronize with the sampling period of a flow monitoring system, like the [Flume](https://flumewater.com/) device. This way the amount of water consumed by each zone is clearly separated zone by zone. The goal is to calculate the water consumption zone by zone, but also to detect when a zone pipe, or a valve, is broken: alert when the flow is anormally high, of when the water does not flow.

(The integration with Flume is a work-in-progress. This time synchronization makes it easier to visually reconcile zone activations from the event log with the water consumption as reported by the Flume application.)

## Panel

The web interface includes a Panel page (/sprinkler/panel.html) that has no menu and only shows the current sprinkler zones, each as one big button to turn the device on and off. This page is meant for a phone screen, typically a shortcut on the phone's home screen. (Because HousePortal redirects the URL, it is recommended to turn the phone in airplane mode when creating the shortcut from the web browser.)

Note that each zone turns itself off after 60 seconds, to avoid wasting water by mistake: this panel is intended for testing the sprinklers, not for normal irrigation.

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network host` option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

