#include <string.h>
#include <stddef.h>

#include "fifo.h"

// #define FIFO_DEB 1
#define cpmem memcpy
#define zmem(p, sz) memset((p), 0, (sz))

#define ATOMIC_PTR (ATOMIC_UINT *)

#define CACHE_FLUSH(a, b, c)
#define CACHE_INVALIDATE(a, b, c)

#ifdef USE_ATOMIC_MEM
#define MUTEX_LOCK(x)
#define MUTEX_UNLOCK(x)
#else
#define MUTEX_LOCK() sys_MutexLock(fifo->mutex)
#define MUTEX_UNLOCK() sys_MutexUnlock(fifo->mutex)
#endif

static __inline unsigned int fifo_atomic_inc(ATOMIC_UINT *pmem, int delta)
{
    unsigned int t;
#ifdef USE_ATOMIC_MEM
    if (delta >= 0)
        t = __atomic_fetch_add(pmem, delta, USE_ATOMIC_MEM);
    else
        t = __atomic_fetch_sub(pmem, -delta, USE_ATOMIC_MEM);
#else
    t = *pmem;
    *(ATOMIC_UINT *)pmem += delta;
#endif
    return t;
}

static __inline unsigned int fifo_atomic_set(ATOMIC_UINT *pmem, unsigned int val)
{
    unsigned int t;
#ifdef USE_ATOMIC_MEM
    __atomic_exchange(pmem, &val, &t, USE_ATOMIC_MEM);
#else
    t = *pmem;
    *pmem = val;
#endif
    return t;
}

static __inline unsigned int fifo_atomic_get(ATOMIC_UINT *pmem)
{
#ifdef USE_ATOMIC_MEM
    unsigned int t;
    __atomic_load(pmem, &t, USE_ATOMIC_MEM);
    return t;
#else
    return *pmem;
#endif
}

static __inline unsigned int fifo_atomic_sum(ATOMIC_UINT *pmem1, ATOMIC_UINT *pmem2)
{
#ifdef USE_ATOMIC_MEM
    unsigned int t1 = 0, t2 = 0;
    unsigned int t1_prev, t2_prev;
    do
    {
        t1_prev = t1;
        t2_prev = t2;
        __atomic_load(pmem1, &t1, USE_ATOMIC_MEM);
        __atomic_load(pmem2, &t2, USE_ATOMIC_MEM);
    }
    while (t1 != t1_prev || t2 != t2_prev);
    return t1 + t2;
#else
    unsigned int t = *pmem1 + *pmem2;
    return t;
#endif
}

unsigned int fifo_GetDataLen(const Fifo *fifo)
{
    return fifo_atomic_get(ATOMIC_PTR & fifo->size);
}

unsigned int fifo_GetFreeLen(const Fifo *fifo)
{
    unsigned int ret;
    MUTEX_LOCK();
    ret = (fifo->limit - fifo_atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size));
    MUTEX_UNLOCK();
    return ret;
}

unsigned int fifo_InsBlock(Fifo *fifo, const void *pData, unsigned int len)
{
    unsigned int wrIdx;
    unsigned char *wrPtr;
    unsigned int i0, i1;
    unsigned char *beg = (unsigned char *)(fifo + 1);

    MUTEX_LOCK();
    if (len > (fifo->limit - fifo_atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size)))
    {
        fifo->overflow_cnt++;
        MUTEX_UNLOCK();
        return 0;
    }
    fifo_atomic_inc(ATOMIC_PTR & fifo->wr_cnt, 1);
    fifo_atomic_inc(ATOMIC_PTR & fifo->wr_size, (int)len);
    wrIdx = fifo_atomic_inc(ATOMIC_PTR & fifo->wrIdx, (int)len);
    MUTEX_UNLOCK();
    wrIdx %= fifo->limit;

    wrPtr = wrIdx + beg;
    if (wrIdx + len >= fifo->limit)
        i0 = wrIdx + len - fifo->limit;
    else
        i0 = 0;
    if (pData)
    {
        cpmem(wrPtr, pData, i1 = len - i0);
        CACHE_FLUSH(wrPtr, i1, fifo);
        if (i0)
        {
            cpmem(beg, (const char *)pData + i1, i0);
            CACHE_FLUSH(beg, i0, fifo);
        }
    }
    MUTEX_LOCK();
    i1 = fifo_atomic_inc(ATOMIC_PTR & fifo->wr_cnt, -1);
    if (i1 - 1 == 0)
    {
        i1 = fifo_atomic_set(ATOMIC_PTR & fifo->wr_size, 0);
        fifo_atomic_inc(ATOMIC_PTR & fifo->size, (int)i1);
    }
    MUTEX_UNLOCK();
    return len;
}

