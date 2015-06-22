// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "engine.h"
#include "dtime.h"
#include "store.h"
#include "session.h"
#include "tag.h"

struct _ofixEngSession {
    struct _ofixEngSession	*next;
    ofixEngine			engine;
    char			*id;
    int64_t			seq_num; // last seq_num
    Store			store;
    int				sock;
    bool			done;
    bool			closed;
    pthread_t			thread;
};

struct _ofixEngine {
    char		*id;
    char		*ipaddr;
    int			port;
    char		*auth_file;
    char 		*store_dir;
    int 		heartbeat_interval;
    bool		done;
    bool		closed;
    ofixEngSession	sessions;
    pthread_mutex_t	session_mutex;
    ofixEngRecvCallback	recv_cb;
    void		*recv_ctx;
};

static ofixEngSession
session_create(ofixErr err, struct _ofixEngine *eng, int sock) {
    if (NULL != err && OFIX_OK != err->code) {
	return NULL;
    }
    ofixEngSession	es = (ofixEngSession)malloc(sizeof(struct _ofixEngSession));

    if (NULL == es) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to allocate memory for session.");
	}
	return NULL;
    }
    es->engine = eng;
    es->id = NULL;
    es->store = NULL;
    es->seq_num = 0;
    es->sock = sock;
    es->done = false;
    es->closed = true;

    return es;
}

static void
session_destroy(ofixErr err, ofixEngSession session) {
    if (NULL != session) {
	double		give_up = dtime() + 2.0;
	ofixEngSession	es;
	ofixEngSession	prev = NULL;
	ofixEngine	eng = session->engine;

	pthread_mutex_lock(&eng->session_mutex);
	for (es = eng->sessions; NULL != es; es = es->next) {
	    if (es == session) {
		if (NULL == prev) {
		    eng->sessions = es->next;
		} else {
		    prev->next = es->next;
		}
		break;
	    }
	    prev = es;
	}
	pthread_mutex_unlock(&eng->session_mutex);

	session->done = true;
	while (dtime() < give_up && !session->closed) {
	    dsleep(0.1);
	}
	if (0 < session->sock) {
	    close(session->sock);
	}
	if (NULL != session->store) {
	    ofix_store_destroy(session->store);
	}
	free(session->id);
	free(session);
    }
}

static void*
session_loop(void *arg) {
    ofixEngSession	session = (ofixEngSession)arg;
    ofixEngine		eng = session->engine;
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
		int64_t	seq = ofix_msg_get_int(&err, msg, OFIX_MsgSeqNumTAG);

		if (NULL == session->id) {
		    char	path[1024];
		    time_t	now = time(NULL);
		    struct tm	*tm = gmtime(&now);

		    session->id = ofix_msg_get_str(&err, msg, OFIX_SenderCompIDTAG);
		    if (NULL == session->id) {
			printf("*** Message did not contain a sender identifier. Closing session.\n");
			session->done = true;
			break;
		    }
		    snprintf(path, sizeof(path), "%s/%s-%04d%02d%02d.%02d%02d%02d",
			     eng->store_dir, session->id,
			     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			     tm->tm_hour, tm->tm_min, tm->tm_sec);
		    session->store = ofix_store_create(&err, path, session->id);
		}
		ofix_store_add(&err, session->store, seq, OFIX_IODIR_RECV, msg);
		if (NULL != eng->recv_cb) {
		    if (eng->recv_cb(session, msg, eng->recv_ctx)) {
			ofix_msg_destroy(msg);
		    }
		} else {
		    ofix_msg_destroy(msg);
		}
	    }
	    start += msg_len;
	    msg_len = 0;
	}
    }
    if (0 < session->sock) {
	close(session->sock);
	session->sock = 0;
    }
    session->closed = true;

    return NULL;
}

static void
session_start(ofixErr err, ofixEngSession session) {
    if (0 != pthread_create(&session->thread, 0, session_loop, session)) {
	if (NULL != err) {
	    err->code = OFIX_THREAD_ERR;
	    strcpy(err->msg, "Failed to start session thread.");
	}
    }
}

ofixEngine
ofix_engine_create(ofixErr err,
		   const char *id,
		   int port,
		   const char *auth_file,
		   const char *store_dir,
		   int heartbeat_interval) {
    if (NULL != err && OFIX_OK != err->code) {
	return NULL;
    }
    ofixEngine	eng = (ofixEngine)malloc(sizeof(struct _ofixEngine));

    if (NULL == eng) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to allocate memory for engine.");
	}
	return NULL;
    }
    if (NULL == id) {
	if (NULL != err) {
	    err->code = OFIX_ARG_ERR;
	    strcpy(err->msg, "NULL session identifier argument to engine create is not valid.");
	}
	free(eng);
	return NULL;
    }
    eng->sessions = NULL;
    if (0 != pthread_mutex_init(&eng->session_mutex, 0)) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to initialize mutex.");
	}
	free(eng);
	return NULL;
    }
    eng->id = strdup(id);
    eng->done = false;
    eng->closed = false;
    eng->ipaddr = NULL;
    eng->port = port;
    if (NULL == auth_file) {
	eng->auth_file = NULL;
    } else {
	eng->auth_file = strdup(auth_file);
    }
    if (NULL == store_dir) {
	eng->store_dir = strdup(".");
    } else {
	char	cmd[1024];

	eng->store_dir = strdup(store_dir);
	snprintf(cmd, sizeof(cmd), "mkdir -p %s", eng->store_dir);
	if (0 != system(cmd)) {
	    if (NULL != err) {
		err->code = OFIX_WRITE_ERR;
		snprintf(err->msg, sizeof(err->msg), "Failed to create directory '%s'.", eng->store_dir);
	    }
	    free(eng);
	    return NULL;
	}
    }
    eng->heartbeat_interval = heartbeat_interval;

    return eng;
}

