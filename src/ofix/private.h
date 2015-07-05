// Copyright 2015 by Peter Ohler, All Rights Reserved

#ifndef __OFIX_PRIVATE_H__
#define __OFIX_PRIVATE_H__

#include <pthread.h>
#include <stdint.h>

#include "store.h"
#include "session.h"

struct _ofixSession {
    char		*sid; // sender ID
    char		*tid; // target ID
    int64_t		sent_seq; // last sent sequence number
    int64_t		recv_seq; // last recieved sequence number
    Store		store;
    int			sock;
    ofixRecvCallback	recv_cb;
    void		*recv_ctx;
    int			heartbeat_interval;
    bool		done;
    bool		closed;
    pthread_t		thread;
    pthread_mutex_t	send_mutex;
};

extern void	_ofix_session_init(ofixErr err,
				   ofixSession s,
				   const char *sid,
				   const char *tid,
				   const char *store_path,
				   ofixRecvCallback cb,
				   void *ctx);

extern void	_ofix_session_free(ofixSession session);
extern void	_ofix_session_start(ofixErr err, ofixSession session);

extern uint32_t	_ofix_net_addr(ofixErr err, const char *host);


#endif /* __OFIX_PRIVATE_H__ */
