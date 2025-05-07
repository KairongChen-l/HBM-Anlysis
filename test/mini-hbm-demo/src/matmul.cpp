#include "matmul.hpp"
#include <random>

void random_fill(double* p, int N) {
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (long long i = 0; i < 1LL * N * N; ++i) p[i] = dist(rng);
}

void matmul(const double* A,const double* B,double* C,int N){
    for(int i=0;i<N;i++)
        for(int j=0;j<N;j++){
            double s=0;
            for(int k=0;k<N;k++) s += A[i*N+k]*B[k*N+j];
            C[i*N+j] = s;
        }
}
