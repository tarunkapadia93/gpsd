/****************************************************************************

NAME
   shmexport.c - shared-memory export from the daemon

DESCRIPTION
   This is a very lightweight alternative to JSON-over-sockets.  Clients
won't be able to filter by device, and won't get device activation/deactivation
notifications.  But both client and daemon will avoid all the marshalling and
unmarshalling overhead.

PERMISSIONS
   This file is Copyright 2010 by the GPSD project
   SPDX-License-Identifier: BSD-2-clause

***************************************************************************/

#include "gpsd_config.h"

#ifdef SHM_EXPORT_ENABLE

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include "gpsd.h"
#include "libgps.h" /* for SHM_PSEUDO_FD */

/* initialize the shared-memory segment to be used for export */
bool shm_acquire(struct gps_context_t *context)
{
    long shmkey = getenv("GPSD_SHM_KEY") ? \
                      strtol(getenv("GPSD_SHM_KEY"), NULL, 0) : GPSD_SHM_KEY;

    int shmid = shmget((key_t)shmkey, sizeof(struct shmexport_t),
                       (int)(IPC_CREAT|0666));
    if (-1 == shmid) {
        GPSD_LOG(LOG_ERROR, &context->errout,
                 "shmget(0x%lx, %zd, 0666) for SHM export failed: %s\n",
                 shmkey,
                 sizeof(struct shmexport_t),
                 strerror(errno));
        return false;
    }

    GPSD_LOG(LOG_PROG, &context->errout,
             "shmget(0x%lx, %zd, 0666) for SHM export succeeded\n",
             shmkey,
             sizeof(struct shmexport_t));

    context->shmexport = (void *)shmat(shmid, 0, 0);
    if ((void *) -1 == context->shmexport) {
        GPSD_LOG(LOG_ERROR, &context->errout,
                 "shmat failed: %s\n", strerror(errno));
        context->shmexport = NULL;
        return false;
    }
    context->shmid = shmid;

    GPSD_LOG(LOG_PROG, &context->errout,
             "shmat() for SHM export succeeded, segment %d\n", shmid);
    return true;
}

// release the shared-memory segment used for export
void shm_release(struct gps_context_t *context)
{
    if (NULL == context->shmexport)
        return;

    /* Mark shmid to go away when no longer used
     * Having it linger forever is bad, and when the size enlarges
     * it can no longer be opened
     */
    if (-1 == shmctl(context->shmid, IPC_RMID, NULL)) {
        GPSD_LOG(LOG_WARN, &context->errout,
                 "shmctl(%d) for IPC_RMID failed, %s(%d)\n",
                 context->shmid, strerror(errno), errno);
    }
    if (-1 == shmdt((const void *)context->shmexport)) {
        GPSD_LOG(LOG_WARN, &context->errout,
                 "shmdt) for id %d, %s(%d)\n",
                 context->shmid, strerror(errno), errno);
    }
}

/* export an update to all listeners */
void shm_update(struct gps_context_t *context, struct gps_data_t *gpsdata)
{
    if (NULL != context->shmexport) {
        static int tick;
        volatile struct shmexport_t *shared = \
                            (struct shmexport_t *)context->shmexport;

        ++tick;
        /*
         * Following block of instructions must not be reordered, otherwise
         * havoc will ensue.
         *
         * This is a simple optimistic-concurrency technique.  We write
         * the second bookend first, then the data, then the first bookend.
         * Reader copies what it sees in normal order; that way, if we
         * start to write the segment during the read, the second bookend will
         * get clobbered first and the data can be detected as bad.
         *
         * Of course many architectures, like Intel, make no guarantees
         * about the actual memory read or write order into RAM, so this
         * is partly wishful thinking.  Thus the need for the memory_barriers()
         * to enforce the required order.
         */
        shared->bookend2 = tick;
        memory_barrier();
        shared->gpsdata = *gpsdata;
        memory_barrier();
#ifndef USE_QT
        shared->gpsdata.gps_fd = SHM_PSEUDO_FD;
#else
        shared->gpsdata.gps_fd = (void *)(intptr_t)SHM_PSEUDO_FD;
#endif /* USE_QT */
        memory_barrier();
        shared->bookend1 = tick;
    }
}


#endif /* SHM_EXPORT_ENABLE */

/* end */
// vim: set expandtab shiftwidth=4
