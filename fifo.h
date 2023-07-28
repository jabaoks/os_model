
#ifndef _FIFO_H_
#define _FIFO_H_

#include "rtos.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define USE_ATOMIC_MEM __ATOMIC_SEQ_CST

#define ATOMIC_UINT volatile unsigned int

typedef struct
{
    unsigned int limit;
    ATOMIC_UINT size;
    ATOMIC_UINT rdIdx;
    ATOMIC_UINT rd_size;
    ATOMIC_UINT wrIdx;
    ATOMIC_UINT wr_cnt;
    ATOMIC_UINT wr_size;
#ifndef USE_ATOMIC_MEM
    SYS_PMUTEX *mutex;
#endif
    // for integrity check
    short id;
    volatile unsigned short overflow_cnt;
} Fifo;

unsigned int fifo_InsBlock(Fifo *fifo, const void *pData, unsigned int len);
unsigned int fifo_InsBlocks(Fifo *fifo, const void **pData, const unsigned int *plen, int cnt);
unsigned int fifo_ExtrBlock(Fifo *fifo, void *pBuf, unsigned int len);
void fifo_InitFifo(Fifo *fifo, void *buf, unsigned int size);
unsigned int fifo_GetDataLen(const Fifo *fifo);
unsigned int fifo_GetFreeLen(const Fifo *fifo);
unsigned int fifo_ExtrBlock_Box(Fifo *fifo, void *pBuf, unsigned int len);
unsigned int fifo_InsBlock_Box(Fifo *fifo, const void *pData, unsigned int len);

#ifdef __cplusplus
}
#endif
#endif  // _FIFO_H_
