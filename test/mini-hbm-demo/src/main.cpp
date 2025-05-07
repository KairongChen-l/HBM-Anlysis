#include "matmul.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    const int N = 1024;
    double* A = static_cast<double*>(malloc(sizeof(double) * N * N));
    double* B = static_cast<double*>(malloc(sizeof(double) * N * N));
    double* C = static_cast<double*>(malloc(sizeof(double) * N * N));

    random_fill(A, N);  random_fill(B, N);
    matmul(A, B, C, N);
    std::cout << "C[0] = " << C[0] << '\n';

    free(A); free(B); free(C);
    return 0;
}
