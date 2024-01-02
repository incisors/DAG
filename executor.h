#include <thread>
#include <vector>
#include <functional>
#include <unordered_map>
#include "graph.h"
#include "queue.h"

class Executor {
public:
    // 修改了构造函数的参数类型
    Executor(Graph& graph, const std::vector<std::unordered_map<std::string, MiniBatch>>& inputBatches)
        : m_graph(graph), m_inputBatches(inputBatches) {
        
        std::cout << "executor constructor" << std::endl;
        initialize();
        std::cout << "executor constructor end" << std::endl;
    }

    void run() {
        std::cout << "executor run" << std::endl;
        std::cout << "initializeTaskQueue" << std::endl;
        initializeTaskQueue();

        std::cout << "initialize workers" << std::endl;

        std::vector<std::thread> workers;
        for (size_t i = 0; i < std::thread::hardware_concurrency(); ++i) {
            workers.emplace_back(&Executor::workerThread, this);
        }

        for (auto& worker : workers) {
            worker.join();
        }
    }

private:
    Graph& m_graph;
    const std::vector<std::unordered_map<std::string, MiniBatch>>& m_inputBatches;
    ThreadSafeQueue<std::pair<size_t, size_t>> m_taskQueue; // Pair of nodeId and batchId

    void initialize() {
        std::cout << "Initialize MiniBatches in Graph" << std::endl;
        m_graph.initMiniBatches(m_inputBatches.size());
        
        std::cout << "Filling input MiniBatches for root nodes" << std::endl;
        for (size_t batchId = 0; batchId < m_inputBatches.size(); ++batchId) {
            const auto& batchMap = m_inputBatches[batchId];
            for (size_t nodeId = 0; nodeId < m_graph.size(); ++nodeId) {
                if (m_graph.isRoot(nodeId)) {
                    auto& nodeBatches = m_graph.getNodeMiniBatches(nodeId);
                    for (const auto& inputField : batchMap) {
                        nodeBatches[batchId][inputField.first] = inputField.second;
                    }
                }
            }
        }
    }

    void initializeTaskQueue() {
        for (size_t nodeId = 0; nodeId < m_graph.size(); ++nodeId) {
            for (size_t batchId = 0; batchId < m_inputBatches.size(); ++batchId) {
                m_taskQueue.push({nodeId, batchId});
            }
        }
    }

    void workerThread() {
        std::pair<size_t, size_t> task;
        while (m_taskQueue.try_pop(task)) {
            size_t nodeId = task.first;
            size_t batchId = task.second;

            if (!m_graph.isReady(nodeId, batchId)) {
                m_taskQueue.push(task); // Task not ready, requeue it
                continue;
            }

            executeNode(nodeId, batchId);
            updateDependencies(nodeId, batchId);
        }
    }

    void executeNode(size_t nodeId, size_t batchId) {
        GraphNode& node = m_graph.getNode(nodeId);

        // Iterate over each input field and corresponding MiniBatch
        for (const auto& inputField : node.getInputs()) {
            auto& inputMiniBatch = m_graph.getMiniBatch(nodeId, batchId, inputField.first);

            for (size_t i = 0; i < inputMiniBatch.size(); ++i) {
                node.setInput(inputField.first, inputMiniBatch.getData(i));
                node.execute();

                // Process each output MiniBatch
                for (const auto& outputField : node.getOutputs()) {
                    auto& outputMiniBatch = m_graph.getMiniBatch(nodeId, batchId, outputField.first);
                    outputMiniBatch.addData(node.getOutput(outputField.first));
                }
            }
        }
    }

    void updateDependencies(size_t nodeId, size_t batchId) {
        // Update the dependencies for downstream nodes
        for (size_t downstreamNodeId = 0; downstreamNodeId < m_graph.size(); ++downstreamNodeId) {
            if (m_graph.edgeExists(nodeId, downstreamNodeId)) {
                // Copy each output MiniBatch to corresponding input MiniBatch of downstream node
                for (const auto& outputField : m_graph.getNode(nodeId).getOutputs()) {
                    auto& outputMiniBatch = m_graph.getMiniBatch(nodeId, batchId, outputField.first);
                    auto& inputMiniBatch = m_graph.getMiniBatch(downstreamNodeId, batchId, outputField.first);
                    inputMiniBatch = outputMiniBatch;
                }
            }
        }
    }
};
