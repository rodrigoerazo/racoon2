/* $Id: rtsock.c,v 1.4 2007/07/24 07:38:34 fukumoto Exp $ */

/*
 * Copyright (C) 1995, 1996, 1997, 1998, and 2005 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#ifdef __linux__
#include <linux/rtnetlink.h>
#endif

#include "racoon.h"
#include "isakmp_impl.h"
#include "rtsock.h"
#include "debug.h"

static int rtsock = -1;

#if defined(RO_MSGFILTER) || defined(ROUTE_MSGFILTER)
        static unsigned char msgfilter[] = {
	    RTM_IFINFO,
#ifdef RTM_IFANNOUNCE
            RTM_IFANNOUNCE,
#endif
            RTM_ADD, RTM_CHANGE, RTM_DELETE, 
#ifdef RTM_CHGADDR
            RTM_CHGADDR,
#endif          
            RTM_NEWADDR, RTM_DELADDR 
        };  
#endif

int
rtsock_init(void)
{
	rtsock = socket(PF_ROUTE, SOCK_RAW, PF_UNSPEC);
	TRACE((PLOGLOC, "rtsock: %d\n", rtsock));
	if (rtsock < 0)
		return -1;

#if defined(RO_MSGFILTER)
	if (setsockopt(rtsock, PF_ROUTE, RO_MSGFILTER,
	    msgfilter, sizeof(msgfilter)) == -1)
		plog(PLOG_INTERR, PLOGLOC, 0,
		     "rtsock: RO_MSGFILTER %s\n", strerror(errno));
#elif defined(ROUTE_MSGFILTER)
	/* Convert the array into a bitmask. */
	int msgfilter_mask = 0;
	for (i = 0; i < __arraycount(msgfilter); i++)
		msgfilter_mask |= ROUTE_FILTER(msgfilter[i]);
	if (setsockopt(ctx->link_fd, PF_ROUTE, ROUTE_MSGFILTER,
	    &msgfilter_mask, sizeof(msgfilter_mask)) == -1)
		plog(PLOG_INTERR, PLOGLOC, 0,
		     "rtsock: ROUTE_MSGFILTER %s\n", strerror(errno));
#endif
	return 0;
}


int
rtsock_socket(void)
{
	return rtsock;
}


void
rtsock_process(void)
{
#ifndef __linux__
	ssize_t len;
#ifdef __linux__ 
	char buf[4096];
	struct sockaddr_nl dst_sa;
	struct iovec iov = { buf, sizeof(buf) };
	struct msghdr msg = { &dst_sa, sizeof(dst_sa), &iov, 1, NULL, 0, 0 }; 
	struct nlmsghdr nlh = (struct nlmsghdr *)buf;
#define rtm_type nlmsg_type
	len = recv(rtsock, &msg, sizeof(msg), 0);
#else
	struct rt_msghdr rtm[10];
	len = recv(rtsock, rtm, sizeof(rtm), 0);
#endif

	TRACE((PLOGLOC, "rtsock %d read len=%zd\n", rtsock, len));
	if (len < 0) {
		plog(PLOG_INTERR, PLOGLOC, 0,
		     "rtsock: recv: %s\n", strerror(errno));
		return;
	}

#ifdef RTM_VERSION
	if ((size_t)len < sizeof(int32_t)) {
		plog(PLOG_INTERR, PLOGLOC, NULL,
		     "PF_ROUTE message is too short (%zd)\n", len);
		return;
	}
	if (len < rtm->rtm_msglen) {
		plog(PLOG_INTERR, PLOGLOC, NULL,
		     "PF_ROUTE message len doesn't match (%zd < %u)\n",
		     len, (unsigned int)rtm->rtm_msglen);
		return;
	}

	if (rtm->rtm_version != RTM_VERSION) {
		plog(PLOG_INTERR, PLOGLOC, NULL,
			"routing socket version mismatch, closing socket\n");
		close(rtsock);
		rtsock = -1;
		return;
	}
#endif

	TRACE((PLOGLOC, "rtm_type %d\n", rtm->rtm_type));
	switch (rtm->rtm_type) {
#ifdef RTM_IFANNOUNCE
	case RTM_IFANNOUNCE:
#endif
	case RTM_NEWADDR:
	case RTM_DELADDR:
#ifdef RTM_CHGADDR
	case RTM_CHGADDR:
#endif
	case RTM_ADD:
	case RTM_CHANGE:
	case RTM_DELETE:

	case RTM_IFINFO:
		isakmp_reopen(); /* rescan interface addresses */
		break;
	case RTM_MISS:
		/* ignore this message silently */
		break;
	default:
		TRACE((PLOGLOC, "ignoring\n"));
		break;
	}
#endif
}
