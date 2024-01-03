#include <iostream>

#include "graph.h"

int main() {
    Graph graph;

    GraphNode nodeA(ComputeType::CPU);
    nodeA.addOutput("dataA", DataContainer(0));
    nodeA.addInput("dataC", DataContainer(0)); // Intentionally mismatched for testing

    GraphNode nodeB(ComputeType::CPU);
    nodeB.addOutput("dataB", DataContainer(0));
    nodeB.addInput("dataA", DataContainer(0));

    GraphNode nodeC(ComputeType::CPU);
    nodeC.addOutput("dataC", DataContainer(0));
    nodeC.addInput("dataB", DataContainer(0));

    size_t idA = graph.addNode(nodeA);
    size_t idB = graph.addNode(nodeB);
    size_t idC = graph.addNode(nodeC);

    graph.printRoots();

    std::cout << "hasCycle: " << (graph.hasCycle() ? "true" : "false") << "\n";

    std::cout << "Adding edge A -> B: " << (graph.addEdge(idA, idB) ? "Success" : "Failed") << "\n";
    std::cout << "Adding edge B -> C: " << (graph.addEdge(idB, idC) ? "Success" : "Failed") << "\n";
    std::cout << "Adding edge C -> A: " << (graph.addEdge(idC, idA) ? "Success" : "Failed (Expected, creates a cycle)") << "\n";
    
    graph.printRoots();

    graph.printGraph();

    return 0;
}