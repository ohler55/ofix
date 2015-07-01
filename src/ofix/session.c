// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "session.h"
#include "engine.h"
#include "store.h"
#include "tag.h"

struct _ofixSession {
    ofixEngine		engine;
    char		*cid;
    char		*sid;
    char		*user;
    char		*password;
    int64_t		seqnum; // last seqnum
    int64_t		recv_seqnum; // last seqnum
    Store		store;
    int			sock;
    int			heartbeat_interval;
    ofixRecvCallback	recv_cb;
    void		*recv_ctx;
    pthread_mutex_t	send_mutex;
    // TBD processing thread(s)
};

ofixSession
ofix_session_create(ofixErr err,
		    const char *cid,
		    const char *sid,
		    const char *store_path,
		    ofixRecvCallback cb,
		    void *ctx) {
    if (NULL != err && OFIX_OK != err->code) {
	return NULL;
    }
    if (NULL == cid) {
	if (NULL != err) {
	    err->code = OFIX_ARG_ERR;
	    strcpy(err->msg, "NULL session client identifier argument to session create is not valid.");
	}
	return NULL;
    }
    if (NULL == sid) {
	if (NULL != err) {
	    err->code = OFIX_ARG_ERR;
	    strcpy(err->msg, "NULL session server identifier argument to session create is not valid.");
	}
	return NULL;
    }
    ofixSession	s = (ofixSession)malloc(sizeof(struct _ofixSession));

    if (NULL == s) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to allocate memory for session.");
	}
	return NULL;
    }
    s->seqnum = 0;
    s->recv_seqnum = 0;
    s->sock = 0;
    s->recv_cb = cb;
    s->recv_ctx = ctx;
    s->heartbeat_interval = 30;
    s->user = NULL;
    s->password = NULL;
    if (0 != pthread_mutex_init(&s->send_mutex, 0)) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to initialize mutex.");
	}
	free(s);
	return NULL;
    }
    if (NULL == (s->store = ofix_store_create(err, store_path, cid))) {
	free(s);
	return NULL;
    }
    s->cid = strdup(cid);
    s->sid = strdup(sid);

    return s;
}

void
ofix_session_destroy(ofixErr err, ofixSession session) {
    if (NULL != session) {
	if (0 < session->sock) {
	    close(session->sock);
	}
	ofix_store_destroy(session->store);
	free(session->cid);
	free(session->sid);
	free(session);
    }
}

static uint32_t
net_addr(ofixErr err, const char *host) {
    struct hostent	*h;

    h = gethostbyname(host);
    if (0 == h || h->h_length <= 0) {
	struct in_addr	in_addr;
	
	if (0 == inet_aton(host, &in_addr)) {
	    // inet_aton is the reverse of most functions and returns 0 for failure
	    if (NULL != err) {
		err->code = OFIX_NETWORK_ERR;
		snprintf(err->msg, sizeof(err->msg),
			 "Failed to resolved host '%s'", (NULL == host ? "<null>" : host));
	    }
	    return 0;
	}
	return in_addr.s_addr;
    }
    //printf("*** addrtype = %d\n", h->h_addrtype);
    if (AF_INET == h->h_addrtype) {
	uint32_t	a = 0;
	int		i;
	uint8_t		*c = (uint8_t*)*h->h_addr_list;

	for (i = 0; i < 4; i++) {
	    a = (a << 8) | *c++;
	}
	return a;
    }
    if (NULL != err) {
	err->code = OFIX_NETWORK_ERR;
	snprintf(err->msg, sizeof(err->msg),
		 "Failed to resolved host '%s'", (NULL == host ? "<null>" : host));
    }
    return 0;
}

