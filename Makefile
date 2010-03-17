all:
	gcc rracadm.c -O2 -o rracadm -lcurl `xml2-config --cflags` -lxml2 -g

clean:
	rm -f rracadm 
