
OBJS= housesprinkler.o \
      housesprinkler_index.o \
      housesprinkler_program.o \
      housesprinkler_zone.o \
      housesprinkler_config.o

ICONS= favicon_1_16x16x4.png

LIBOJS=

SHARE=/usr/local/share/house

# Local build ---------------------------------------------------

all: housesprinkler

clean:
	rm -f *.o *.a housesprinkler

rebuild: clean all

%.o: %.c
	gcc -c -Os -o $@ $<

housesprinkler: $(OBJS)
	gcc -Os -o housesprinkler $(OBJS) -lhouseportal -lechttp -lssl -lcrypto -lrt

# Distribution agnostic file installation -----------------------

dev:

install-files:
	mkdir -p /usr/local/bin
	rm -f /usr/local/bin/housesprinkler
	cp housesprinkler /usr/local/bin
	chown root:root /usr/local/bin/housesprinkler
	chmod 755 /usr/local/bin/housesprinkler
	touch /etc/default/housesprinkler
	mkdir -p /etc/house
	touch /etc/house/sprinkler.json
	mkdir -p /var/lib/house
	mkdir -p $(SHARE)/public/sprinkler
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/sprinkler
	cp public/* $(SHARE)/public/sprinkler
	chown root:root $(SHARE)/public/sprinkler/*
	chmod 644 $(SHARE)/public/sprinkler/*

uninstall-files:
	rm -rf $(SHARE)/public/sprinkler
	rm -f /usr/local/bin/housesprinkler

purge-config:
	rm -rf /etc/house/sprinkler.json /etc/default/housesprinkler

# Distribution agnostic systemd support -------------------------

install-systemd:
	cp systemd.service /lib/systemd/system/housesprinkler.service
	chown root:root /lib/systemd/system/housesprinkler.service
	systemctl daemon-reload
	systemctl enable housesprinkler
	systemctl start housesprinkler

uninstall-systemd:
	if [ -e /etc/init.d/housesprinkler ] ; then systemctl stop housesprinkler ; systemctl disable housesprinkler ; rm -f /etc/init.d/housesprinkler ; fi
	if [ -e /lib/systemd/system/housesprinkler.service ] ; then systemctl stop housesprinkler ; systemctl disable housesprinkler ; systemctl daemon-reload ; rm -f /lib/systemd/system/housesprinkler.service ; fi

stop-systemd: uninstall-systemd

# Debian GNU/Linux install --------------------------------------

install-debian: stop-systemd install-files install-systemd

uninstall-debian: uninstall-systemd uninstall-files

purge-debian: uninstall-debian purge-config

# Void Linux install --------------------------------------------

install-void: install-files

uninstall-void: uninstall-files

purge-void: uninstall-void purge-config

# Default install (Debian GNU/Linux) ----------------------------

install: install-debian

uninstall: uninstall-debian

purge: purge-debian

# Docker install ------------------------------------------------

docker: all
	rm -rf build
	mkdir -p build/usr/local/bin
	mkdir -p build/var/lib/house
	cp housesprinkler build/usr/local/bin
	chmod 755 build/usr/local/bin/housesprinkler
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

