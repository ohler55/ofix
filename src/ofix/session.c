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
#include "private.h"

void
_ofix_session_init(ofixErr err,
		   ofixSession s,
		   const char *sid,
		   const char *tid,
		   const char *store_path,
		   ofixRecvCallback cb,
		   void *ctx) {
    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    if (NULL == sid) {
	if (NULL != err) {
	    err->code = OFIX_ARG_ERR;
	    strcpy(err->msg, "NULL sender identifier argument to session create is not valid.");
	}
	return;
    }
    s->sent_seq = 0;
    s->recv_seq = 0;
    s->sock = 0;
    s->recv_cb = cb;
    s->recv_ctx = ctx;
    s->heartbeat_interval = 30;
    if (0 != pthread_mutex_init(&s->send_mutex, 0)) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to initialize mutex.");
	}
	free(s);
	return;
    }
    if (NULL == (s->store = ofix_store_create(err, store_path, sid))) {
	free(s);
	return;
    }
    s->sid = strdup(sid);
    if (NULL == tid) {
	s->tid = NULL;
    } else {
	s->tid = strdup(tid);
    }
    s->done = true;
    s->closed = true;
}

void
_ofix_session_free(ofixSession session) {
    double	give_up = dtime() + 2.0;

    session->done = true;
    while (dtime() < give_up && !session->closed) {
	dsleep(0.1);
    }
    if (0 < session->sock) {
	close(session->sock);
	session->sock = 0;
    }
    if (NULL != session->store) {
	ofix_store_destroy(session->store);
	session->store = NULL;
    }
    free(session->sid);
    free(session->tid);
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
		}
	    } else if (0 == cnt) {
		continue;
	    } else {
		// TBD if after logout then no error
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
		} else if (NULL == sid || 0 != strcmp(session->tid, sid)) {
		    printf("*** Error: -Expected sender of '%s'. Received '%s'.\n", session->tid, (NULL == sid ? "<null>" : sid));
		    session->recv_seq = seq;
		} else if (session->recv_seq == seq) {
		    printf("*** Warn: Duplicate message  from '%s'.\n", session->tid);
		    // TBD check dup flag if the same
		} else if (session->recv_seq + 1 != seq) {
		    printf("*** Error: '%s' did not send the correct sequence number.\n", session->tid);
		} else if (handle_session_msg(&err, session, mt, msg)) {
		    // TBD if seq is not the next then error, try to recover
		    session->recv_seq = seq;
		} else if ('A' == *mt && '\0' == mt[1]) {
		    if (ofix_msg_tag_exists(msg, OFIX_HeartBtIntTAG)) {
			session->heartbeat_interval = ofix_msg_get_int(&err, msg, OFIX_HeartBtIntTAG);
		    }
		    session->recv_seq = seq;
		    //TBD logon(&err, session);
		} else if (NULL != session->recv_cb) {
		    session->recv_seq = seq;
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
_ofix_session_start(ofixErr err, ofixSession session) {
    double	giveup;

    if (0 != pthread_create(&session->thread, 0, session_loop, session)) {
	if (NULL != err) {
	    err->code = OFIX_THREAD_ERR;
	    strcpy(err->msg, "Failed to start sessionthread.");
	}
    }
    giveup = dtime() + 2.0;
    while (session->closed && session->done) {
	if (giveup < dtime()) {
	    err->code = OFIX_NETWORK_ERR;
	    strcpy(err->msg, "Timed out waiting for session to start.");
	    return;
	}
    }
}

void
ofix_session_send(ofixErr err, ofixSession session, ofixMsg msg) {
    int			cnt;
    const char		*str;
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;
    int64_t		seq;

    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    ofix_msg_set_date(err, msg, OFIX_SendingTimeTAG, &now);
    ofix_msg_set_str(err, msg, OFIX_SenderCompIDTAG, session->sid);
    ofix_msg_set_str(err, msg, OFIX_TargetCompIDTAG, session->tid);

    pthread_mutex_lock(&session->send_mutex);
    session->sent_seq++;
    seq = session->sent_seq;
    ofix_msg_set_int(err, msg, OFIX_MsgSeqNumTAG, seq);
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
    ofix_store_add(err, session->store, seq, OFIX_IODIR_SEND, msg);
}

ofixMsg
ofix_session_get_msg(ofixErr err, ofixSession session, int64_t seqnum) {
    // TBD
    return NULL;
}

int64_t
ofix_session_send_seqnum(ofixSession session) {
    return session->sent_seq;
}

int64_t
ofix_session_recv_seqnum(ofixSession session) {
    return session->recv_seq;
}
