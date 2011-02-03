CURL_CONFIG ?= /usr/bin/curl-config
XML_CONFIG ?= /usr/bin/xml2-config
DESTDIR ?= /usr
PREFIX ?= ${DESTDIR}

all:
	gcc rracadm.c -O2 -o rracadm `${CURL_CONFIG} --libs --cflags` `${XML_CONFIG} --cflags --libs` -g

install:
	install -D rracadm ${PREFIX}/bin

clean:
	rm -f rracadm 
