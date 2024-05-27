# housesprinkler - A simple home web server for sprinkler control
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.

HAPP=housesprinkler
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Local build ---------------------------------------------------

OBJS= housesprinkler.o \
      housesprinkler_index.o \
      housesprinkler_season.o \
      housesprinkler_program.o \
      housesprinkler_schedule.o \
      housesprinkler_control.o \
      housesprinkler_zone.o \
      housesprinkler_feed.o \
      housesprinkler_time.o \
      housesprinkler_config.o

ICONS= favicon_1_16x16x4.png

LIBOJS=

all: housesprinkler

clean:
	rm -f *.o *.a housesprinkler

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housesprinkler: $(OBJS)
	gcc -Os -o housesprinkler $(OBJS) -lhouseportal -lechttp -luuid -lssl -lcrypto -lrt

# Distribution agnostic file installation -----------------------

dev:

install-app:
	mkdir -p $(HROOT)/bin
	rm -f $(HROOT)/bin/housesprinkler
	cp housesprinkler $(HROOT)/bin
	chown root:root $(HROOT)/bin/housesprinkler
	chmod 755 $(HROOT)/bin/housesprinkler
	touch /etc/default/housesprinkler
	mkdir -p /etc/house
	touch /etc/house/sprinkler.json
	mkdir -p /var/lib/house
	mkdir -p $(SHARE)/public/sprinkler
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/sprinkler
	cp public/* $(SHARE)/public/sprinkler
	chown root:root $(SHARE)/public/sprinkler/*
	chmod 644 $(SHARE)/public/sprinkler/*
	rm -f $(SHARE)/public/sprinkler/valves.html
	if [ -e /etc/house/sprinklerbkp.json ] ; then if grep -q useindex /etc/house/sprinklerbkp.json ; then echo yes > /dev/null ; else rm -f /tmp/sprinklerbkp.json ; sed -e 's/}/, "useindex":1}/' < /etc/house/sprinklerbkp.json > /tmp/sprinklerbkp.json ; mv /tmp/sprinklerbkp.json /etc/house/sprinklerbkp.json ; fi ; fi

uninstall-app:
	rm -rf $(SHARE)/public/sprinkler
	rm -f $(HROOT)/bin/housesprinkler

purge-app:

purge-config:
	rm -rf /etc/house/sprinkler.json /etc/default/housesprinkler

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker install ------------------------------------------------

docker: all
	rm -rf build
	mkdir -p build$(HROOT)/bin
	mkdir -p build/var/lib/house
	cp housesprinkler build$(HROOT)/bin
	chmod 755 build$(HROOT)/bin/housesprinkler
	mkdir -p build/etc/house
	touch build/etc/house/sprinkler.json
	mkdir -p build$(SHARE)/public/sprinkler
	chmod 755 build$(SHARE) build$(SHARE)/public build$(SHARE)/public/sprinkler
	cp public/* build$(SHARE)/public/sprinkler
	cp $(SHARE)/public/house.css $(SHARE)/public/event.js build$(SHARE)/public
	icotool -c -o build$(SHARE)/public/favicon.ico $(ICONS)
	chmod 644 build$(SHARE)/public/sprinkler/*
	chmod 644 build$(SHARE)/public/house.css build$(SHARE)/public/event.js
	chmod 644 build$(SHARE)/public/favicon.ico
	cp Dockerfile build
	cd build ; docker build -t housesprinkler .
	rm -rf build

