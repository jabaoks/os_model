/*
 * fifo.c
 */

#include "rtos.h"
#include "fifo.h"
#include <string.h>

#define FIFO_DEB 1

#define cpmem memcpy
#define zmem(p,sz) memset((p),0,(sz))
#define MutexLock pthread_mutex_lock
#define MutexUnlock pthread_mutex_unlock
#define MutexAdd(m) pthread_mutex_init((m), 0)
#define terminateA os_printf

#define ATOMIC_PTR (volatile unsigned int *)

#define CACHE_FLUSH(a,b,c)
#define CACHE_INVALIDATE(a,b,c)

static unsigned int atomic_inc(volatile unsigned int *pmem, int delta)
{
    unsigned int t = *pmem;
    *pmem += delta;
    return t;
}

static unsigned int atomic_inc_lim(volatile unsigned int *pmem, int delta, unsigned int limit)
{
    unsigned int t = *pmem;
    *pmem += delta;
    if(*pmem >= limit)
        *pmem -= limit;
    return t;
}

static unsigned int atomic_set(volatile unsigned int *pmem, unsigned int val)
{
    unsigned int t = *pmem;
    *pmem = val;
    return t;
}

static unsigned int atomic_mov(volatile unsigned int *pmem_dst, volatile unsigned int *pmem_src)
{
    unsigned int t = *pmem_src;
    *pmem_dst = t;
    return t;
}

static unsigned int atomic_get(volatile unsigned int *pmem)
{
    return *pmem;
}

static unsigned int atomic_sum(volatile unsigned int *pmem1, volatile unsigned int *pmem2)
{
    unsigned int t = *pmem1 + *pmem2;
    return t;
}

#ifdef FIFO_SEM_SUPPORT

#define MUTEX_LOCK MutexLock(&fifo->mutex)
#define MUTEX_UNLOCK MutexUnlock(&fifo->mutex)

#else

#define MUTEX_LOCK
#define MUTEX_UNLOCK

#endif

unsigned int fifo_GetDataLen(const Fifo * fifo)
{
    return atomic_get(ATOMIC_PTR & fifo->size);
}

unsigned int fifo_GetFreeLen(const Fifo * fifo)
{
    unsigned int ret;
    MUTEX_LOCK;
    ret = (fifo->limit - atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size));
    MUTEX_UNLOCK;
    return ret;
}

#define INS_BLOCK \
wrPtr = wrIdx + fifo->beg; \
if( wrIdx+len >= fifo->limit ) \
  i0 = wrIdx+len - fifo->limit; \
else \
  i0 = 0; \
if( pData ) \
{ \
  cpmem( wrPtr, pData, i1 = len - i0 ); \
  CACHE_FLUSH( wrPtr, i1, fifo); \
  if( i0 ) { \
    cpmem( fifo->beg, pData + i1, i0 ); \
    CACHE_FLUSH( fifo->beg, i0, fifo); \
  } \
} \
MUTEX_LOCK; \
i1 = atomic_inc(ATOMIC_PTR&fifo->wr_cnt, -1); \
if( i1 - 1 == 0 ) \
{ \
  i1 = atomic_set(ATOMIC_PTR&fifo->wr_size, 0); \
  atomic_inc(ATOMIC_PTR&fifo->size, i1); \
} \
MUTEX_UNLOCK;


unsigned int fifo_InsBlock_Overwrite(Fifo * fifo, const unsigned char *pData, unsigned int len, int *over_flag)
{
    (void) over_flag;
    unsigned int wrIdx;
    unsigned char *wrPtr;
    unsigned int i0, i1;

    MUTEX_LOCK;
    if(len > (fifo->limit - atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size)))
    {
        fifo->overflow_cnt++;
        if(atomic_get(ATOMIC_PTR & fifo->rd_size))
        {
            // read in progress
            MUTEX_UNLOCK;
#ifdef FIFO_DEB
            os_printf("fifo#%d insovr skip\n", fifo->id);
#endif
            return 0;
        }
        // no reads in progress
        atomic_inc(ATOMIC_PTR & fifo->wr_cnt, 1);
        i1 = atomic_inc(ATOMIC_PTR & fifo->wr_size, len);
        if(i1 + len > fifo->limit)
        {
            // many writes in progress
            atomic_inc(ATOMIC_PTR & fifo->wr_size, -len);
            atomic_inc(ATOMIC_PTR & fifo->wr_cnt, -1);
            MUTEX_UNLOCK;
            //  terminateA("wr_size full #%d\n", fifo->id);
            return 0;
        }
        atomic_inc(ATOMIC_PTR & fifo->size, -len);
        wrIdx = atomic_inc_lim(ATOMIC_PTR & fifo->wrIdx, len, fifo->limit);
        atomic_mov(ATOMIC_PTR & fifo->rdIdx, ATOMIC_PTR & fifo->wrIdx);
    }
    else
    {
        atomic_inc(ATOMIC_PTR & fifo->wr_cnt, 1);
        atomic_inc(ATOMIC_PTR & fifo->wr_size, len);
        wrIdx = atomic_inc_lim(ATOMIC_PTR & fifo->wrIdx, len, fifo->limit);
    }
    MUTEX_UNLOCK;

    INS_BLOCK;
    return len;
}

