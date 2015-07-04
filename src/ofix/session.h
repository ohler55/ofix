// Copyright 2009 by Peter Ohler, All Rights Reserved

#ifndef __OFIX_SESSION_H__
#define __OFIX_SESSION_H__

#include <stdint.h>

#include "msg.h"
#include "role.h"

struct _ofixEngine;

/**
 * @file session.h
 *
 * This file defines the functions used to work with a FIX session.
 */

/**
 * The ofixSession structure is the representation of a FIX engine for the
 * library.
 */
typedef struct _ofixSession	*ofixSession;

/**
 * This type is used for receiving message callbacks.
 */
typedef bool	(*ofixRecvCallback)(ofixSession session, ofixMsg msg, void *ctx);

extern ofixSession	ofix_session_create(ofixErr err,
					    const char *cid,
					    const char *sid,
					    const char *store_path,
					    ofixRecvCallback cb, void *ctx);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_session_destroy(ofixErr err, ofixSession session);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_session_connect(ofixErr err, ofixSession session, const char *host, int port);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_session_send(ofixErr err, ofixSession session, ofixMsg msg);

/**
 *
 *
 * @param err pointer to error struct or NULL
 *
 * @return Returns a sent message or NULL on error or not found.
 */
extern ofixMsg	ofix_session_get_msg(ofixErr err, ofixSession session, int64_t seq_num);

extern int64_t	ofix_session_send_seqnum(ofixSession session);
extern int64_t	ofix_session_recv_seqnum(ofixSession session);

#endif /* __OFIX_SESSION_H__ */
