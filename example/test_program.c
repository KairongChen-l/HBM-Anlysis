#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>  // 如果你的环境没有OpenMP，可以移除相关部分

// 流式访问（高带宽）
void stream_access(int* data, int size) {
    // 连续访问内存，非常适合HBM
    for (int i = 0; i < size; i++) {
        data[i] = i * 2;  // 写入
    }
    
    // 另一个流式访问循环
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += data[i];  // 读取
    }
    
    printf("Stream access sum: %d\n", sum);
}

// 随机访问（低带宽效率）
void random_access(int* data, int size) {
    // 随机访问内存模式
    srand(42);
    for (int i = 0; i < size/10; i++) {
        int idx = rand() % size;
        data[idx] = i;  // 随机写入
    }
    
    int sum = 0;
    for (int i = 0; i < size/10; i++) {
        int idx = rand() % size;
        sum += data[idx];  // 随机读取
    }
    
    printf("Random access sum: %d\n", sum);
}

// 嵌套循环（访问模式复杂）
void nested_loops(int** matrix, int rows, int cols) {
    // 行优先访问
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = i * j;
        }
    }
    
    // 列优先访问（对缓存不友好）
    int sum = 0;
    for (int j = 0; j < cols; j++) {
        for (int i = 0; i < rows; i++) {
            sum += matrix[i][j];
        }
    }
    
    printf("Nested loops sum: %d\n", sum);
}

// 向量化友好操作
void vectorizable_operation(int* data, int size) {
    // 这种循环可以被向量化
    for (int i = 0; i < size; i++) {
        data[i] = data[i] * 2 + 1;
    }
    
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += data[i];
    }
    
    printf("Vectorizable operation sum: %d\n", sum);
}

// 并行处理
void parallel_processing(int* data, int size) {
#pragma omp parallel for
    for (int i = 0; i < size; i++) {
        data[i] = i * i;
    }
    
    int sum = 0;
#pragma omp parallel for reduction(+:sum)
    for (int i = 0; i < size; i++) {
        sum += data[i];
    }
    
    printf("Parallel processing sum: %d\n", sum);
}

// 带有多维数组的函数
void multidimensional_array() {
    const int dim1 = 100;
    const int dim2 = 100;
    const int dim3 = 100;
    
    // 这个分配应该被识别为热点
    int*** array3d = (int***)malloc(dim1 * sizeof(int**));
    for (int i = 0; i < dim1; i++) {
        array3d[i] = (int**)malloc(dim2 * sizeof(int*));
        for (int j = 0; j < dim2; j++) {
            array3d[i][j] = (int*)malloc(dim3 * sizeof(int));
        }
    }
    
    // 三层嵌套循环访问
    for (int i = 0; i < dim1; i++) {
        for (int j = 0; j < dim2; j++) {
            for (int k = 0; k < dim3; k++) {
                array3d[i][j][k] = i + j + k;
            }
        }
    }
    
    // 释放内存
    for (int i = 0; i < dim1; i++) {
        for (int j = 0; j < dim2; j++) {
            free(array3d[i][j]);
        }
        free(array3d[i]);
    }
    free(array3d);
}

// 跨函数传递内存
void process_data(int* data, int size) {
    for (int i = 0; i < size; i++) {
        data[i] += i;
    }
}

void cross_function_memory() {
    const int size = 1000000;
    // 这个分配应该被识别为热点
    int* data = (int*)malloc(size * sizeof(int));
    
    for (int i = 0; i < size; i++) {
        data[i] = i;
    }
    
    // 调用另一个函数处理数据
    process_data(data, size);
    
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum += data[i];
    }
    
    printf("Cross function sum: %d\n", sum);
    free(data);
}

// 主函数
int main() {
    const int size = 10000000;  // 大数组
    
    // 这个分配应该被识别为热点
    int* large_array = (int*)malloc(size * sizeof(int));
    if (!large_array) {
        printf("Memory allocation failed\n");
        return 1;
    }
    
    // 测试不同的访问模式
    stream_access(large_array, size);
    random_access(large_array, size);
    vectorizable_operation(large_array, size);
    
    // 如果支持OpenMP
#ifdef _OPENMP
    parallel_processing(large_array, size);
#endif
    
    // 创建二维矩阵
    const int rows = 1000;
    const int cols = 1000;
    
    // 这个分配应该被识别为热点
    int** matrix = (int**)malloc(rows * sizeof(int*));
    for (int i = 0; i < rows; i++) {
        matrix[i] = (int*)malloc(cols * sizeof(int));
    }
    
    nested_loops(matrix, rows, cols);
    
    // 释放矩阵内存
    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
    
    // 多维数组测试
    multidimensional_array();
    
    // 跨函数内存测试
    cross_function_memory();
    
    // 释放大数组
    free(large_array);
    
    // 测试小分配 - 这个不太可能被标记为热点
    int* small_array = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) {
        small_array[i] = i;
    }
    free(small_array);
    
    return 0;
}