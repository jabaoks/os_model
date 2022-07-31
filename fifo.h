/*
 * fifo.h
 */
#ifndef __FIFO_H
#define __FIFO_H

#include "rtos.h"

typedef struct
{
    unsigned char *beg;
    unsigned int limit;
    volatile unsigned int size;
    volatile unsigned int rdIdx;
    volatile unsigned int rd_size;
    volatile unsigned int wrIdx;
    volatile unsigned int wr_cnt;
    volatile unsigned int wr_size;
    MUTEX_ID mutex;
    short id;
    volatile unsigned short overflow_cnt;
} Fifo;

#ifdef __cplusplus
extern "C"
{
#endif

    unsigned int fifo_InsBlock(Fifo * fifo, const unsigned char *pData, unsigned int len);
    unsigned int fifo_ExtrBlock(Fifo * fifo, unsigned char *pBuf, unsigned int len);
    unsigned int fifo_InsBlock_Overwrite(Fifo * fifo, const unsigned char *pData, unsigned int len, int *over_flag);
    void fifo_InitFifo(Fifo * fifo, unsigned char *buf, unsigned int size);
    unsigned int fifo_GetDataLen(const Fifo * fifo);
    unsigned int fifo_GetFreeLen(const Fifo * fifo);
    unsigned int fifo_ExtrBlock_Box(Fifo * fifo, unsigned char *pBuf, unsigned int len);
    unsigned int fifo_InsBlock_Box(Fifo * fifo, const unsigned char *pData, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
