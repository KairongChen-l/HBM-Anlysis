/*
 * test_numa.c
 *
 * 作用：
 *   1. 通过 malloc 分配一块可选大小的内存（默认 1024 MiB）。
 *   2. 写入已分配区域，确保物理内存真正被占用。
 *   3. 每 2 秒打印一次所有 NUMA 节点的 “已用 / 总量” 内存。
 *
 * 编译依赖：libnuma
 *   clang -Wall -O2 test_numa.c -o test_numa -lnuma
 *
 * 运行示例：
 *   # 默认分配 1 GiB
 *   ./test_numa
 *
 *   # 分配 16 GiB，并限定进程运行在 CPU0 上
 *   numactl -C 0 ./test_numa 16384
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <numa.h>

static void print_node_usage(void)
{
    int max_node = numa_max_node();
    for (int n = 0; n <= max_node; ++n) {
        long long free_bytes = 0;
        long long total_bytes = numa_node_size64(n, &free_bytes);
        double used_gb = (double)(total_bytes - free_bytes) / (1024.0 * 1024 * 1024);
        double total_gb = (double)total_bytes             / (1024.0 * 1024 * 1024);

        printf("Node %-2d : used %6.2f GB / %6.2f GB\n", n, used_gb, total_gb);
    }
}

int main(int argc, char **argv)
{
    if (numa_available() < 0) {
        fprintf(stderr, "Error: NUMA API not available on this system.\n");
        return 1;
    }

    /* 读取待分配大小（MiB）—— 默认 1024 MiB */
    size_t mbytes = (argc > 1) ? strtoull(argv[1], NULL, 10) : 1024;
    size_t bytes  = mbytes * 1024ULL * 1024ULL;

    printf("Allocating %zu MiB …\n", mbytes);
    void *buf = malloc(bytes);
    if (!buf) {
        perror("malloc");
        return 1;
    }

    /* 触碰所有页面，确保真正分配到物理内存 / HBM */
    memset(buf, 0xA5, bytes);
    printf("Allocation done — start monitoring (Ctrl-C to quit)\n\n");

    /* 周期性打印各节点内存使用情况 */
    while (1) {
        print_node_usage();
        puts("-------------------------------------------------\n");
        sleep(2);
    }

    /* 永远不会到这里；加上以防万一 */
    free(buf);
    return 0;
}

