#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>      /* ← 必须包含，才能使用 bool */

extern void *hbm_malloc(size_t);
extern void  hbm_free(void *);
extern void  hbm_set_debug(bool);

/* 分配 32 KiB，写入校验模式 */
static void touch(void *p, size_t n)
{
    uint8_t *q = (uint8_t *)p;
    for (size_t i = 0; i < n; ++i)
        q[i] = (uint8_t)(i & 0xFF);
}

int main(int argc, char **argv)
{
    size_t iters = (argc > 1) ? strtoull(argv[1], NULL, 10) : 100000;
    const  size_t SZ = 32 * 1024;         /* 32 KiB */

    /* 如需观察运行时分配/释放日志，可打开调试 */
    /* hbm_set_debug(true); */

    printf("Loop %zu iterations (32 KiB each)…\n", iters);

    for (size_t i = 0; i < iters; ++i) {
        /* 1) malloc / free */
        void *a = malloc(SZ);
        if (!a) { perror("malloc"); return 1; }
        touch(a, SZ);
        free(a);

        /* 2) hbm_malloc / free (外层统一写 free) */
        void *b = hbm_malloc(SZ);
        if (!b) { perror("hbm_malloc"); return 1; }
        touch(b, SZ);
        free(b);
    }

    puts("All allocations freed — test finished.");
    return 0;
}

