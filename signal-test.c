//
//  signal-test.c
//  lwt
//
//  Created by Tongliang Liu on 10/18/13.
//  Copyright (c) 2013 cooniur. All rights reserved.
//

/*
 Example of SIGALRM usage
 
 Jim Teresco, Fall 2002, Computer Science 432, Williams College
 
 $Id: sigalrm-example.c,v 1.1 2002/10/01 01:38:55 terescoj Exp $
 */
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <math.h>

/* we make some variables global so they can be seen easily from
 the signal handler and the main program */
time_t start_time;
int i,j,k;
double ans=0.0;

/* Our signal handler.  We will tell the system to call this
 function whenever it gets a SIGALRM */
void sigFunc(int sig) {
	
	printf("Alarm!  Alarm!  got signal %d at time %d\n",
		   sig, time(NULL)-start_time);
	printf("i=%d, j=%d, k=%d, ans=%f\n",i,j,k,ans);
	/* reset the timer so we get called again in 5 seconds */
	alarm(5);
}

int main() {
	
	/* remember starting time - this returns the number of seconds
     since the Unix epoch at 1970/01/01 00:00:00 */
	start_time=time(NULL);
	printf("This program will compute for a long time, but\n");
	printf("will handle SIGALRM events asynchronously to\n");
	printf("report on its progress every 5 seconds\n");
	/* set up our signal handler to catch SIGALRM */
	signal(SIGALRM, sigFunc);
	
	/* start the timer - we want to wake up in 5 seconds */
	alarm(5);
	
	/* compute stuff for a long time */
	for (i=0;i<10000;i++)
		for (j=0;j<10000;j++)
			for(k=0;k<10000;k++)
				ans=ans+sqrt(i)+sqrt(j)-sqrt(k);
	
	/* for the very patient... */
	printf("Answer is: %f\n", ans);
	return 0;
}
