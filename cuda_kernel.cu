#include <cuda_runtime.h>
#include <iostream>

// CUDA核函数 - 向量加法
__global__ void addKernel(const double *x, const double *y, double *z, int N) {
    int n = blockDim.x * blockIdx.x + threadIdx.x;
    if (n < N) {
        z[n] = x[n] + y[n];
    }
}

// 检查CUDA调用的结果
void checkCudaCall(cudaError_t result) {
    if (result != cudaSuccess) {
        std::cerr << "CUDA Runtime Error: " << cudaGetErrorString(result) << std::endl;
        exit(-1);
    }
}

// 在GPU上执行向量加法的函数
void runCudaAddKernel(const double *h_x, const double *h_y, double *h_z, int N) {
    size_t size = N * sizeof(double);
    
    double *d_x, *d_y, *d_z;
    checkCudaCall(cudaMalloc((void **)&d_x, size));
    checkCudaCall(cudaMalloc((void **)&d_y, size));
    checkCudaCall(cudaMalloc((void **)&d_z, size));

    checkCudaCall(cudaMemcpy(d_x, h_x, size, cudaMemcpyHostToDevice));
    checkCudaCall(cudaMemcpy(d_y, h_y, size, cudaMemcpyHostToDevice));

    const int block_size = 128;
    const int grid_size = (N + block_size - 1) / block_size;

    addKernel<<<grid_size, block_size>>>(d_x, d_y, d_z, N);
    cudaDeviceSynchronize(); // 等待CUDA核函数完成

    checkCudaCall(cudaMemcpy(h_z, d_z, size, cudaMemcpyDeviceToHost));

    cudaFree(d_x);
    cudaFree(d_y);
    cudaFree(d_z);
}
