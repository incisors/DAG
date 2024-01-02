#include <iostream>

#include "graph.h"

int main() {
    Graph graph;

    // 创建节点
    GraphNode nodeA(ComputeType::CPU);
    nodeA.addOutput("dataA", DataContainer(0));
    nodeA.addInput("dataC", DataContainer(0)); // Intentionally mismatched for testing

    GraphNode nodeB(ComputeType::CPU);
    nodeB.addOutput("dataB", DataContainer(0));
    nodeB.addInput("dataA", DataContainer(0));

    GraphNode nodeC(ComputeType::CPU);
    nodeC.addOutput("dataC", DataContainer(0));
    nodeC.addInput("dataB", DataContainer(0));

    // 添加节点到图
    size_t idA = graph.addNode(nodeA);
    size_t idB = graph.addNode(nodeB);
    size_t idC = graph.addNode(nodeC);
    graph.printRoots();
    // 尝试添加边
    std::cout << "hasCycle: " << (graph.hasCycle() ? "true" : "false") << "\n";
    std::cout << "Adding edge A -> B: " << (graph.addEdge(idA, idB) ? "Success" : "Failed") << "\n";
    std::cout << "Adding edge B -> C: " << (graph.addEdge(idB, idC) ? "Success" : "Failed") << "\n";
    std::cout << "Adding edge C -> A: " << (graph.addEdge(idC, idA) ? "Success" : "Failed (Expected, creates a cycle)") << "\n";
    graph.printRoots();

    // 打印图的结构
    graph.printGraph();

    return 0;
}