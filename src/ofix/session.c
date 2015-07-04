// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dtime.h"
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
    bool		done;
    bool		closed;
    pthread_t		thread;
    pthread_mutex_t	send_mutex;
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
    s->done = true;
    s->closed = true;

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

static bool
handle_session_msg(ofixErr err, ofixSession session, const char *mt, ofixMsg msg) {
    if ('\0' != mt[1]) {
	return false;
    }
    switch (*mt) {
    case '0': // Heartbeat
    case '1': // TestRequest
    case '2': // ResendRequest
    case '3': // Reject
    case '4': // SequenceReset
    case '5': // Logout
	return false;
    case 'A': // Logon
	printf("*** received logon\n");
	if (ofix_msg_tag_exists(msg, OFIX_HeartBtIntTAG)) {
	    session->heartbeat_interval = ofix_msg_get_int(err, msg, OFIX_HeartBtIntTAG);
	}
	//TBD logon(err, session);
	break;
    default:
	return false;
    }
    return true;
}

static void*
session_loop(void *arg) {
    ofixSession		session = (ofixSession)arg;
    char		buf[4096];
    char		*b = buf;
    char		*start = buf;
    char		*end = buf + sizeof(buf);
    fd_set		xfds;
    fd_set		rfds;
    struct timeval	to;
    int			cnt;
    int			sock = session->sock;
    int			max_sock = session->sock + 1;
    socklen_t		rlen = 0;
    ssize_t		rcnt;
    int			msg_len = 0;

    session->done = false;
    session->closed = false;
    while (!session->done) {
	while (start < b && isspace(*start)) {
	    start++;
	}
	if (b - start < 22 || (0 < msg_len && b - start < msg_len)) {
	    // Slide to make room before reading.
	    if (buf < start) {
		if (0 < b - start) {
		    memmove(buf, start, b - start);
		    b -= start - buf;
		} else {
		    b = buf;
		}
		start = buf;
	    }
	    to.tv_sec = 1;
	    to.tv_usec = 0;
	    FD_ZERO(&rfds);
	    FD_ZERO(&xfds);
	    FD_SET(sock, &rfds);
	    FD_SET(sock, &xfds);
	    if (0 < (cnt = select(max_sock, &rfds, 0, &xfds, &to))) {
		if (FD_ISSET(sock, &xfds)) {
		    session->done = true;
		    break;
		}
		if (FD_ISSET(sock, &rfds)) {
		    rlen = 0;
		    rcnt = recvfrom(session->sock, b, end - b, 0, 0, &rlen);
		    b += rcnt;
		    printf("*** recvfrom %ld\n", rcnt);
		}
	    } else if (0 == cnt) {
		continue;
	    } else {
		printf("*** select error %d - %s\n", errno, strerror(errno));
		session->done = true;
		break;
	    }
	}
	if (22 <= b - start && 0 == msg_len) {
	    msg_len = ofix_msg_expected_buf_size(start);
	    if (0 == msg_len) {
		*b = '\0';
		printf("*** failed to parse message length, aborting '%s'\n", start);
		break;
	    }
	    // TBD if msg_len is greater than buf then allocate,exit for now
	}
	if (0 < msg_len && msg_len <= b - start) {
	    struct _ofixErr	err = OFIX_ERR_INIT;
	    ofixMsg		msg = ofix_msg_parse(&err, start, msg_len);

	    if (OFIX_OK != err.code) {
		printf("*** parse error: %s\n", err.msg);
	    } else {
		int64_t		seq = ofix_msg_get_int(&err, msg, OFIX_MsgSeqNumTAG);
		const char	*mt = ofix_msg_get_str(&err, msg, OFIX_MsgTypeTAG);
		bool		keep = false;
		const char	*sid = ofix_msg_get_str(&err, msg, OFIX_SenderCompIDTAG);

		ofix_store_add(&err, session->store, seq, OFIX_IODIR_RECV, msg);
		if (NULL == mt) {
		    printf("*** Invalid message. No MsgType field.\n");
		} else if (NULL == sid || 0 != strcmp(session->sid, sid)) {
		    printf("*** Error: Expected sender of '%s'. Received '%s'.\n", session->sid, (NULL == sid ? "<null>" : sid));
		    session->recv_seqnum = seq;
		} else if (session->recv_seqnum == seq) {
		    printf("*** Warn: Duplicate message  from '%s'.\n", session->sid);
		    // TBD check dup flag if the same
		} else if (session->recv_seqnum + 1 != seq) {
		    printf("*** Error: '%s' did not send the correct sequence number.\n", session->sid);
		} else if (handle_session_msg(&err, session, mt, msg)) {
		    // TBD if seq is not the next then error, try to recover
		    session->recv_seqnum = seq;
		} else if ('A' == *mt && '\0' == mt[1]) {
		    if (ofix_msg_tag_exists(msg, OFIX_HeartBtIntTAG)) {
			session->heartbeat_interval = ofix_msg_get_int(&err, msg, OFIX_HeartBtIntTAG);
		    }
		    session->recv_seqnum = seq;
		    //TBD logon(&err, session);
		} else if (NULL != session->recv_cb) {
		    printf("*** callback for seq num %lld\n", seq);
		
		    session->recv_seqnum = seq;
		    keep = !session->recv_cb(session, msg, session->recv_ctx);
		}
		if (!keep) {
		    ofix_msg_destroy(msg);
		}
	    }
	    start += msg_len;
	    msg_len = 0;
	    if (OFIX_OK != err.code) {
		printf("*** Error: [%d] %s.\n", err.code, err.msg);
		ofix_err_clear(&err);
	    }
	}
    }
    if (0 < session->sock) {
	close(session->sock);
	session->sock = 0;
    }
    session->closed = true;

    return NULL;
}

void
ofix_session_connect(ofixErr err, ofixSession session, const char *host, int port) {
    struct sockaddr_in	sin;
    struct sockaddr_in	sout;
    uint32_t		addr = net_addr(err, host);
    int			serr;
    double		giveup;

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
    if (0 != pthread_create(&session->thread, 0, session_loop, session)) {
	if (NULL != err) {
	    err->code = OFIX_THREAD_ERR;
	    strcpy(err->msg, "Failed to start session thread.");
	}
    }
    giveup = dtime() + 2.0;
    while (session->closed && session->done) {
	if (giveup < dtime()) {
	    err->code = OFIX_NETWORK_ERR;
	    strcpy(err->msg, "Timed out waiting for client to start.");
	    // TBD cleanup
	    return;
	}
    }
    printf("*** client connected\n");
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
