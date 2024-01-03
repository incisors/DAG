// Copyright (C) 2024 Haochen Jiang
//
// Authors: Haochen Jiang
// Date: 2024/1/2

/**
 * @file graph.h
 *
 * @brief Represents a computational graph capable of processing data through interconnected nodes.
 *
 * The Graph class manages a collection of interconnected GraphNodes, forming a directed graph.
 * It supports operations like adding nodes, creating edges, and executing computations across the graph.
 */

#pragma once

#include <vector>
#include "graph_node.h"
#include "mini_batch.h"

class Graph {
public:
    /**
     * @brief Default constructor for Graph.
     */
    Graph() : batchData(0) {} // initialize batch size to be 0
    
    /**
     * @brief Adds a new node to the graph.
     *
     * @param node The GraphNode to be added.
     * @return The ID (index) of the newly added node.
     */
    size_t addNode(GraphNode node) {
        size_t nodeId = nodes.size();
        nodes.push_back(std::move(node));
        // update adjacency list
        adjacencyList.push_back(std::vector<bool>(nodeId + 1, false));
        for (auto &row : adjacencyList) {
            row.resize(nodes.size(), false);
        }
        // update batch data
        for (auto& nodeBatches : batchData) {
            nodeBatches.push_back(std::unordered_map<std::string, MiniBatch>());
        }
        updateRoots();
        return nodeId;
    }

    /**
     * @brief Adds an edge from one node to another.
     *
     * @param from The ID of the source node.
     * @param to The ID of the destination node.
     * @return True if the edge is added successfully, false otherwise.
     */
    bool addEdge(size_t from, size_t to) {
        if (from < nodes.size() && to < nodes.size() && !createsCycle(from, to) && matchingIO(from, to)) {
            adjacencyList[from][to] = true;
            updateRoots(); // 更新根节点
            return true;
        }
        if (createsCycle(from, to)) {
            std::cout << "create cycle failed" << std::endl;
        }
        if (!matchingIO(from, to)) {
            std::cout << "matching IO failed" << std::endl;
        }
        return false; // 边未被添加
    }

    /**
     * @brief Retrieves a reference to a node by its ID.
     *
     * @param index The ID of the node.
     * @return A reference to the requested node.
     */
    GraphNode& getNode(size_t index) {
        return nodes.at(index);
    }

    /**
     * @brief Returns the number of nodes in the graph.
     *
     * @return The number of nodes.
     */
    size_t size() const {
        return nodes.size();
    }

    /**
     * @brief Checks if an edge exists between two nodes.
     *
     * @param from The ID of the source node.
     * @param to The ID of the destination node.
     * @return True if the edge exists, false otherwise.
     */
    bool edgeExists(size_t from, size_t to) const {
        return from < nodes.size() && to < nodes.size() && adjacencyList[from][to];
    }

    /**
     * @brief Checks if adding an edge would create a cycle in the graph.
     *
     * @param from The ID of the source node.
     * @param to The ID of the destination node.
     * @return True if adding the edge creates a cycle, false otherwise.
     */
    bool createsCycle(size_t from, size_t to) {
        // temporarily add the edge and check if a cycle is created
        adjacencyList[from][to] = true;

        bool flag = hasCycle();

        adjacencyList[from][to] = false;

        return flag;
    }