unsigned int fifo_InsBlocks(Fifo *fifo, const void **pData, const unsigned int *plen, int cnt)
{
    int i;
    unsigned int wrIdx;
    unsigned char *wrPtr;
    unsigned int i0, i1;
    unsigned char *beg = (unsigned char *)(fifo + 1);
    unsigned int len = 0;
    for (i = 0; i < cnt; i++)
    {
        len += plen[i];
    }
    MUTEX_LOCK();
    if (len > (fifo->limit - fifo_atomic_sum(ATOMIC_PTR & fifo->size, ATOMIC_PTR & fifo->wr_size)))
    {
        fifo->overflow_cnt++;
        MUTEX_UNLOCK();
        return 0;
    }
    fifo_atomic_inc(ATOMIC_PTR & fifo->wr_cnt, 1);
    fifo_atomic_inc(ATOMIC_PTR & fifo->wr_size, (int)len);
    wrIdx = fifo_atomic_inc(ATOMIC_PTR & fifo->wrIdx, (int)len);
    MUTEX_UNLOCK();
    wrIdx %= fifo->limit;

    for (i = 0; i < cnt; i++)
    {
        wrPtr = wrIdx + beg;
        if (wrIdx + plen[i] >= fifo->limit)
            i0 = wrIdx + plen[i] - fifo->limit;
        else
            i0 = 0;
        if (pData[i])
        {
            cpmem(wrPtr, pData[i], i1 = plen[i] - i0);
            CACHE_FLUSH(wrPtr, i1, fifo);
            if (i0)
            {
                cpmem(beg, (char *)pData[i] + i1, i0);
                CACHE_FLUSH(beg, i0, fifo);
            }
        }
        wrIdx += plen[i];
        if (wrIdx >= fifo->limit)
        {
            wrIdx -= fifo->limit;
        }
    }
    MUTEX_LOCK();
    i1 = fifo_atomic_inc(ATOMIC_PTR & fifo->wr_cnt, -1);
    if (i1 - 1 == 0)
    {
        i1 = fifo_atomic_set(ATOMIC_PTR & fifo->wr_size, 0);
        fifo_atomic_inc(ATOMIC_PTR & fifo->size, (int)i1);
    }
    MUTEX_UNLOCK();

    return len;
}

unsigned int fifo_ExtrBlock(Fifo *fifo, void *pBuf, unsigned int len)
{
    unsigned char *rdPtr;
    unsigned int i0, i1;
    unsigned int size;

    unsigned int rdIdx;
    unsigned char *beg = (unsigned char *)(fifo + 1);

    MUTEX_LOCK();
    size = fifo_atomic_get(ATOMIC_PTR & fifo->size);
    if (len > size)
    {
        len = size;
    }
    if (len == 0)
    {
        MUTEX_UNLOCK();
        return 0;
    }
    i1 = fifo_atomic_inc(ATOMIC_PTR & fifo->rd_size, (int)len);
    if (i1)
    {
        fifo_atomic_inc(ATOMIC_PTR & fifo->rd_size, -(int)len);
        MUTEX_UNLOCK();
        return 0;
    }
    rdIdx = fifo_atomic_inc(ATOMIC_PTR & fifo->rdIdx, (int)len);
    MUTEX_UNLOCK();
    rdIdx %= fifo->limit;
    rdPtr = rdIdx + beg;

    if (rdIdx + len >= fifo->limit)
        i0 = rdIdx + len - fifo->limit;
    else
        i0 = 0;

    if (pBuf)
    {
        CACHE_INVALIDATE(rdPtr, i1, fifo);
        cpmem(pBuf, rdPtr, i1 = len - i0);
        if (i0)
        {
            CACHE_INVALIDATE(beg, i0, fifo);
            cpmem((char *)pBuf + i1, beg, i0);
        }
    }

    MUTEX_LOCK();
    fifo_atomic_inc(ATOMIC_PTR & fifo->size, -(int)len);
    fifo_atomic_inc(ATOMIC_PTR & fifo->rd_size, -(int)len);
    MUTEX_UNLOCK();
    return (len);
}

