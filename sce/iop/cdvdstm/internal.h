#ifndef SCE_IOP_CDVDSTM_INTERNAL_H
#define SCE_IOP_CDVDSTM_INTERNAL_H

#include "sce/iop/cdvdstm/cdvdstm.h"
#include "sce/iop/intrman/intrman.h"
#include "sce/iop/ioman/ioman.h"
#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/sifman/sif.h"
#include "sce/iop/stdio/stdio.h"
#include "sce/iop/sysmem/sysmem.h"
#include "sce/iop/thbase/thread.h"
#include "sce/iop/thevent/thevent.h"
#include "sce/iop/thsemap/thsemap.h"

#define CDVDSTM_CLOCKS_PER_MSEC 36864
#define CDVDSTM_TIMEOUT_RETRY (15000 * CDVDSTM_CLOCKS_PER_MSEC)
#define CDVDSTM_POLL_FULL (8 * CDVDSTM_CLOCKS_PER_MSEC)
#define CDVDSTM_POLL_BUSY (200 * CDVDSTM_CLOCKS_PER_MSEC)
#define CDVDSTM_DMA_RETRY_USEC 500
#define CDVDSTM_RETRY_COUNT 3
#define CDVDSTM_RETRY_SECTOR_STEP 16
#define CDVDSTM_DRIVER_ERROR (-5)

#define CDVD_SC_STATUS (-1)
#define CDVD_SC_SET_ERROR (-2)
#define CDVD_SC_EVENT_ID (-11)
#define CDVD_SC_TIMEOUT (-17)
#define CDVD_SC_TIMEOUT_OCCURRED (-18)
#define CDVD_STREAM_DISABLED 0
#define CDVD_STREAM_IOP 1
#define CDVD_STREAM_EE 2

extern int cdvdstm_debug;
extern unsigned int cdvdstm_stub[];
void cdvdstm_ee_stream(struct CdvdStreamRequest* request, int size, void* result);
unsigned int cdvdstm_copy(void* destination, const void* source, unsigned int size);

#endif
