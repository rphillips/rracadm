CURL_CONFIG ?= /usr/bin/curl-config

all:
	gcc rracadm.c -O2 -o rracadm `${CURL_CONFIG} --libs --cflags` `xml2-config --cflags` -lxml2 -g

clean:
	rm -f rracadm 
