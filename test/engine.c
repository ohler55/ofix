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
#include "ofix/client.h"
#include "ofix/dtime.h"
#include "ofix/engine.h"
#include "ofix/err.h"
#include "ofix/msg.h"
#include "ofix/role.h"
#include "ofix/store.h"
#include "ofix/tag.h"
#include "ofix/versionspec.h"

extern ofixVersionSpec	ofix_get_spec(ofixErr err, int major, int minor);
extern void		_ofix_client_raw_send(ofixErr err, ofixClient client, ofixMsg msg);

static int		xid_cnt = 0;
static const char	*client_storage = "client_storage.fix";

static char*
load_fix_file(const char *filename) {
    char	*contents = test_load_file(filename);

    for (char *s = contents; '\0' != *s; s++) {
	if ('\1' == *s) {
	    *s = '^';
	}
    }
    return contents;
}

static bool
log_on(void *ctx, ofixLogLevel level) {
    return false;
}

static void
log(void *ctx, ofixLogLevel level, const char *format, ...) {
    if (log_on(ctx, level)) {
	va_list	ap;
    
	va_start(ap, format);
	vfprintf((FILE*)ctx, format, ap);
	fputc('\n', (FILE*)ctx);
	va_end(ap);
    }
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

    //printf("*** client callback: %s\n", s);
    free(s);
    return true;
}

static void
run_test(ofixMsg *msgs, bool raw, int server_seq, int port) {
    struct _ofixErr	err = OFIX_ERR_INIT;
    const char		*client_storage = "client_storage.fix";
    ofixVersionSpec	vspec = ofix_get_spec(&err, 4, 4);
    ofixEngine		server = ofix_engine_create(&err, "Server", port, NULL, "server_storage", vspec, 0);
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
    ofix_engine_set_log(server, log_on, log, stdout);

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
    ofix_client_set_log(client, log_on, log, stdout);

    // wait for engine to start
    giveup = dtime() + 1.0;
    while (!ofix_engine_running(server)) {
	if (giveup < dtime()) {
	    test_print("Timed out waiting for engine to start.\n");
	    test_fail();
	    return;
	}
    }

    ofix_client_connect(&err, client, "localhost", port, 1.0);
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
    for (; NULL != *msgs; msgs++) {
	if (raw) {
	    _ofix_client_raw_send(&err, client, *msgs);
	} else {
	    ofix_client_send(&err, client, *msgs);
	}
    }

    // wait for exchanges to complete
    giveup = dtime() + 1.0;
    while (server_seq > ofix_client_recv_seqnum(client)) {
	if (giveup < dtime()) {
	    test_print("Timed out waiting for client to receive responses.\n");
	    test_fail();
	    return;
	}
    }

    ofix_client_logout(&err, client, "bye bye");

    ofix_client_destroy(&err, client);
    ofix_engine_destroy(&err, server);
}

