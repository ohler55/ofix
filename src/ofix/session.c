// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "session.h"
#include "engine.h"
#include "store.h"
#include "tag.h"

struct _ofixSession {
    ofixEngine		engine;
    char		*cid;
    char		*sid;
    int64_t		seq_num; // last seq_num
    Store		store;
    int			sock;
    ofixRecvCallback	recv_cb;
    void		*recv_ctx;
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
    s->cid = strdup(cid);
    s->sid = strdup(sid);
    s->seq_num = 0;
    s->sock = 0;
    s->recv_cb = cb;
    s->recv_ctx = ctx;
    if (NULL == (s->store = ofix_store_create(err, store_path, cid))) {
	free(s);
	return NULL;
    }
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
}

void
ofix_session_send(ofixErr err, ofixSession session, ofixMsg msg) {
    int		cnt = ofix_msg_size(err, msg);
    const char	*str;

    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    ofix_msg_set_str(err, msg, OFIX_SenderCompIDTAG, session->cid);
    ofix_msg_set_str(err, msg, OFIX_TargetCompIDTAG, session->sid);
    // TBD mutex protect
    session->seq_num++;
    ofix_msg_set_int(err, msg, OFIX_MsgSeqNumTAG, session->seq_num);
    //

    str = ofix_msg_FIX_str(err, msg);
    if (cnt != send(session->sock, str, cnt, 0)) {
	if (NULL != err) {
	    err->code = OFIX_WRITE_ERR;
	    snprintf(err->msg, sizeof(err->msg),
		     "Failed to send message. error [%d] %s", errno, strerror(errno));
	}
    }
}

ofixMsg
ofix_session_get_msg(ofixErr err, ofixSession session, int64_t seq_num) {
    // TBD
    return NULL;
}

void
ofix_session_listen(ofixErr err, ofixSession session, ofixRecvCallback cb, void *ctx) {
    // TBD
}

void
_ofix_session_recv(ofixSession session, ofixMsg msg) {
    // TBD
}
