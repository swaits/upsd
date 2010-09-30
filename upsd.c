#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <syslog.h>

#define POLL_INTERVAL (1)   // poll the ups this often (seconds)
#define KILL_TIME     (10)  // ups needs this much time with shutdown signal
#define SHUTDOWN_TIME (300) // halt system after this much time
#define DEBOUNCE_TIME (5)   // fluctuations less than this time don't count

#define SHUTDOWN_CMD  "/sbin/halt -p"

#define INIT_BITS        TIOCM_RTS
#define KILL_BITS        TIOCM_DTR
#define BATT_GOOD_BITS   TIOCM_CD
#define POWER_GOOD_BITS  TIOCM_CTS

int fd = -1;

enum 
{
	U_OK,
	U_LOW,
	U_FAIL
};

void warn(int secondsleft)
{
	//printf("warn\n");
	syslog(LOG_ALERT,"power failure.  shutting down in %d seconds.",secondsleft);
}

void lowmessage()
{
	//printf("low\n");
	syslog(LOG_ALERT,"ups battery low.");
}

void setline(int kill)
{
	int flags;

	// Get the current line bits
	ioctl(fd, TIOCMGET, &flags);

	// and set ours
	flags |= INIT_BITS;
	if ( kill )
	{
		syslog(LOG_ALERT,"ups powerdown enabled.");
		flags |= KILL_BITS;
	}
	else
	{
		//syslog(LOG_ALERT,"reset ups.  ups powerdown disabled.");
		flags &= ~KILL_BITS;
	}
	ioctl(fd, TIOCMSET, &flags);
}

void init()
{
	//printf("init\n");
	setline(0);
}

void killups()
{
	//printf("killups\n");
	setline(1);
}

void shutdown()
{
	//printf("shutdown\n");
	syslog(LOG_ALERT,"shutting down.");
	system(SHUTDOWN_CMD);
}

void cancel()
{
	//printf("cancel\n");
	syslog(LOG_ALERT,"power returned.  cancelling shutdown.");
	init();
}

void flagdump(int flags)
{
	int low  = !((flags & BATT_GOOD_BITS) == BATT_GOOD_BITS);
	int fail = !((flags & POWER_GOOD_BITS) == POWER_GOOD_BITS);
	int init = ((flags & INIT_BITS) == INIT_BITS);
	int kill = ((flags & KILL_BITS) == KILL_BITS);

	//printf("low=%d  fail=%d  init=%d  kill=%d\n",low,fail,init,kill);
}

int readline()
{
	static int count         = 0;
	static int state         = U_OK;
	static int lastnextstate = U_OK;
	int nextstate;

	int flags;

	// Get the current line bits
	ioctl(fd, TIOCMGET, &flags);

	flagdump(flags);

	// nextstate = current line conditions
	nextstate = U_OK;
	if ( (flags & POWER_GOOD_BITS) != POWER_GOOD_BITS )
	{
		if ( (flags & BATT_GOOD_BITS) != BATT_GOOD_BITS )
		{
			nextstate = U_LOW;
		}
		else
		{
			nextstate = U_FAIL;
		}
	}

	// count the time we've been in this new state
	if ( nextstate != lastnextstate )
	{
		count = 0;
	}
	else
	{
		// keep this from overflowing
		if ( count <= DEBOUNCE_TIME )
		{
			count += POLL_INTERVAL;
		}
	}
	lastnextstate = nextstate;

	// and if we exceed the threshold take the state as our current
	if ( count > DEBOUNCE_TIME )
	{
		state = nextstate;
	}

	return state;
}

void term()
{
	syslog(LOG_NOTICE,"caught signal.");
	init();
	if ( fd != -1 )
	{
		close(fd);
	}
	syslog(LOG_NOTICE,"terminated.");
	exit(EXIT_SUCCESS);
}

int main(int argc,char** argv)
{
	// check parameters
	if ( argc < 2 )
	{
		fprintf(stderr, "Usage: upsd <device>\n");
		exit(1);
	}

	// open serial port
	if ( (fd = open(argv[1], O_RDONLY | O_NDELAY)) < 0 )
	{
		fprintf(stderr, "upsd: %s: %s\n", argv[1], sys_errlist[errno]);
		exit(1);
	}

	// become a daemon
	switch ( fork() )
	{
		case 0:		/* I am the child. */
			setsid();
			break;
		case -1:		/* Failed to become daemon. */
			fprintf(stderr, "%s: can't create daemon.\n", argv[0]);
			exit(EXIT_FAILURE);
		default:		/* I am the parent. */
			exit(EXIT_SUCCESS);
	}

	// open syslog
	openlog("upsd",LOG_PID,LOG_DAEMON);
	syslog(LOG_NOTICE, "started on %s.",argv[1]);

	// setup termination routine
	signal(SIGHUP,SIG_IGN);
	signal(SIGINT,term);
	signal(SIGKILL,term);
	signal(SIGTERM,term);

	// init line
	init();

	while ( 1 )
	{
		static int count = 0;
		static int killtime = 0;
		static int laststate = U_OK;
		static int nextwarn = 0;
		int state;

		// get current ups state
		state = readline();
		//printf("state = %s\n",state==U_OK?"OK":state==U_FAIL?"U_FAIL":"U_LOW");

		// count the time we've been in this state
		if ( state == laststate )
		{
			count += POLL_INTERVAL;
		}

		// don't count uptime (avoid overflow)
		if ( state == U_OK )
		{
			count = 0;
			killtime = 0;
		}

		// if we just went back up let's reset
		if ( state == U_OK && state != laststate )
		{
			cancel();
		}

		// if the battery is low let's handle it immediately
		if ( state == U_LOW )
		{
			// warn if we just got the signal!
			if ( laststate != U_LOW )
			{
				lowmessage();
			}

			// set kill signal
			if ( killtime == 0 )
			{
				killups();
				killtime += POLL_INTERVAL;
			}
			else if ( killtime >= KILL_TIME )
			{
				shutdown();
			}
			else
			{
				killtime += POLL_INTERVAL;
			}
		}

		// if the power is off handle
		if ( state == U_FAIL )
		{
			// if this is the first time let's setup
			if ( state != laststate )
			{
				nextwarn = 0;
			}

			// kill within KILL_TIME of SHUTDOWN_TIME
			if ( count >= (SHUTDOWN_TIME - KILL_TIME) && killtime == 0 )
			{
				killups();
				killtime += POLL_INTERVAL;
			}

			// and shutdown at appropriate time
			if ( count >= SHUTDOWN_TIME )
			{
				shutdown();
			}
			else if ( count >= nextwarn )
			{
				int secondsleft = (SHUTDOWN_TIME-count);
				if ( secondsleft >= 10 )
				{
					warn(SHUTDOWN_TIME-count);
					nextwarn += secondsleft/2;
				}
			}
		}

		laststate = state;

		sleep(POLL_INTERVAL);
	}
}