static void
normal_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixMsgSpec		spec = ofix_version_spec_get_msg_spec(&err, "D", 4, 4);
    ofixMsg		msg1;
    ofixMsg		msg2;
    ofixMsg		msgs[3];
    const char		*s;
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;
    char		*actual;

    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    
    // Create an single order message.
    // First get the message spec.
    if (OFIX_OK != err.code || NULL == spec) {
	test_print("Failed to find message spec for 'D' [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msg1 = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == msg1) {
	test_print("Failed to create message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg1, OFIX_ClOrdIDTAG, "order-123");
    ofix_msg_set_str(&err, msg1, OFIX_SymbolTAG, "IBM");
    ofix_msg_set_char(&err, msg1, OFIX_SideTAG, '1'); // buy
    ofix_msg_set_int(&err, msg1, OFIX_OrderQtyTAG, 250);
    ofix_msg_set_date(&err, msg1, OFIX_TransactTimeTAG, &now);
    ofix_msg_set_char(&err, msg1, OFIX_OrdTypeTAG, '1'); // market order
    if (OFIX_OK != err.code) {
	test_print("Error while setting fields in message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }

    if (NULL == (s = ofix_msg_FIX_str(&err, msg1)) ||
	NULL == (msg2 = ofix_msg_parse(&err, s, strlen(s))) ||
	OFIX_OK != err.code) {
	test_print("Error cloning message.\n");
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg2, OFIX_ClOrdIDTAG, "order-124");
    msgs[0] = msg1;
    msgs[1] = msg2;
    msgs[2] = NULL;

    run_test(msgs, false, 3, 6161);

    actual = load_fix_file(client_storage);
    test_same("sender: Client\n\
\n\
8=FIX.4.4^9=073^35=A^49=Client^56=Server^34=1^52=$-$:$:$.$^98=0^108=30^141=Y^10=$^\n\
8=FIX.4.4^9=067^35=A^49=Server^56=Client^34=1^52=$-$:$:$.$^98=0^108=30^10=$^\n\
8=FIX.4.4^9=117^35=D^49=Client^56=Server^34=2^52=$-$:$:$.$^11=order-123^55=IBM^54=1^60=$-$:$:$.$^38=250^40=1^10=$^\n\
8=FIX.4.4^9=117^35=D^49=Client^56=Server^34=3^52=$-$:$:$.$^11=order-124^55=IBM^54=1^60=$-$:$:$.$^38=250^40=1^10=$^\n\
8=FIX.4.4^9=117^35=8^49=Server^56=Client^34=2^52=$-$:$:$.$^37=order-123^17=x-1^150=0^39=0^55=IBM^54=1^151=250^14=250^6=0^10=$^\n\
8=FIX.4.4^9=117^35=8^49=Server^56=Client^34=3^52=$-$:$:$.$^37=order-124^17=x-2^150=0^39=0^55=IBM^54=1^151=250^14=250^6=0^10=$^\n\
8=FIX.4.4^9=066^35=5^49=Client^56=Server^34=4^52=$-$:$:$.$^58=bye bye^10=$^\n\
8=FIX.4.4^9=055^35=5^49=Server^56=Client^34=4^52=$-$:$:$.$^10=$^\n",
	      actual);
    free(actual);
}

static void
bad_sender_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixMsgSpec		spec = ofix_version_spec_get_msg_spec(&err, "D", 4, 4);
    ofixMsg		msg1;
    ofixMsg		msgs[2];
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;
    char		*actual;

    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    
    // Create an single order message.
    // First get the message spec.
    if (OFIX_OK != err.code || NULL == spec) {
	test_print("Failed to find message spec for 'D' [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msg1 = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == msg1) {
	test_print("Failed to create message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg1, OFIX_SenderCompIDTAG, "Bad");
    ofix_msg_set_str(&err, msg1, OFIX_TargetCompIDTAG, "Server");
    ofix_msg_set_int(&err, msg1, OFIX_MsgSeqNumTAG, 2);

    ofix_msg_set_str(&err, msg1, OFIX_ClOrdIDTAG, "order-123");
    ofix_msg_set_str(&err, msg1, OFIX_SymbolTAG, "IBM");
    ofix_msg_set_char(&err, msg1, OFIX_SideTAG, '1'); // buy
    ofix_msg_set_int(&err, msg1, OFIX_OrderQtyTAG, 250);
    ofix_msg_set_date(&err, msg1, OFIX_TransactTimeTAG, &now);
    ofix_msg_set_char(&err, msg1, OFIX_OrdTypeTAG, '1'); // market order
    if (OFIX_OK != err.code) {
	test_print("Error while setting fields in message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msgs[0] = msg1;
    msgs[1] = NULL;

    run_test(msgs, true, 3, 6162);

    actual = load_fix_file(client_storage);
    test_same("sender: Client\n\
\n\
8=FIX.4.4^9=073^35=A^49=Client^56=Server^34=1^52=$-$:$:$.$^98=0^108=30^141=Y^10=$^\n\
8=FIX.4.4^9=067^35=A^49=Server^56=Client^34=1^52=$-$:$:$.$^98=0^108=30^10=$^\n\
8=FIX.4.4^9=114^35=D^49=Bad^56=Server^34=2^52=$-$:$:$.$^11=order-123^55=IBM^54=1^60=$-$:$:$.$^38=250^40=1^10=$^\n\
8=FIX.4.4^9=127^35=3^49=Server^56=Client^34=2^52=$-$:$:$.$^45=2^371=49^372=D^373=9^58=Expected sender of 'Client'. Received 'Bad'.^10=$^\n\
8=FIX.4.4^9=103^35=5^49=Server^56=Client^34=3^52=$-$:$:$.$^58=Expected sender of 'Client'. Received 'Bad'.^10=$^\n\
8=FIX.4.4^9=055^35=5^49=Client^56=Server^34=3^52=$-$:$:$.$^10=$^\n",
	      actual);
    free(actual);
}

static void
bad_target_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixMsgSpec		spec = ofix_version_spec_get_msg_spec(&err, "D", 4, 4);
    ofixMsg		msg1;
    ofixMsg		msgs[2];
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;
    char		*actual;

    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    
    // Create an single order message.
    // First get the message spec.
    if (OFIX_OK != err.code || NULL == spec) {
	test_print("Failed to find message spec for 'D' [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msg1 = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == msg1) {
	test_print("Failed to create message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg1, OFIX_SenderCompIDTAG, "Client");
    ofix_msg_set_str(&err, msg1, OFIX_TargetCompIDTAG, "Bad");
    ofix_msg_set_int(&err, msg1, OFIX_MsgSeqNumTAG, 2);

    ofix_msg_set_str(&err, msg1, OFIX_ClOrdIDTAG, "order-123");
    ofix_msg_set_str(&err, msg1, OFIX_SymbolTAG, "IBM");
    ofix_msg_set_char(&err, msg1, OFIX_SideTAG, '1'); // buy
    ofix_msg_set_int(&err, msg1, OFIX_OrderQtyTAG, 250);
    ofix_msg_set_date(&err, msg1, OFIX_TransactTimeTAG, &now);
    ofix_msg_set_char(&err, msg1, OFIX_OrdTypeTAG, '1'); // market order
    if (OFIX_OK != err.code) {
	test_print("Error while setting fields in message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msgs[0] = msg1;
    msgs[1] = NULL;

    run_test(msgs, true, 3, 6163);

    actual = load_fix_file(client_storage);
    test_same("sender: Client\n\
\n\
8=FIX.4.4^9=073^35=A^49=Client^56=Server^34=1^52=$-$:$:$.$^98=0^108=30^141=Y^10=$^\n\
8=FIX.4.4^9=067^35=A^49=Server^56=Client^34=1^52=$-$:$:$.$^98=0^108=30^10=$^\n\
8=FIX.4.4^9=114^35=D^49=Client^56=Bad^34=2^52=$-$:$:$.$^11=order-123^55=IBM^54=1^60=$-$:$:$.$^38=250^40=1^10=$^\n\
8=FIX.4.4^9=127^35=3^49=Server^56=Client^34=2^52=$-$:$:$.$^45=2^371=56^372=D^373=9^58=Expected target of 'Server'. Received 'Bad'.^10=$^\n\
8=FIX.4.4^9=103^35=5^49=Server^56=Client^34=3^52=$-$:$:$.$^58=Expected target of 'Server'. Received 'Bad'.^10=$^\n\
8=FIX.4.4^9=055^35=5^49=Client^56=Server^34=3^52=$-$:$:$.$^10=$^\n",
	      actual);
    free(actual);
}

static void
bad_msgtype_test() {
    struct _ofixErr	err = OFIX_ERR_INIT;
    ofixMsgSpec		spec = ofix_version_spec_get_msg_spec(&err, "D", 4, 4);
    ofixMsg		msg1;
    ofixMsg		msgs[2];
    struct timeval	tv;
    struct timezone	tz;
    struct _ofixDate	now;
    char		*actual;

    gettimeofday(&tv, &tz);
    ofix_date_set_timestamp(&now, (uint64_t)tv.tv_sec * 1000000LL + (uint64_t)tv.tv_usec);
    
    // Create an single order message.
    // First get the message spec.
    if (OFIX_OK != err.code || NULL == spec) {
	test_print("Failed to find message spec for 'D' [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msg1 = ofix_msg_create_from_spec(&err, spec, 16);
    if (OFIX_OK != err.code || NULL == msg1) {
	test_print("Failed to create message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    ofix_msg_set_str(&err, msg1, OFIX_MsgTypeTAG, "BAD");
    ofix_msg_set_str(&err, msg1, OFIX_SenderCompIDTAG, "Client");
    ofix_msg_set_str(&err, msg1, OFIX_TargetCompIDTAG, "Server");
    ofix_msg_set_int(&err, msg1, OFIX_MsgSeqNumTAG, 2);

    ofix_msg_set_str(&err, msg1, OFIX_ClOrdIDTAG, "order-123");
    ofix_msg_set_str(&err, msg1, OFIX_SymbolTAG, "IBM");
    ofix_msg_set_char(&err, msg1, OFIX_SideTAG, '1'); // buy
    ofix_msg_set_int(&err, msg1, OFIX_OrderQtyTAG, 250);
    ofix_msg_set_date(&err, msg1, OFIX_TransactTimeTAG, &now);
    ofix_msg_set_char(&err, msg1, OFIX_OrdTypeTAG, '1'); // market order
    if (OFIX_OK != err.code) {
	test_print("Error while setting fields in message [%d] %s\n", err.code, err.msg);
	test_fail();
	return;
    }
    msgs[0] = msg1;
    msgs[1] = NULL;

    run_test(msgs, true, 3, 6164);

    actual = load_fix_file(client_storage);
    test_same("sender: Client\n\
\n\
8=FIX.4.4^9=073^35=A^49=Client^56=Server^34=1^52=$-$:$:$.$^98=0^108=30^141=Y^10=$^\n\
8=FIX.4.4^9=067^35=A^49=Server^56=Client^34=1^52=$-$:$:$.$^98=0^108=30^10=$^\n\
8=FIX.4.4^9=119^35=BAD^49=Client^56=Server^34=2^52=$-$:$:$.$^11=order-123^55=IBM^54=1^60=$-$:$:$.$^38=250^40=1^10=$^\n\
8=FIX.4.4^9=120^35=3^49=Server^56=Client^34=2^52=$-$:$:$.$^45=2^373=0^58=FIX specification for BAD in version 4.4 not found^10=$^\n\
8=FIX.4.4^9=109^35=5^49=Server^56=Client^34=3^52=$-$:$:$.$^58=FIX specification for BAD in version 4.4 not found^10=$^\n\
8=FIX.4.4^9=055^35=5^49=Client^56=Server^34=3^52=$-$:$:$.$^10=$^\n",
	      actual);
    free(actual);
}

void
append_engine_tests(Test tests) {
    test_append(tests, "engine.normal", normal_test);
    test_append(tests, "engine.bad_sender", bad_sender_test);
    test_append(tests, "engine.bad_target", bad_target_test);
    test_append(tests, "engine.bad_msgtype", bad_msgtype_test);
    // TBD seq number errors, duplicate with and without pos dup, out of sequence in past and future
}
