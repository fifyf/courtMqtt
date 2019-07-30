all:
	gcc mosquitto.c -lmosquitto -o courtControl
install:
	cp -f courtControl /usr/sbin/courtControl
clean:
	rm -f courtControl
