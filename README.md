# HouseSprinkler
A sprinkler controller in C with a web interface

## Overview
This project is a small sprinkler controller with a web interface.

See the [gallery](https://github.com/pascal-fb-martin/housesprinkler/blob/master/gallery/README.md) for a view of the HouseSpinkler web UI.

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
* Records a log of activity, so that one may monitor what happened the previous days.
* Can be integrated as part of a suite of applications managed from a central point.

This project replaces and obsoletes the [Sprinkler](https://github.com/pascal-fb-martin/sprinkler) project. This project is a complete rewrite in the spirit of micro-services: it relies on other web services to implement some interfaces. This makes it a more decentralized and flexible setup.

Instead of installing drivers for different types of relay/triac interfaces, the design relies on a generic web API, and the specific interface itself is implemented as a web service. The [houserelays](https://github.com/pascal-fb-martin/houserelays) web service is the first implementation that follows this new design. A benefit is that a new interface can be developped and maintained independently of the HouseSprinkler code base, which is the essence of the web service philosophy. This also makes it possible to support multiple interfaces simultaneously, both geographically distributed and possibly using different hardware interfaces.

Instead of implementing multiple weather and watering index interfaces, it relies on a generic web API, and the specific weather/index interface itself is implemented as a web service. The [waterwise](https://github.com/pascal-fb-martin/waterwise) web service is a minimal implementation that relies on the bewaterwise.com web site maintained by the Metropolitan Water District of Southern California (and is therefore of interest only for those living in the Los Angeles and San Diego areas).

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

## Panel

The web interface includes a Panel page (/sprinkler/panel.html) that has no menu and only shows the current sprinkler zones, each as one big button to turn the device on and off. This page is meant for a phone screen, typically a shortcut on the phone's home screen. (Because HousePortal redirects the URL, it is recommended to turn the phone in airplane mode when creating the shortcut from the web browser.)

Note that each zone turns itself off after 60 seconds, to avoid wasting water by mistake: this panel is intended for testing the sprinklers, not for normal irrigation.

## Docker

The project supports a Docker container build, which was tested on an ARM board running Debian. To make it work, all the house containers should be run in host network mode (`--network host` option). This is because of the way [houseportal](https://github.com/pascal-fb-martin/houseportal) manages access to each service: using dynamically assigned ports does not mesh well with Docker's port mapping.

