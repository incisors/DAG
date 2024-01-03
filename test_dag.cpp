#include <iostream>
#include "dag.h"

int main() {
    // create graph
    Graph graph;

    // create node
    GraphNode multiplyNode(ComputeType::CPU, [](auto& inputs, auto& outputs) {
        double inputVal = std::get<double>(inputs["multiplyin"]);
        outputs["multiplyout"] = inputVal * 2;
    });
    multiplyNode.addInput("multiplyin", DataContainer()); // set multiplyNode's input field
    multiplyNode.addOutput("multiplyout", DataContainer()); // set multiplyNode's output field

    GraphNode divideNode(ComputeType::CPU, [](auto& inputs, auto& outputs) {
        double inputVal = std::get<double>(inputs["multiplyout"]);
        outputs["divideout"] = inputVal / 10;
    });
    divideNode.addInput("multiplyout", DataContainer()); // set divideNode's input field
    divideNode.addOutput("divideout", DataContainer()); // set divideNode's output field

    // add node to graph
    size_t multiplyNodeId = graph.addNode(multiplyNode);
    size_t divideNodeId = graph.addNode(divideNode);

    // try to add edge
    std::cout << "Adding edge multiplyNode -> divideNode: " << (graph.addEdge(multiplyNodeId, divideNodeId) ? "Success" : "Failed") << "\n";

    // input MiniBatch
    std::vector<std::unordered_map<std::string, MiniBatch>> inputBatches = {
        {{"multiplyin", MiniBatch({1.0, 2.0, 3.0})}}
    };

    std::cout << "executor start" << std::endl;
    // create executor
    Executor executor(graph, inputBatches);
    executor.run();

    std::cout << "reach end" << std::endl;
    // output MiniBatch
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
