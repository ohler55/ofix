// Copyright 2009 by Peter Ohler, All Rights Reserved

#ifndef __OFIX_CLIENT_H__
#define __OFIX_CLIENT_H__

#include <stdint.h>

#include "msg.h"
#include "role.h"

struct _ofixEngine;

/**
 * @file client.h
 *
 * This file defines the functions used to work with a FIX client.
 */

/**
 * The ofixClient structure is the representation of a FIX engine for the
 * library.
 */
typedef struct _ofixClient	*ofixClient;

extern ofixClient	ofix_client_create(ofixErr err,
					    const char *cid,
					    const char *sid,
					    const char *store_path,
					    ofixRecvCallback cb, void *ctx);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_client_destroy(ofixErr err, ofixClient client);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_client_connect(ofixErr err, ofixClient client, const char *host, int port, double timeout);

/**
 *
 *
 * @param err pointer to error struct or NULL
 */
extern void	ofix_client_send(ofixErr err, ofixClient client, ofixMsg msg);

/**
 *
 *
 * @param err pointer to error struct or NULL
 *
 * @return Returns a sent message or NULL on error or not found.
 */
extern ofixMsg	ofix_client_get_msg(ofixErr err, ofixClient client, int64_t seq_num);

extern int64_t	ofix_client_send_seqnum(ofixClient client);
extern int64_t	ofix_client_recv_seqnum(ofixClient client);

#endif /* __OFIX_CLIENT_H__ */
