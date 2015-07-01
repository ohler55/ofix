// Copyright 2009 by Peter Ohler, All Rights Reserved

#ifndef __OFIX_ENGINE_H__
#define __OFIX_ENGINE_H__

#include <stdbool.h>
#include <stdint.h>

#include "err.h"
#include "session.h"

/**
 * @file engine.h
 *
 * This file defines the functions used to create and work with a FIX engine.
 */

/**
 * The ofixEngine structure is the representation of a FIX engine for the
 * library.
 */
typedef struct _ofixEngine	*ofixEngine;
typedef struct _ofixEngSession	*ofixEngSession;

/**
 * This type is used for receiving message callbacks.
 */
typedef bool	(*ofixEngRecvCallback)(ofixEngSession session, ofixMsg msg, void *ctx);

/**
 *
 *
 * @param err pointer to error struct or NULL
 *
 * @return Returns a new engine or NULL on error.
 */
extern ofixEngine	ofix_engine_create(ofixErr err,
					   const char *id,
					   int port,
					   const char *auth_file,
					   const char *store_dir,
					   int heartbeat_interval);

extern void		ofix_engine_on_recv(ofixEngine eng, ofixEngRecvCallback cb, void *ctx);
extern const char*	ofix_engine_id(ofixEngine eng);
extern const char*	ofix_engine_ipaddr(ofixEngine eng);
extern const char*	ofix_engine_auth_file(ofixEngine eng);
extern const char*	ofix_engine_store_dir(ofixEngine eng);
extern int		ofix_engine_heartbeat_interval(ofixEngine eng);
extern int		ofix_engine_port(ofixEngine eng);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void		ofix_engine_start(ofixErr err, ofixEngine eng);

/**
 *
 *
 * @param err pointer to error struct or NULL
 *
 * @return Returns a new session or NULL on error.
 */
extern ofixEngSession	ofix_engine_get_session(ofixErr err, ofixEngine eng, const char *cid);

extern void		ofix_engine_send(ofixErr err, ofixEngSession session, ofixMsg msg);

extern bool		ofix_engine_running(ofixEngine eng);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void		ofix_engine_destroy(ofixErr err, ofixEngine eng);

extern int64_t		ofix_engine_send_seqnum(ofixEngSession session);
extern int64_t		ofix_engine_recv_seqnum(ofixEngSession session);

#endif /* __OFIX_ENGINE_H__ */
