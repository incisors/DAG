#include <iostream>
#include <numeric> // For std::accumulate

#include "graph_node.h"

void sumIntegersCPU() {
    std::vector<int> data = {1, 2, 3, 4, 5}; // 示例数据
    int sum = std::accumulate(data.begin(), data.end(), 0);

    std::cout << "Sum (CPU): " << sum << std::endl;
}

int main() {
    // 创建一个 CPU 类型的 GraphNode 实例
    GraphNode node(ComputeType::CPU);

    // 设置 CPU 处理函数
    node.
    node.setCPUProcess(sumIntegersCPU);

    // 执行节点
    node.execute();

    return 0;
}