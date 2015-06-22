// Copyright 2009 by Peter Ohler, All Rights Reserved

#ifndef __OFIX_ERR_H__
#define __OFIX_ERR_H__

#define OFIX_ERR_INIT	{ 0, { 0 } }

/**
 * Error codes for the __code__ field in __ofixErr__ structs.
 * @see ofixErr
 */
typedef enum {
    /** okay, no error */
    OFIX_OK		= 0,
    /** parse error */
    OFIX_PARSE_ERR	= 'p',
    /** buffer overflow error */
    OFIX_OVERFLOW_ERR	= 'o',
    /** write error */
    OFIX_WRITE_ERR	= 'w',
    /** memory error */
    OFIX_MEMORY_ERR	= 'm',
    /** argument error */
    OFIX_ARG_ERR	= 'a',
    /** not found */
    OFIX_NOT_FOUND_ERR	= 'f',
    /** read error */
    OFIX_READ_ERR	= 'r',
    /** denied */
    OFIX_DENIED_ERR	= 'd',
    /** network error */
    OFIX_NETWORK_ERR	= 'n',
    /** thread error */
    OFIX_THREAD_ERR	= 't',
} ofixErrCode;

/**
 * The struct used to report errors or status after a function returns. The
 * struct must be initialized before use as most calls that take an err argument
 * will return immediately if an error has already occurred.
 *
 * @see ofixErrCode
 */
typedef struct _ofixErr {
    /** Error code identifying the type of error. */
    int		code;
    /** Error message associated with a failure if the code is not __OFIX_OK__. */
    char	msg[256];
} *ofixErr;

static inline void ofix_err_clear(ofixErr err) {
    err->code = OFIX_OK;
    *err->msg = '\0';
}

#endif /* __OFIX_ERR_H__ */
