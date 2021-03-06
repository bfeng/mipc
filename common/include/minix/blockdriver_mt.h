#ifndef _MINIX_BLOCKDRIVER_MT_H
#define _MINIX_BLOCKDRIVER_MT_H

#define BLOCKDRIVER_MT_API 1	/* do not expose the singlethreaded API */
#include <minix/blockdriver.h>

/* The maximum number of worker threads. */
#define BLOCKDRIVER_MT_MAX_WORKERS	32

_PROTOTYPE( void blockdriver_mt_task, (struct blockdriver *driver_tab) );
_PROTOTYPE( void blockdriver_mt_sleep, (void) );
_PROTOTYPE( void blockdriver_mt_wakeup, (thread_id_t id) );
_PROTOTYPE( void blockdriver_mt_stop, (void) );
_PROTOTYPE( void blockdriver_mt_terminate, (void) );

#endif /* _MINIX_BLOCKDRIVER_MT_H */
