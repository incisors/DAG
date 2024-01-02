#include <vector>
#include "graph_node.h"
#include "mini_batch.h"

class Graph {
public:
    Graph() : batchData(0) {} // 初始化batchData的大小为0

    size_t addNode(GraphNode node) {
        size_t nodeId = nodes.size();
        nodes.push_back(std::move(node));
        // 更新邻接列表
        adjacencyList.push_back(std::vector<bool>(nodeId + 1, false));
        for (auto &row : adjacencyList) {
            row.resize(nodes.size(), false);
        }
        // 为新节点添加空的MiniBatch映射
        for (auto& nodeBatches : batchData) {
            nodeBatches.push_back(std::unordered_map<std::string, MiniBatch>());
        }
        updateRoots();
        return nodeId;
    }

    // 添加一条从节点from到节点to的边
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

    // 获取特定编号的节点
    GraphNode& getNode(size_t index) {
        return nodes.at(index);
    }

    // 获取图的节点数量
    size_t size() const {
        return nodes.size();
    }

    // 检查是否存在从from到to的边
    bool edgeExists(size_t from, size_t to) const {
        return from < nodes.size() && to < nodes.size() && adjacencyList[from][to];
    }

    // 检查添加边是否会形成环
    bool createsCycle(size_t from, size_t to) {
        // 临时添加边以进行检测
        adjacencyList[from][to] = true;

        bool flag = hasCycle();

        adjacencyList[from][to] = false;

        return flag;
    }

    // 检查是否有环
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

    void printRoots() {
        std::cout << "Root nodes: ";
        for (const auto& root : rootNodes) {
            std::cout << root << " ";
        }
        std::cout << "\n";
    }

    bool isReady(size_t nodeId, size_t batchId) {
        const auto& node = nodes[nodeId];
        const auto& inputFields = node.getInputs();

        for (const auto& field : inputFields) {
            const auto& fieldName = field.first;
            if (batchData[nodeId][batchId].find(fieldName) == batchData[nodeId][batchId].end()) {
                return false; // 如果找不到某个输入字段的数据，则节点未就绪
            }
        }

        return true; // 所有输入字段的数据都已经存在
    }
    
    // 新增方法: 获取指定节点和batch的MiniBatch
    MiniBatch& getMiniBatch(size_t nodeId, size_t batchId, const std::string& fieldName) {
        return batchData[nodeId][batchId][fieldName];
    }

    std::vector<std::unordered_map<std::string, MiniBatch>>& getNodeMiniBatches(size_t nodeId) {
        if (nodeId >= batchData.size()) {
            throw std::out_of_range("Node ID out of range.");
        }

        return batchData[nodeId];
    }

    // 新增方法: 初始化MiniBatch数据结构
    void initMiniBatches(size_t numBatches) {
        batchData.resize(nodes.size()); // 确保batchData的大小与节点数量一致
        for (size_t nodeId = 0; nodeId < nodes.size(); nodeId++) {
            auto& nodeBatches = batchData[nodeId];
            nodeBatches.resize(numBatches);

            for (auto& batch : nodeBatches) {
                const auto& inputs = nodes[nodeId].getInputs();
                const auto& outputs = nodes[nodeId].getOutputs();
                for (const auto& input : inputs) {
                    // 仅初始化尚未设置的MiniBatch
                    if (batch.find(input.first) == batch.end()) {
                        batch[input.first] = MiniBatch();
                    }
                }
                for (const auto& output : outputs) {
                    // 仅初始化尚未设置的MiniBatch
                    if (batch.find(output.first) == batch.end()) {
                        batch[output.first] = MiniBatch();
                    }
                }
            }
        }
    }


    // 新增方法: 获取所有根节点
    const std::vector<size_t> getRootNodes() const {
        return rootNodes;
    }

    bool isRoot(size_t nodeIndex) {
        for (const auto& row : adjacencyList) {
            if (row[nodeIndex]) {
                return false; // 找到一条指向该节点的边
            }
        }
        return true; // 没有入边，是根节点
    }

private:
    std::vector<GraphNode> nodes; // 存储图的所有节点
    std::vector<std::vector<bool>> adjacencyList; // 邻接表表示边
    std::vector<size_t> rootNodes; // 存储所有根节点的编号
    // 每个节点对应一组MiniBatch，每个MiniBatch对应一个字段名到MiniBatch的映射
    std::vector<std::vector<std::unordered_map<std::string, MiniBatch>>> batchData;

    // 深度优先搜索辅助函数
    bool dfs(size_t current, std::vector<bool>& visited, std::vector<bool>& recStack) {
        if (!visited[current]) {
            visited[current] = true;
            recStack[current] = true;

            for (size_t i = 0; i < nodes.size(); ++i) {
                if (adjacencyList[current][i]) {
                    if (!visited[i] && dfs(i, visited, recStack)) {
                        return true; // 从当前节点找到回路
                    } else if (recStack[i]) {
                        return true; // 在递归堆栈中发现节点，表示存在回路
                    }
                }
            }
        }
        recStack[current] = false; // 移除当前节点从递归堆栈
        return false; // 从当前节点未找到回路
    }

    // 检查节点间的输入输出字段是否匹配
    bool matchingIO(size_t from, size_t to) {
        auto& fromOutputs = nodes[from].getOutputs();
        auto& toInputs = nodes[to].getInputs();

        for (const auto& output : fromOutputs) {
            if (toInputs.find(output.first) != toInputs.end()) {
                return true; // 找到至少一个匹配的字段名
            }
        }
        return false; // 未找到匹配字段
    }
    
    // 更新根节点信息
    void updateRoots() {
        rootNodes.clear();
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (isRoot(i)) {
                rootNodes.push_back(i);
            }
        }
    }

};