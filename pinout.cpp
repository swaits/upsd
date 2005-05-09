#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <syslog.h>

#define POLL_INTERVAL (1)

int fd = -1;

// Main program
int main(int argc, char **argv)
{
	int lastflags = 0;
	bool cts,cd,dtr;

	// check parameters
	if (argc < 2) 
	{
		fprintf(stderr, "Usage: pinout <device>\n");
		exit(1);
	}

	// open serial port
	if ((fd = open(argv[1], O_RDONLY | O_NDELAY)) < 0) 
	{
		fprintf(stderr, "pinout: %s: %s\n", argv[1], sys_errlist[errno]);
		exit(1);
	}

	// Main loop
	while (1) 
	{
		int flags;

		// Get the current line bits
		ioctl(fd, TIOCMGET, &flags);
		flags &= ~TIOCM_DTR;
		ioctl(fd, TIOCMSET, &flags);

		// set state
		cts = (flags & TIOCM_CTS ? true : false);
		cd  = (flags & TIOCM_CD  ? true : false);
		dtr = (flags & TIOCM_DTR ? true : false);

		if ( flags != lastflags )
			printf("cts=%s  cd=%s  dtr=%s\n",
				cts?"high":"low",
				cd?"high":"low",
				dtr?"high":"low"
			);
		lastflags = flags;

		sleep(POLL_INTERVAL);
	}

	close(fd);
}