unsigned int fifo_InsBlock(Fifo * fifo, const unsigned char *pData, unsigned int len)
{
    unsigned int wrIdx;
    unsigned char *wrPtr;
    unsigned int i0, i1;

    MUTEX_LOCK;
    if(len > (fifo->limit - atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size)))
    {
        fifo->overflow_cnt++;
        MUTEX_UNLOCK;
        // os_printf("ins ovr len %d sz %d free %d\n", len, fifo->size, ( fifo->limit - atomic_sum(ATOMIC_PTR&fifo->size, ATOMIC_PTR&fifo->wr_size) ));
        return 0;
    }
    atomic_inc(ATOMIC_PTR & fifo->wr_cnt, 1);
    atomic_inc(ATOMIC_PTR & fifo->wr_size, len);
    wrIdx = atomic_inc_lim(ATOMIC_PTR & fifo->wrIdx, len, fifo->limit);
    MUTEX_UNLOCK;

    INS_BLOCK;
    return len;
}

/****************************************************************************/
unsigned int fifo_ExtrBlock(Fifo * fifo, unsigned char *pBuf, unsigned int len)
{
    unsigned char *rdPtr;
    unsigned int i0, i1;
    unsigned int size;

    unsigned int rdIdx;

    MUTEX_LOCK;
    size = atomic_get(ATOMIC_PTR & fifo->size);
    if(len > size)
    {
        len = size;
    }
    if(len == 0)
    {
        MUTEX_UNLOCK;
        return 0;
    }
    i1 = atomic_inc(ATOMIC_PTR & fifo->rd_size, len);
    if(i1)
    {
        atomic_inc(ATOMIC_PTR & fifo->rd_size, -len);
        MUTEX_UNLOCK;
#ifdef FIFO_DEB
        os_printf
#else
        terminateA
#endif
            ("fifo#%d concurrent read\n", fifo->id);
        return 0;
    }
    rdIdx = atomic_inc_lim(ATOMIC_PTR & fifo->rdIdx, len, fifo->limit);
    MUTEX_UNLOCK;
    // rdIdx %= fifo->limit;
    rdPtr = rdIdx + fifo->beg;

    if(rdIdx + len >= fifo->limit)
        i0 = rdIdx + len - fifo->limit;
    else
        i0 = 0;

    if(pBuf)
    {
        CACHE_INVALIDATE(rdPtr, i1, fifo);
        cpmem(pBuf, rdPtr, i1 = len - i0);
        if(i0)
        {
            CACHE_INVALIDATE(fifo->beg, i0, fifo);
            cpmem(pBuf + i1, fifo->beg, i0);
        }
    }

    MUTEX_LOCK;
    atomic_inc(ATOMIC_PTR & fifo->size, -len);
    atomic_inc(ATOMIC_PTR & fifo->rd_size, -len);
    MUTEX_UNLOCK;
    return (len);
}

/****************************************************************************/
unsigned int fifo_InsBlock_Box(Fifo * fifo, const unsigned char *pData, unsigned int len)
{
    unsigned int wrIdx;
    unsigned int i1;
    MUTEX_LOCK;
    i1 = atomic_set(ATOMIC_PTR & fifo->wr_cnt, 1);
    if(i1)
    {
        // write is in progress
        MUTEX_UNLOCK;
#ifdef FIFO_DEB
        os_printf("fifo#%d box wr skip %d\n", fifo->id, i1);
#endif
        return 0;
    }
    wrIdx = atomic_get(ATOMIC_PTR & fifo->rdIdx);
    MUTEX_UNLOCK;

    wrIdx += fifo->limit / 2;
    if(wrIdx >= fifo->limit)
        wrIdx -= fifo->limit;

    if(pData)
    {
        cpmem(fifo->beg + wrIdx, pData, len);
        CACHE_FLUSH(fifo->beg + wrIdx, len, fifo);
    }
    MUTEX_LOCK;
    atomic_inc((unsigned int *) &fifo->wr_size, len);
    atomic_set(&fifo->size, len);
    atomic_set((unsigned int *) &fifo->wr_cnt, 0);
    MUTEX_UNLOCK;
    return (len);
}

/****************************************************************************/
unsigned int fifo_ExtrBlock_Box(Fifo * fifo, unsigned char *pBuf, unsigned int len)
{
    unsigned int rdIdx;
    unsigned int i1;
    // len must be equal element size!
    MUTEX_LOCK;
    if(atomic_get(ATOMIC_PTR & fifo->size) < len)
    {
        // no data written yet
        MUTEX_UNLOCK;
        return 0;
    }
    i1 = atomic_inc(ATOMIC_PTR & fifo->rd_size, len);
    if(!i1 && !atomic_get(ATOMIC_PTR & fifo->wr_cnt) && atomic_set(ATOMIC_PTR & fifo->wr_size, 0))
    {
        rdIdx = atomic_inc_lim(ATOMIC_PTR & fifo->rdIdx, fifo->limit / 2, fifo->limit);
        rdIdx += fifo->limit / 2;
        if(rdIdx >= fifo->limit)
            rdIdx -= fifo->limit;
    }
    else
        rdIdx = atomic_get(ATOMIC_PTR & fifo->rdIdx);
    MUTEX_UNLOCK;

    // rdIdx %= fifo->limit;

    if(pBuf)
    {
        CACHE_INVALIDATE(rdIdx + fifo->beg, len, fifo);
        cpmem(pBuf, rdIdx + fifo->beg, len);
    }
    MUTEX_LOCK;
    atomic_inc(ATOMIC_PTR & fifo->rd_size, -len);
    MUTEX_UNLOCK;

    return (len);
}

void fifo_InitFifo(Fifo * fifo, unsigned char *buf, unsigned int size)
{
    zmem(fifo, sizeof(*fifo));
    fifo->beg = buf;
    fifo->limit = size;
#ifdef FIFO_SEM_SUPPORT
    MutexAdd(&fifo->mutex);
    // pthread_spin_init(&fifo->lock, PTHREAD_PROCESS_PRIVATE);
#endif
}