static void
logon(ofixErr err, ofixSession session) {
    // TBD use a configured version
    ofixMsg	msg = ofix_msg_create(err, "A", 4, 4, 14);

    if (NULL == msg) {
	return;
    }
    ofix_msg_set_int(err, msg, OFIX_EncryptMethodTAG, 0); // not encrypted
    ofix_msg_set_int(err, msg, OFIX_HeartBtIntTAG, session->heartbeat_interval);
    ofix_msg_set_bool(err, msg, OFIX_ResetSeqNumFlagTAG, true);
    //ofix_msg_set_bool(err, msg, OFIX_TestMessageIndicatorTAG, true);
    if (NULL != session->user) {
	ofix_msg_set_str(err, msg, OFIX_UsernameTAG, session->user);
    }
    if (NULL != session->password) {
	ofix_msg_set_str(err, msg, OFIX_PasswordTAG, session->password);
    }
    ofix_session_send(err, session, msg);
    // TBD flag for when logon has succeeded or failed
}

void
ofix_session_connect(ofixErr err, ofixSession session, const char *host, int port) {
    struct sockaddr_in	sin;
    struct sockaddr_in	sout;
    uint32_t		addr = net_addr(err, host);
    int			serr;

    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    memset(&sin, 0, sizeof(sin));
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_family = AF_INET;
    memset(&sout, 0, sizeof(sout));
    sout.sin_family = AF_INET;
    sout.sin_port = htons(port);
    sout.sin_addr.s_addr = htonl(addr);
    //sout.sin_addr.s_addr = htonl(sout.sin_addr.s_addr);
    //printf("*** Connecting to %d.%d.%d.%d on port %d\n",
    //addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, port);
    if (-1 == (session->sock = socket(AF_INET, SOCK_STREAM, 0))) {
	printf("*** Failed to create socket to %d.%d.%d.%d on port %d, error [%d] %s\n",
	       addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, port,
	       errno, strerror(errno));
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg),
		     "Failed to create socket to %d.%d.%d.%d on port %d, error [%d] %s",
		     addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, port,
		     errno, strerror(errno));
	}
	return;
    }
    if (0 != bind(session->sock, (struct sockaddr*)&sin, sizeof(sin))) {
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg),
		     "Bind failed to %d.%d.%d.%d on socket %d, error [%d] %s",
		     addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, session->sock,
		     errno, strerror(errno));
	}
    }
    serr = EINPROGRESS;
    while (EINPROGRESS == serr || EALREADY == serr) {
	if (-1 == connect(session->sock, (struct sockaddr*)&sout, sizeof(sout))) {
	    serr = errno;
	}
    }
    // TBD start listen thread, wait for it to be started and ready

    logon(err, session);
}

void
ofix_session_send(ofixErr err, ofixSession session, ofixMsg msg) {
    int			cnt;
    const char		*str;
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;

    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    ofix_msg_set_date(err, msg, OFIX_SendingTimeTAG, &now);
    ofix_msg_set_str(err, msg, OFIX_SenderCompIDTAG, session->cid);
    ofix_msg_set_str(err, msg, OFIX_TargetCompIDTAG, session->sid);

    pthread_mutex_lock(&session->send_mutex);
    session->seqnum++;
    ofix_msg_set_int(err, msg, OFIX_MsgSeqNumTAG, session->seqnum);
    cnt = ofix_msg_size(err, msg);

    // TBD
    if (true) {
	char	*s = ofix_msg_to_str(err, msg);
	printf("*** sending %s\n", s);
	free(s);
    }
    str = ofix_msg_FIX_str(err, msg);
    if (cnt != send(session->sock, str, cnt, 0)) {
	if (NULL != err) {
	    err->code = OFIX_WRITE_ERR;
	    snprintf(err->msg, sizeof(err->msg),
		     "Failed to send message. error [%d] %s", errno, strerror(errno));
	}
    }
    pthread_mutex_unlock(&session->send_mutex);
}

ofixMsg
ofix_session_get_msg(ofixErr err, ofixSession session, int64_t seqnum) {
    // TBD
    return NULL;
}

int64_t
ofix_session_send_seqnum(ofixSession session) {
    return session->seqnum;
}

int64_t
ofix_session_recv_seqnum(ofixSession session) {
    return session->recv_seqnum;
}
