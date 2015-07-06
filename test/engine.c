// Copyright 2009 by Peter Ohler, All Rights Reserved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

#include "test.h"
#include "ofix/dtime.h"
#include "ofix/store.h"
#include "ofix/engine.h"
#include "ofix/client.h"
#include "ofix/msg.h"
#include "ofix/role.h"
#include "ofix/tag.h"
#include "ofix/versionspec.h"

extern ofixVersionSpec	ofix_get_spec(ofixErr err, int major, int minor);

static int	xid_cnt = 0;

static void
log_same(const char *log, const char *expect) {
    FILE	*f = fopen(log, "r");
    long	size = 0;
    char	*contents;

    if (NULL == f) {
	test_print("Failed to open log file '%s'\n", log);
	test_fail();
	return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (0 >= size || NULL == (contents = (char*)malloc(size + 1))) {
	test_print("'%s' was empty\n", log);
	test_fail();
	fclose(f);
	return;
    }
    if (size != fread(contents, 1, size, f)) {
	test_print("Failed to read '%s'. %s\n", log, strerror(errno));
	test_fail();
	fclose(f);
	return;
    }
    contents[size] = '\0';
    fclose(f);
    for (char *c = contents; '\0' != *c; c++) {
	if ('\1' == *c) {
	    *c = '^';
	}
    }
    test_same(expect, contents);
}

static void*
start_engine(void *arg) {
    struct _ofixErr	err = OFIX_ERR_INIT;

    ofix_engine_start(&err, (ofixEngine)arg);

    return NULL;
}

static bool
server_cb(ofixSession session, ofixMsg msg, void *ctx) {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixMsgSpec		spec;
    ofixMsg		reply;
    char		*s;
    char		xid[16];
    int64_t		qty;

    spec = ofix_version_spec_get_msg_spec(&err, "8", 4, 4);
    if (OFIX_OK != err.code || NULL == spec) {
	printf("Failed to find message spec for '8' [%d] %s\n", err.code, err.msg);
	return true;
    }
    reply = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == reply) {
	printf("Failed to create message [%d] %s\n", err.code, err.msg);
	return true;
    }
    s = ofix_msg_get_str(&err, msg, OFIX_ClOrdIDTAG);
    ofix_msg_set_str(&err, reply, OFIX_OrderIDTAG, s);
    free(s);
    s = ofix_msg_get_str(&err, msg, OFIX_SymbolTAG);
    ofix_msg_set_str(&err, reply, OFIX_SymbolTAG, s);
    free(s);
    ofix_msg_set_char(&err, reply, OFIX_SideTAG, ofix_msg_get_char(&err, msg, OFIX_SideTAG));
    sprintf(xid, "x-%d", ++xid_cnt);
    ofix_msg_set_str(&err, reply, OFIX_ExecIDTAG, xid);
    ofix_msg_set_char(&err, reply, OFIX_ExecTypeTAG, '0');
    ofix_msg_set_char(&err, reply, OFIX_OrdStatusTAG, '0');
    qty = ofix_msg_get_int(&err, msg, OFIX_OrderQtyTAG);
    ofix_msg_set_int(&err, reply, OFIX_LeavesQtyTAG, qty);
    ofix_msg_set_int(&err, reply, OFIX_CumQtyTAG, qty);
    ofix_msg_set_float(&err, reply, OFIX_AvgPxTAG, 0.0, 4);

    if (OFIX_OK != err.code) {
	printf("Error setting field values [%d] %s\n", err.code, err.msg);
	return true;
    }
    ofix_session_send(&err, session, reply);

    return true;
}

static bool
client_cb(ofixSession session, ofixMsg msg, void *ctx) {
    struct _ofixErr	err = OFIX_ERR_INIT;
    char		*s = ofix_msg_to_str(&err, msg);

    printf("*** client callback: %s\n", s);
    free(s);
    return true;
}

static void
normal_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    const char		*client_storage = "client_storage.fix";
    ofixVersionSpec	vspec = ofix_get_spec(&err, 4, 4);
    ofixEngine		server = ofix_engine_create(&err, "Server", 6161, NULL, "server_storage", vspec, 0);
    pthread_t		server_thread;
    ofixClient		client;
    ofixSession		server_session;
    ofixMsgSpec		spec;
    ofixMsg		msg;
    double		giveup;
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;

    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);

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

    client = ofix_client_create(&err, "Client", "Server", client_storage, vspec, client_cb, NULL);
    if (OFIX_OK != err.code || NULL == client) {
	test_print("Failed to create client [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    // wait for engine to start
    giveup = dtime() + 1.0;
    while (!ofix_engine_running(server)) {
	if (giveup < dtime()) {
	    test_print("Timed out waiting for engine to start.\n");
	    test_fail();
	    return;
	}
    }

    ofix_client_connect(&err, client, "localhost", 6161, 1.0);
    if (OFIX_OK != err.code) {
	test_print("Connect failed [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    
    server_session = ofix_engine_get_session(&err, server, "Client");
    if (OFIX_OK != err.code || NULL == server_session) {
	test_print("Failed to find server session [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    
    // Create an single order message.
    // First get the message spec.
    spec = ofix_version_spec_get_msg_spec(&err, "D", 4, 4);
    if (OFIX_OK != err.code || NULL == spec) {
	test_print("Failed to find message spec for 'D' [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msg = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == msg) {
	test_print("Failed to create message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg, OFIX_ClOrdIDTAG, "order-123");
    ofix_msg_set_str(&err, msg, OFIX_SymbolTAG, "IBM");
    ofix_msg_set_char(&err, msg, OFIX_SideTAG, '1'); // buy
    ofix_msg_set_int(&err, msg, OFIX_OrderQtyTAG, 250);
    ofix_msg_set_date(&err, msg, OFIX_TransactTimeTAG, &now);
    ofix_msg_set_char(&err, msg, OFIX_OrdTypeTAG, '1'); // market order
    if (OFIX_OK != err.code) {
	test_print("Error while setting fields in message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }

    ofix_client_send(&err, client, msg);
    ofix_msg_set_str(&err, msg, OFIX_ClOrdIDTAG, "order-124");
    ofix_client_send(&err, client, msg);

    // wait for exchanges to complete
    giveup = dtime() + 1.0;
    while (3 > ofix_client_recv_seqnum(client)) {
	if (giveup < dtime()) {
	    test_print("Timed out waiting for client to receive responses.\n");
	    test_fail();
	    return;
	}
    }

    // TBD Logout

    ofix_client_destroy(&err, client);
    ofix_engine_destroy(&err, server);

    log_same(client_storage,
	     "zzzzzz");
}

void
append_engine_tests(Test tests) {
    system("rm -rf server_storage"); // clear out old results
    system("rm -rf client_storage"); // clear out old results
    test_append(tests, "engine.normal", normal_test);
}
