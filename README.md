# HouseSprinkler
A sprinkler controller in C with a web interface

## Overview
This project is a small sprinkler controller with a web interface. This project depends on the following projects:
* [echttp](https://github.com/pascal-fb-martin/echttp),
* [houseportal](https://github.com/pascal-fb-martin/houseportal),
* [waterwise](https://github.com/pascal-fb-martin/waterwise) and
* [houserelays](https://github.com/pascal-fb-martin/houserelays).

The primary intend is to create a sprinkler controller that:
* Adjust watering automatically, based on an online index (Southern California) or on a season-based index table (monthly or weekly).
* Split the watering periods into short pulse separated by pauses, to avoid poodles and run-off.
* Record a log of activity.
* Accessible from a computer or phone using a web interface, including manual controls for sprinkler test and maintenance.
* Access the sprinkler relays through the network, to offer access to a distributed network of relay computers.
* Is built upon the "house" series of micro-service applications.

These goals are very similar those of the [Sprinkler](https://github.com/pascal-fb-martin/sprinkler) project. This complete redesign was triggered by two specific issues:
* Bringing all the sprinkler wires to a single place is not always easy. Following a home remodeling, I ended with one half of the wires in a separate location. This is handled by having two independent controllers and two programs that alternate the pulse and pause periods in a carefully balanced way.
* Maintaining node.js on Debian, and a Javascript application in the always changing world of Node.js and ECMA standards is more hassle than I want to entertain.

The original Sprinkler application also includes complex features that did not pan out well and are not supported in this project:
* Google calendar based watering programs: this creates more complexity than it is worth, both in coding and configuration.
* Weather based index calculation: this was never completed because it is difficult to find all the data required (especially sun radiation).
* Weather web sites all have their annoyances: NOAA is bulky and bureaucratic, and its data is surprisingly not that rich, while the WeatherUnderground stations keep changing, with uneven data quality.

This project is a complete rewrite of Sprinkler in the spirit of micro-services: it does less, and relies more on other web services to implement the missing features. This makes it a more decentralized and flexible setup.

Instead of installing drivers for different types of relay/triac interfaces, the design relies on a generic web API, and the specific interface itself is implemented as a web service. The [houserelays](https://github.com/pascal-fb-martin/houserelays) web service is the first implementation that follows this new design. A benefit is that a new interface can be created outside of the HouseSprinkler code base, which is the essence of the web service philosophy. This also makes it possible to support multiple interfaces simultaneously, both geographically distributed and possibly using different hardware interfaces.

Instead of implementing multiple weather and watering index interfaces, it relies on a generic web API, and the specific weather/index interface itself is implemented as a web service. The [waterwise](https://github.com/pascal-fb-martin/waterwise) web service is a minimal implementation that relies on the bewaterwise.com web site maintained by the Metropolitan Water District of Southern California (and is therefore of interest only for those in the Los Angeles and San Diego areas).

Future plan is to create other microservices for weather information and index calculation, starting with an interface to the state of California's CIMIS web site and data.

## Installation

* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Install [waterwise](https://github.com/pascal-fb-martin/waterwise)
* Install [houserelays](https://github.com/pascal-fb-martin/houserelays)
* Clone this repository.
* make rebuild
* sudo make install

## Configuration

The HouseSprinkler configuration is defined in a JSON file, by default /etc/house/sprinkler.json. However the proper way to configure is to access the Config page on the sprinkler's web UI.

