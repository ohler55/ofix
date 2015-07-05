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
#include "client.h"
#include "engine.h"
#include "store.h"
#include "tag.h"
#include "private.h"

struct _ofixClient {
    ofixEngine		engine;
    struct _ofixSession	session;
    char		*user;
    char		*password;
};

ofixClient
ofix_client_create(ofixErr err,
		    const char *cid,
		    const char *sid,
		    const char *store_path,
		    ofixRecvCallback cb,
		    void *ctx) {
    if (NULL != err && OFIX_OK != err->code) {
	return NULL;
    }
    if (NULL == sid) {
	if (NULL != err) {
	    err->code = OFIX_ARG_ERR;
	    strcpy(err->msg, "NULL client server identifier argument to client create is not valid.");
	}
	return NULL;
    }
    ofixClient	client = (ofixClient)malloc(sizeof(struct _ofixClient));

    if (NULL == client) {
	if (NULL != err) {
	    err->code = OFIX_MEMORY_ERR;
	    strcpy(err->msg, "Failed to allocate memory for client.");
	}
	return NULL;
    }
    
    _ofix_session_init(err, &client->session, cid, sid, store_path, cb, ctx);

    client->user = NULL;
    client->password = NULL;

    return client;
}

void
ofix_client_destroy(ofixErr err, ofixClient client) {
    if (NULL != client) {
	_ofix_session_free(&client->session);
	free(client);
    }
}

static void
logon(ofixErr err, ofixClient client) {
    // TBD use a configured version
    ofixMsg	msg = ofix_msg_create(err, "A", 4, 4, 14);

    if (NULL == msg) {
	return;
    }
    ofix_msg_set_int(err, msg, OFIX_EncryptMethodTAG, 0); // not encrypted
    ofix_msg_set_int(err, msg, OFIX_HeartBtIntTAG, client->session.heartbeat_interval);
    ofix_msg_set_bool(err, msg, OFIX_ResetSeqNumFlagTAG, true);
    //ofix_msg_set_bool(err, msg, OFIX_TestMessageIndicatorTAG, true);
    if (NULL != client->user) {
	ofix_msg_set_str(err, msg, OFIX_UsernameTAG, client->user);
    }
    if (NULL != client->password) {
	ofix_msg_set_str(err, msg, OFIX_PasswordTAG, client->password);
    }
    client->session.logon_sent = true;
    ofix_client_send(err, client, msg);
}

void
ofix_client_connect(ofixErr err, ofixClient client, const char *host, int port, double timeout) {
    struct sockaddr_in	sin;
    struct sockaddr_in	sout;
    uint32_t		addr = _ofix_net_addr(err, host);
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
    if (-1 == (client->session.sock = socket(AF_INET, SOCK_STREAM, 0))) {
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
    if (0 != bind(client->session.sock, (struct sockaddr*)&sin, sizeof(sin))) {
	if (NULL != err) {
	    err->code = OFIX_NETWORK_ERR;
	    snprintf(err->msg, sizeof(err->msg),
		     "Bind failed to %d.%d.%d.%d on socket %d, error [%d] %s",
		     addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF, client->session.sock,
		     errno, strerror(errno));
	}
    }
    serr = EINPROGRESS;
    while (EINPROGRESS == serr || EALREADY == serr) {
	if (-1 == connect(client->session.sock, (struct sockaddr*)&sout, sizeof(sout))) {
	    serr = errno;
	}
    }
    _ofix_session_start(err, &client->session, true);
    if (NULL != err && OFIX_OK != err->code) {
	return;
    }
    logon(err, client);
    if (0.0 < timeout) {
	double	giveup = dtime() + timeout;

	while (!client->session.logon_recv) {
	    if (giveup < dtime()) {
		err->code = OFIX_LOGON_ERR;
		strcpy(err->msg, "Timed out waiting for logon to complete.");
		return;
	    }
	}
    }
}

void
ofix_client_send(ofixErr err, ofixClient client, ofixMsg msg) {
    ofix_session_send(err, &client->session, msg);
}

ofixMsg
ofix_client_get_msg(ofixErr err, ofixClient client, int64_t seqnum) {
    return ofix_session_get_msg(err, &client->session, seqnum);
}

int64_t
ofix_client_send_seqnum(ofixClient client) {
    return client->session.sent_seq;
}

int64_t
ofix_client_recv_seqnum(ofixClient client) {
    return client->session.recv_seq;
}