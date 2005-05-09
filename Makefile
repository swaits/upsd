upsd: upsd.c
	cc -Os upsd.c -o upsd

install: upsd
	install -s -o root -g wheel -m 500 upsd /usr/local/sbin/upsd