void
ofix_engine_destroy(ofixErr err, ofixEngine eng) {
    if (NULL != eng) {
	ofixEngSession	sessions;
	ofixEngSession	es;
	double		give_up;

	pthread_mutex_lock(&eng->session_mutex);
	sessions = eng->sessions;
	eng->sessions = NULL;
	pthread_mutex_unlock(&eng->session_mutex);

	while (NULL != (es = sessions)) {
	    sessions = sessions->next;
	    session_destroy(err, es);
	}
	eng->done = true;
	give_up = dtime() + 2.0;
	while (dtime() < give_up && !eng->closed) {
	    dsleep(0.1);
	}
	free(eng->id);
	free(eng->ipaddr);
	free(eng->auth_file);
	free(eng->store_dir);
	free(eng);
    }
}

void
ofix_engine_on_recv(ofixEngine eng, ofixEngRecvCallback cb, void *ctx) {
    eng->recv_cb = cb;
    eng->recv_ctx = ctx;
}

const char*
ofix_engine_id(ofixEngine eng) {
    return eng->id;
}

const char*
ofix_engine_ipaddr(ofixEngine eng) {
    return eng->ipaddr;
}

const char*
ofix_engine_auth_file(ofixEngine eng) {
    return eng->auth_file;
}

const char*
ofix_engine_store_dir(ofixEngine eng) {
    return eng->store_dir;
}

int
ofix_engine_heartbeat_interval(ofixEngine eng) {
    return eng->heartbeat_interval;
}

int
ofix_engine_port(ofixEngine eng) {
    return eng->port;
}

void
ofix_engine_start(ofixErr err, ofixEngine eng) {
    struct sockaddr_in	server_addr;
    struct sockaddr_in	client_addr;
    struct sockaddr_in	saddr;
    struct timeval	to;
    fd_set		xfds;
    fd_set		rfds;
    socklen_t		addr_len = sizeof(client_addr);
    int			csock;
    int			ssock;
    int			cnt;

    if (-1 == (ssock = socket(AF_INET, SOCK_STREAM, 0))) {
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg), "Failed to create socket. %s.", strerror(errno));
	}
	return;
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(eng->port);
    if (0 != bind(ssock, (struct sockaddr*)&server_addr, sizeof(server_addr))) {
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg), "Bind failed on socket. %s.", strerror(errno));
	}
	return;
    }
    if (0 != listen(ssock, 1)) {
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg), "Listen failed on socket. %s.", strerror(errno));
	}
	return;
    }
    eng->done = false;
    eng->closed = false;
    printf("*** starting loop\n");
    while (!eng->done) {
	to.tv_sec = 1;
	to.tv_usec = 0;
	FD_ZERO(&rfds);
	FD_ZERO(&xfds);
	FD_SET(ssock, &rfds);
	FD_SET(ssock, &xfds);
	if (0 < (cnt = select(ssock + 1, &rfds, 0, &xfds, &to))) {
	    if (FD_ISSET(ssock, &rfds)) {
		ofixEngSession	session = NULL;

		if (0 > (csock = accept(ssock, (struct sockaddr*)&client_addr, &addr_len))) {
		    csock = 0;
		    // TBD handle better
		    printf("*** Failed to accept a client connection.");
		    continue;
		}
		getpeername(csock, (struct sockaddr*)&saddr, &addr_len);
		saddr.sin_addr.s_addr = htonl(saddr.sin_addr.s_addr);

		printf("*** connection established from %d.%d.%d.%d on socket %d\n",
		       saddr.sin_addr.s_addr >> 24,
		       (saddr.sin_addr.s_addr >> 16) & 0xFF,
		       (saddr.sin_addr.s_addr >> 8) & 0xFF,
		       saddr.sin_addr.s_addr & 0xFF,
		       csock);

		session = session_create(err, eng, csock);
		if (NULL == session) {
		    if (NULL != err) {
			printf("*** %s\n", err->msg);
		    }
		    close(csock);
		} else {
		    session->next = eng->sessions;
		    eng->sessions = session;
		    session_start(err, session);
		    if (NULL != err && OFIX_OK != err->code) {
			printf("*** %s\n", err->msg);
			close(csock);
		    }
		}
	    } else if (FD_ISSET(ssock, &xfds)) {
		if (NULL != err) {
		    err->code = OFIX_NETWORK_ERR;
		    snprintf(err->msg, sizeof(err->msg), "Error on server socket.");
		}
		break;
	    }
	}
    }
    close(ssock);
    eng->closed = true;
}

ofixEngSession
ofix_engine_get_session(ofixErr err, ofixEngine eng, const char *cid) {
    // TBD
    return NULL;
}
