#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "rtos.h"
#include "fifo.h"

#define BLOCK_SIZE 0x100
#define MAX_PIPES 10

// #define USE_IO 1

#ifndef USE_IO
Fifo *fifos[MAX_PIPES] = { 0 };
#endif
_Atomic unsigned long reads_cnt[MAX_PIPES] = { 0 };
_Atomic unsigned long writes_cnt[MAX_PIPES] = { 0 };
_Atomic unsigned long miss_cnt[MAX_PIPES] = { 0 };
_Atomic unsigned long errs_cnt[MAX_PIPES] = { 0 };
_Atomic unsigned long write0_cnt[MAX_PIPES] = { 0 };


static unsigned int calc_csum(void *p, int len)
{
    unsigned int csum = 0xE6;
    unsigned char *pb = (unsigned char *) p;
    while(len-- > 0)
        csum += *(pb++);
    return (csum);
}

int check_buf(int i, char *s, int len, unsigned long *prev_num)
{
    (void)len;
    char *ps;
    unsigned int csum = 0, csum2 = 0;
    unsigned long n;
    int from = 0;
    reads_cnt[i]++;
    ps = strchr(s, '#');
    if(ps)
    {
        from = strtol(ps + 1, 0, 10);
        n = strtol(s, 0, 10);
        if(prev_num[from])
        {
            if(n != prev_num[from] + 1)
            {
                miss_cnt[i]++;
                // os_printf("miss from #%d %lu expected %lu\n", from, n, prev_num[from]+1);
            }
        }
        prev_num[from] = n;
    }
    ps = strchr(s, '*');
    if(ps)
    {
        csum = calc_csum(s, ps - s);
        csum2 = strtol(ps + 1, 0, 16);
        if(csum2 != csum)
        {
            // os_printf("read %d %x==%x\n", len, csum, csum2);
            errs_cnt[i]++;
            return 1;
        }
    }
    // os_printf("read %d %x==%x from #%d %s\n", len, csum, csum2, from,  s);
    return 0;
}

void test_io_task(void *p)
{
    (void) p;
    int rd_num = (int) p;
    unsigned long prev_num[MAX_PIPES] = { 0 };
    unsigned long write_num[MAX_PIPES] = { 0 };
    os_printf("start %s %d\n", os_get_cur_task_name(), rd_num);
    sleep(1);
    while(1)
    {
        int i, j;
        for(j = 0; j < MAX_PIPES; j++)
        {
            for(i = 1; i < MAX_PIPES; i++)
            {
                int len;
                char buf[BLOCK_SIZE * 10];
                // all reads first, then one write
                do
                {
#ifdef USE_IO
                    len = io_read(rd_num, buf, BLOCK_SIZE);
#else
                    len = fifo_ExtrBlock(fifos[rd_num], buf, BLOCK_SIZE);
#endif
                    if(len == BLOCK_SIZE)
                    {
                        check_buf(rd_num, buf, len, prev_num);
                    }
                }
                while(len == BLOCK_SIZE);

                if(i != rd_num)
                {
                    write_num[i]++;
                    snprintf(buf, BLOCK_SIZE, "%lu from #%d to %d %0200d ", write_num[i], rd_num, i, i);
                    unsigned int csum = calc_csum(buf, BLOCK_SIZE - 16);
                    sprintf(buf + BLOCK_SIZE - 16, "*%x\n", csum);
#ifdef USE_IO
                    len = io_write(i, buf, BLOCK_SIZE);
#else
                    len = fifo_InsBlock(fifos[i], buf, BLOCK_SIZE);
#endif
                    if(len != BLOCK_SIZE)
                    {
                        write0_cnt[i]++;
                    }
                    // os_printf("write to #%d %s\n", i, buf);
                    writes_cnt[i]++;
                }
            }
        }
        os_sleep_ms(1);
    }
}

int main(void)
{
    char *p;
    int len;
    int i;
    os_init();
    len = BLOCK_SIZE * 100 * (MAX_PIPES + 10);
    p = malloc(len);
    io_init(p, len);
    os_printf("start\n");

    for(i = 1; i < MAX_PIPES; i++)
    {
#ifdef USE_IO        
        io_open(i, 10, BLOCK_SIZE, O_NONBLOCK);
#else        
        len = BLOCK_SIZE * MAX_PIPES * 20 + sizeof(Fifo);
        fifos[i] = malloc(len);
        fifo_InitFifo(fifos[i], fifos[i] + 1, len - sizeof(Fifo));
#endif        
    }
    for(i = 0; i < MAX_PIPES - 1; i++)
    {
        char s[100];
        sprintf(s, "task#%d", i + 1);
        os_create_task(s, &test_io_task, 7+i, (void *) (i + 1));
    }
    os_start();

    for(;;)
    {
        os_printf("##        reads       writes    erros   missed   wr_ovr\n", i, reads_cnt[i], writes_cnt[i], errs_cnt[i], miss_cnt[i], write0_cnt[i]);
        for(i = 1; i < MAX_PIPES; i++)
        {
            os_printf("%2d %12lu %12lu %8lu %8lu %8lu\n", i, reads_cnt[i], writes_cnt[i], errs_cnt[i], miss_cnt[i], write0_cnt[i]);
        }
        os_sleep_ms(1000);
    }
    return 0;
}
