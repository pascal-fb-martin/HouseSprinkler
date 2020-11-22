
OBJS= housesprinkler.o \
      housesprinkler_index.o \
      housesprinkler_program.o \
      housesprinkler_zone.o \
      housesprinkler_config.o

ICONS= favicon_1_16x16x4.png

LIBOJS=

all: housesprinkler

clean:
	rm -f *.o *.a housesprinkler

rebuild: clean all

%.o: %.c
	gcc -c -g -O -o $@ $<

housesprinkler: $(OBJS)
	gcc -g -O -o housesprinkler $(OBJS) -lhouseportal -lechttp -lcrypto -lrt

install:
	if [ -e /etc/init.d/housesprinkler ] ; then systemctl stop housesprinkler ; fi
	mkdir -p /usr/local/bin
	mkdir -p /var/lib/house
	rm -f /usr/local/bin/housesprinkler /etc/init.d/housesprinkler
	cp housesprinkler /usr/local/bin
	cp init.debian /etc/init.d/housesprinkler
	chown root:root /usr/local/bin/housesprinkler /etc/init.d/housesprinkler
	chmod 755 /usr/local/bin/housesprinkler /etc/init.d/housesprinkler
	touch /etc/default/housesprinkler
	mkdir -p /etc/house
	touch /etc/house/sprinkler.json
	mkdir -p /usr/local/share/house/public/sprinkler
	cp public/* /usr/local/share/house/public/sprinkler
	chown root:root /usr/local/share/house/public/sprinkler/*
	chmod 644 /usr/local/share/house/public/sprinkler/*
	icotool -c -o /usr/local/share/house/public/favicon.ico $(ICONS)
	systemctl daemon-reload
	systemctl enable housesprinkler
	systemctl start housesprinkler

uninstall:
	systemctl stop housesprinkler
	systemctl disable housesprinkler
	rm -f /usr/local/bin/housesprinkler /etc/init.d/housesprinkler
	systemctl daemon-reload

purge: uninstall
	rm -rf /etc/house/sprinkler.json /etc/default/housesprinkler

