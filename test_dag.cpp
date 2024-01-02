#include <iostream>
#include "executor.h"

int main() {
    // 创建图
    Graph graph;

    // 创建节点
    GraphNode multiplyNode(ComputeType::CPU, [](auto& inputs, auto& outputs) {
        double inputVal = std::get<double>(inputs["multiplyin"]);
        outputs["multiplyout"] = inputVal * 2;
    });
    multiplyNode.addInput("multiplyin", DataContainer()); // 设置 multiplyNode 的输入字段
    multiplyNode.addOutput("multiplyout", DataContainer()); // 设置 multiplyNode 的输出字段

    GraphNode divideNode(ComputeType::CPU, [](auto& inputs, auto& outputs) {
        double inputVal = std::get<double>(inputs["multiplyout"]);
        outputs["divideout"] = inputVal / 10;
    });
    divideNode.addInput("multiplyout", DataContainer()); // 设置 divideNode 的输入字段
    divideNode.addOutput("divideout", DataContainer()); // 设置 divideNode 的输出字段

    // 添加节点到图
    size_t multiplyNodeId = graph.addNode(multiplyNode);
    size_t divideNodeId = graph.addNode(divideNode);

    // 添加边
    std::cout << "Adding edge multiplyNode -> divideNode: " << (graph.addEdge(multiplyNodeId, divideNodeId) ? "Success" : "Failed") << "\n";

    // 准备输入 MiniBatch
    std::vector<std::unordered_map<std::string, MiniBatch>> inputBatches = {
        {{"multiplyin", MiniBatch({1.0, 2.0, 3.0})}}
    };
    
    std::cout << "executor start" << std::endl;
    // 创建执行器并运行
    Executor executor(graph, inputBatches);
    executor.run();

    std::cout << "reach end" << std::endl;
    // 打印结果
    for (size_t batchId = 0; batchId < inputBatches.size(); ++batchId) {
        std::cout << "Batch " << batchId << " output: ";
        auto output = graph.getMiniBatch(divideNodeId, batchId, "divideout");
        for (size_t i = 0; i < output.size(); ++i) {
            std::cout << std::get<double>(output.getData(i)) << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}
