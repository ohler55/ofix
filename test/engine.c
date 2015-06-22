// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "test.h"
#include "ofix/store.h"
#include "ofix/engine.h"
#include "ofix/role.h"

static void*
start_engine(void *arg) {
    struct _ofixErr	err = OFIX_ERR_INIT;

    ofix_engine_start(&err, (ofixEngine)arg);

    return NULL;
}

static bool
server_cb(ofixEngSession session, ofixMsg msg, void *ctx) {
    struct _ofixErr	err = OFIX_ERR_INIT;
    char		*s = ofix_msg_to_str(&err, msg);

    printf("*** message callback: %s\n", s);
    free(s);
    return true;
}

static void
logon_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixEngine		server = ofix_engine_create(&err, "Server", 6161, NULL, "server_storage", 0);
    pthread_t		server_thread;
    ofixSession		client;
    const char		*vmsg = "8=FIX.4.4^9=113^35=D^49=Client^56=Server^34=4^52=20071031-17:42:33.123^11=order-4^21=1^55=IBM^54=2^60=20071031-17:42:11.321^40=7^10=206^";
    const char		*c = vmsg;
    char		buf[256];
    char		*b = buf;
    ofixMsg		msg;

    if (OFIX_OK != err.code || NULL == server) {
	test_print("Failed to create server [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_engine_on_recv(server, server_cb, NULL);

    // throw server into a separate thread
    if (0 != pthread_create(&server_thread, 0, start_engine, server)) {
	test_print("failed to start engine thread\n");
	test_fail();
	return;
    }

    client = ofix_session_create(&err, "Client", "Server", "client_storage", NULL, NULL);
    if (OFIX_OK != err.code || NULL == client) {
	test_print("Failed to create client [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    // TBD func to wait for server to be ready
    sleep(1);
    ofix_session_connect(&err, client, "localhost", 6161);
    for (; '\0' != *c; c++, b++) {
	if ('^' == *c) {
	    *b = '\1';
	} else {
	    *b = *c;
	}
    }
    *b = '\0';
    msg = ofix_msg_parse(&err, buf, strlen(buf));

    ofix_session_send(&err, client, msg);
    ofix_session_send(&err, client, msg);
    ofix_session_send(&err, client, msg);
    // TBD wait for logon to complete, maybe when connect completes?

    sleep(4);

    ofix_session_destroy(&err, client);
    ofix_engine_destroy(&err, server);
}

void
append_engine_tests(Test tests) {
    system("rm -rf server_storage"); // clear out old results
    system("rm -rf client_storage"); // clear out old results
    test_append(tests, "engine.logon", logon_test);
}
