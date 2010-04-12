CURL_CONFIG ?= /usr/bin/curl-config
XML_CONFIG ?= /usr/bin/xml2-config

all:
	gcc rracadm.c -O2 -o rracadm `${CURL_CONFIG} --libs --cflags` `${XML_CONFIG} --cflags --libs` -g

clean:
	rm -f rracadm 