    /**
     * @brief Checks if the graph has any cycles.
     *
     * @return True if the graph contains cycles, false otherwise.
     */
    bool hasCycle() {
        std::vector<bool> visited(nodes.size(), false);
        std::vector<bool> recStack(nodes.size(), false); // 用于记录递归堆栈

        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!visited[i]) {
                if (dfs(i, visited, recStack)) {
                    return true; // 发现回路
                }
            }
        }
        return false; // 整个图中未找到回路
    }

    /**
     * @brief Prints the structure of the graph.
     */
    void printGraph() {
        for (size_t i = 0; i < nodes.size(); ++i) {
            std::cout << "Node " << i << ":\n";
            for (size_t j = 0; j < nodes.size(); ++j) {
                if (adjacencyList[i][j]) {
                    std::cout << "  Edge to Node " << j << "\n";
                }
            }
        }
        std::cout << "\n";
    }

    /**
     * @brief Prints the IDs of all root nodes in the graph.
     */
    void printRoots() {
        std::cout << "Root nodes: ";
        for (const auto& root : rootNodes) {
            std::cout << root << " ";
        }
        std::cout << "\n";
    }

    /**
     * @brief Checks if a node is ready for processing, based on the availability of input data.
     *
     * @param nodeId The ID of the node.
     * @param batchId The ID of the batch to check.
     * @return True if the node is ready, false otherwise.
     */
    bool isReady(size_t nodeId, size_t batchId) {
        const auto& node = nodes[nodeId];
        const auto& inputFields = node.getInputs();

        for (const auto& field : inputFields) {
            const auto& fieldName = field.first;
            if (batchData[nodeId][batchId].find(fieldName) == batchData[nodeId][batchId].end()) {
                return false; // if any input field is missing, the node is not ready
            }
        }

        return true; // all input fields are present
    }
    
    /**
     * @brief Retrieves a specific MiniBatch for a node.
     *
     * @param nodeId The ID of the node.
     * @param batchId The ID of the batch.
     * @param fieldName The name of the field.
     * @return A reference to the requested MiniBatch.
     */
    MiniBatch& getMiniBatch(size_t nodeId, size_t batchId, const std::string& fieldName) {
        return batchData[nodeId][batchId][fieldName];
    }

    /**
     * @brief Retrieves all MiniBatches for a node.
     *
     * @param nodeId The ID of the node.
     * @return A reference to the vector of MiniBatch maps for the node.
     */
    std::vector<std::unordered_map<std::string, MiniBatch>>& getNodeMiniBatches(size_t nodeId) {
        if (nodeId >= batchData.size()) {
            throw std::out_of_range("Node ID out of range.");
        }

        return batchData[nodeId];
    }

    /**
     * @brief Initializes the MiniBatch structures for all nodes.
     *
     * @param numBatches The number of batches to initialize for each node.
     */
    void initMiniBatches(size_t numBatches) {
        batchData.resize(nodes.size()); // 确保batchData的大小与节点数量一致
        for (size_t nodeId = 0; nodeId < nodes.size(); nodeId++) {
            auto& nodeBatches = batchData[nodeId];
            nodeBatches.resize(numBatches);

            for (auto& batch : nodeBatches) {
                const auto& inputs = nodes[nodeId].getInputs();
                const auto& outputs = nodes[nodeId].getOutputs();
                for (const auto& input : inputs) {
                    // initialize only MiniBatches that have not been set
                    if (batch.find(input.first) == batch.end()) {
                        batch[input.first] = MiniBatch();
                    }
                }
                for (const auto& output : outputs) {
                    // initialize only MiniBatches that have not been set
                    if (batch.find(output.first) == batch.end()) {
                        batch[output.first] = MiniBatch();
                    }
                }
            }
        }
    }

    /**
     * @brief Retrieves a list of all root nodes in the graph.
     *
     * @return A vector containing the IDs of all root nodes.
     */
    const std::vector<size_t> getRootNodes() const {
        return rootNodes;
    }

    /**
     * @brief Checks if a node is a root node.
     * 
     * @param nodeIndex The index of the node to check.
     * @return True if the node is a root node, false otherwise.
     */
    bool isRoot(size_t nodeIndex) {
        for (const auto& row : adjacencyList) {
            if (row[nodeIndex]) {
                return false; // 找到一条指向该节点的边
            }
        }
        return true; // 没有入边，是根节点
    }

private:
    std::vector<GraphNode> nodes; // Stores all nodes in the graph.
    std::vector<std::vector<bool>> adjacencyList; // Adjacency list representing edges.
    std::vector<size_t> rootNodes; // Stores IDs of all root nodes.
    std::vector<std::vector<std::unordered_map<std::string, MiniBatch>>> batchData; // Each node's MiniBatch data for each batch.

    /**
     * @brief Performs a depth-first search to check if a cycle exists in the graph.
     *
     * @param current The ID of the current node.
     * @param visited A vector of booleans indicating whether a node has been visited.
     * @param recStack A vector of booleans indicating whether a node is in the recursion stack.
     * @return True if a cycle is found, false otherwise.
     */
    bool dfs(size_t current, std::vector<bool>& visited, std::vector<bool>& recStack) {
        if (!visited[current]) {
            visited[current] = true;
            recStack[current] = true;

            for (size_t i = 0; i < nodes.size(); ++i) {
                if (adjacencyList[current][i]) {
                    if (!visited[i] && dfs(i, visited, recStack)) {
                        return true; // find a cycle
                    } else if (recStack[i]) {
                        return true; // find a cycle
                    }
                }
            }
        }
        recStack[current] = false; // remove the node from the recursion stack
        return false; // no cycle found
    }

    /**
     * @brief Checks if the input and output fields of two nodes match.
     * 
     * @param from The ID of the source node.
     * @param to The ID of the destination node.
     * @return True if the fields match, false otherwise.
     */
    bool matchingIO(size_t from, size_t to) {
        auto& fromOutputs = nodes[from].getOutputs();
        auto& toInputs = nodes[to].getInputs();

        for (const auto& output : fromOutputs) {
            if (toInputs.find(output.first) != toInputs.end()) {
                return true; // found a matching field
            }
        }
        return false; // no matching field found
    }
    
    /**
     * @brief Updates the list of root nodes.
     */
    void updateRoots() {
        rootNodes.clear();
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (isRoot(i)) {
                rootNodes.push_back(i);
            }
        }
    }

};