#include <cuda_runtime.h>
#include <iostream>
#include "mini_batch.h"
#include "graph_node.h"


// CUDA核函数 - 将输入元素乘以2
__global__ void multiplyKernel(const double *input, double *output, int N) {
    int idx = blockDim.x * blockIdx.x + threadIdx.x;
    if (idx < N) {
        output[idx] = input[idx] * 2.0;
    }
}

void checkCudaCall(cudaError_t result) {
    if (result != cudaSuccess) {
        std::cerr << "CUDA Runtime Error: " << cudaGetErrorString(result) << std::endl;
        exit(-1);
    }
}

// 执行CUDA处理的函数
void runCudaProcess(GraphNode& node, const std::vector<MiniBatch>& inputMiniBatches, std::vector<MiniBatch>& outputMiniBatches, const std::string outputName) {
    // 假设只处理第一个MiniBatch
    if (inputMiniBatches.empty()) return;

    const auto& inputBatch = inputMiniBatches[0].getData();
    int N = inputBatch.size();
    size_t size = N * sizeof(double);

    double *d_input, *d_output;
    checkCudaCall(cudaMalloc((void **)&d_input, size));
    checkCudaCall(cudaMalloc((void **)&d_output, size));

    // 准备数据
    std::vector<double> h_input(N);
    for (int i = 0; i < N; ++i) {
        h_input[i] = std::get<double>(inputBatch[i]);
    }

    checkCudaCall(cudaMemcpy(d_input, h_input.data(), size, cudaMemcpyHostToDevice));

    // 计算grid和block大小
    int block_size = 128;
    int grid_size = (N + block_size - 1) / block_size;

    // 调用CUDA核函数
    multiplyKernel<<<grid_size, block_size>>>(d_input, d_output, N);
    cudaDeviceSynchronize();

    // 从GPU内存复制回主机内存
    std::vector<double> h_output(N);
    checkCudaCall(cudaMemcpy(h_output.data(), d_output, size, cudaMemcpyDeviceToHost));

    // 准备输出MiniBatch
    outputMiniBatches.clear();
    std::vector<DataContainer> outputData(N);
    for (int i = 0; i < N; ++i) {
        outputData[i] = h_output[i];
    }
    outputMiniBatches.emplace_back(outputName, outputData);

    cudaFree(d_input);
    cudaFree(d_output);
}