unsigned int fifo_InsBlock_Box(Fifo *fifo, const void *pData, unsigned int len)
{
    unsigned int wrIdx;
    unsigned int i1;
    unsigned char *beg = (unsigned char *)(fifo + 1);

    MUTEX_LOCK();
    i1 = fifo_atomic_set(ATOMIC_PTR & fifo->wr_cnt, 1);
    if (i1)
    {
        // write is in progress
        MUTEX_UNLOCK();
        return 0;
    }
    wrIdx = fifo_atomic_get(ATOMIC_PTR & fifo->rdIdx);
    MUTEX_UNLOCK();
    wrIdx %= fifo->limit;
    wrIdx += fifo->limit / 2;
    if (wrIdx >= fifo->limit)
        wrIdx -= fifo->limit;

    if (pData)
    {
        cpmem(beg + wrIdx, pData, len);
        CACHE_FLUSH(beg + wrIdx, len, fifo);
    }
    MUTEX_LOCK();
    fifo_atomic_inc((ATOMIC_UINT *)&fifo->wr_size, (int)len);
    fifo_atomic_set(&fifo->size, len);
    fifo_atomic_set((ATOMIC_UINT *)&fifo->wr_cnt, 0);
    MUTEX_UNLOCK();
    return (len);
}

unsigned int fifo_ExtrBlock_Box(Fifo *fifo, void *pBuf, unsigned int len)
{
    unsigned int rdIdx;
    unsigned int i1;
    unsigned char *beg = (unsigned char *)(fifo + 1);

    // len must be equal element size!
    MUTEX_LOCK();
    if (fifo_atomic_get(ATOMIC_PTR & fifo->size) < len)
    {
        // no data written yet
        MUTEX_UNLOCK();
        return 0;
    }
    i1 = fifo_atomic_inc(ATOMIC_PTR & fifo->rd_size, (int)len);
    if (!i1 && !fifo_atomic_get(ATOMIC_PTR & fifo->wr_cnt) && fifo_atomic_set(ATOMIC_PTR & fifo->wr_size, 0))
    {
        rdIdx = fifo_atomic_inc(ATOMIC_PTR & fifo->rdIdx, (int)(fifo->limit / 2));
        MUTEX_UNLOCK();
        rdIdx %= fifo->limit;
        rdIdx += fifo->limit / 2;
        if (rdIdx >= fifo->limit)
            rdIdx -= fifo->limit;
    }
    else
    {
        rdIdx = fifo_atomic_get(ATOMIC_PTR & fifo->rdIdx);
        MUTEX_UNLOCK();
        rdIdx %= fifo->limit;
    }

    if (pBuf)
    {
        CACHE_INVALIDATE(rdIdx + beg, len, fifo);
        cpmem(pBuf, rdIdx + beg, len);
    }
    MUTEX_LOCK();
    fifo_atomic_inc(ATOMIC_PTR & fifo->rd_size, -(int)len);
    MUTEX_UNLOCK();

    return (len);
}

void fifo_InitFifo(Fifo *fifo, void *buf, unsigned int size)
{
    (void)buf;
    zmem(fifo, sizeof(*fifo));
    fifo->limit = size;
#ifndef USE_ATOMIC_MEM
    fifo->mutex = sys_MutexCreate();
#endif
}
