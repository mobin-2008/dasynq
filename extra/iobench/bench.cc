/*
 * Copyright 2003 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Mon 03/10/2003 - Modified by Davide Libenzi <davidel@xmailserver.org>
 *
 *     Added chain event propagation to improve the sensitivity of
 *     the measure respect to the event loop efficency.
 *
 *
 */

#define	timersub(tvp, uvp, vvp)						\
    do {								\
        (vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
        (vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
        if ((vvp)->tv_usec < 0) {				\
            (vvp)->tv_sec--;				\
            (vvp)->tv_usec += 1000000;			\
        }							\
    } while (0)

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "dasynq.h"


static int count, writes, fired;
static int *pipes;
static int num_pipes, num_active, num_writes;
static int timers, native;

using namespace dasynq;

NEventLoop eloop;

#include <vector>

using namespace std;



class Pipeio : public NEventLoop::FdWatcher
{
    public:
    int idx;

    virtual Rearm fdEvent(NEventLoop &eloop, int fd, int flags);
};

class PTimer : public NEventLoop::Timer
{
    virtual Rearm timerExpiry(NEventLoop &eloop, int intervals)
    {
    	return Rearm::DISARM;
    }
};


static vector<Pipeio> evio;
static vector<PTimer> evto;

Rearm Pipeio::fdEvent(NEventLoop &eloop, int fd, int flags)
{
    int widx = idx + 1;
    if (timers) {
        struct timespec ts;
        ts.tv_sec = 10;
        ts.tv_nsec = drand48() * 1e9;
        evto[idx].armTimerRel(eloop, ts);
    }

    unsigned char ch;
    count += read(fd, &ch, sizeof(ch));
    if (writes) {
	if (widx >= num_pipes)
	    widx -= num_pipes;
	write(pipes[2 * widx + 1], "e", 1);
	writes--;
	fired++;
    }
    return Rearm::REARM;
}



struct timeval *
run_once(void)
{
    int *cp, i, space;
    static struct timeval ta, ts, te, tv;

    gettimeofday(&ta, NULL);
    for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        evio[i].addWatch(eloop, cp[0], IN_EVENTS, true, i /* prio */);
        if (timers) {
            evto[i].addTimer(eloop);
            
            struct timespec tsv;
            tsv.tv_sec = 10;
            tsv.tv_nsec = drand48() * 1e9;
            evto[i].armTimerRel(eloop, tsv);
        }
    }
    
    fired = 0;
    space = num_pipes / num_active;
    space = space * 2;
    for (i = 0; i < num_active; i++, fired++)
    	write(pipes[i * space + 1], "e", 1);
    
    count = 0;
    writes = num_writes;
    
    {
        int xcount = 0;
        gettimeofday(&ts, NULL);
        do {
	    // event_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);
	    eloop.run();
            xcount++;
        } while (count != fired);
        gettimeofday(&te, NULL);
        
        //if (xcount != count) fprintf(stderr, "Xcount: %d, Rcount: %d\n", xcount, count);
    }

    timersub(&te, &ta, &ta);
    timersub(&te, &ts, &ts);
    fprintf(stdout, "%8ld %8ld\n",
		ta.tv_sec * 1000000L + ta.tv_usec,
		ts.tv_sec * 1000000L + ts.tv_usec
            );

    return (&te);
}

int
main (int argc, char **argv)
{
    struct rlimit rl;
    int i, c;
    struct timeval *tv;
    int *cp;
    extern char *optarg;

    num_pipes = 100;
    num_active = 1;
    num_writes = num_pipes;
    while ((c = getopt(argc, argv, "n:a:w:te")) != -1) {
        switch (c) {
            case 'n':
                num_pipes = atoi(optarg);
                break;
            case 'a':
                num_active = atoi(optarg);
                break;
            case 'w':
                num_writes = atoi(optarg);
                break;
            case 'e':
                native = 1;
                break;
            case 't':
                timers = 1;
                break;
            default:
                fprintf(stderr, "Illegal argument \"%c\"\n", c);
		exit(1);
        }
    }

    rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
        perror("setrlimit");
    }

    evio.resize(num_pipes);
    evto.resize(num_pipes);
    //events = calloc(num_pipes, sizeof(struct event));
    pipes = (int *) calloc(num_pipes * 2, sizeof(int));
    if ( /* events == NULL || */ pipes == NULL) {
        perror("malloc");
        exit(1);
    }

    for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
        evio[i].idx = i;
        //evto[i].addTimer(eloop);
            
#ifdef USE_PIPES
        if (pipe(cp) == -1) {
#else
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, cp) == -1) {
#endif
            perror("pipe");
            exit(1);
        }
    }

    for (i = 0; i < 2; i++) {
        tv = run_once();

        // deregister watchers now
        for (int j = 0; j < num_pipes; j++) {
            evio[j].deregister(eloop);
            if (timers) {
                evto[j].deregister(eloop);
            }
        }
    }

    exit(0);
}