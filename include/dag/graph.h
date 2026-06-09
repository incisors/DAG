/**
 * @file graph.h
 * @brief Immutable DAG topology: nodes, port-to-port edges, and precomputed
 *        adjacency / indegree data used by the executor.
 *
 * Edges connect a producer port to a consumer port:
 *     (from_node.out_port) ---> (to_node.in_port)
 * which is explicit and unambiguous, unlike the old "match any field with the
 * same name" routing.
 *
 * finalize() is called once after the graph is built. It computes successor /
 * predecessor edge lists, verifies every input port has at most one producer,
 * and rejects cycles (Kahn's algorithm). After finalize() the topology is read
 * only and can be shared by many concurrent Executor runs.
 */
#pragma once

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "node.h"

namespace dag {

struct Edge {
    int from_node;  ///< producer node id
    int from_port;  ///< producer output-port index
    int to_node;    ///< consumer node id
    int to_port;    ///< consumer input-port index
};

class Graph {
public:
    /// Add a node; returns its id. Names must be unique.
    int add_node(Node node) {
        if (finalized_) throw std::logic_error("dag::Graph: add_node after finalize()");
        if (name_to_id_.count(node.name())) {
            throw std::runtime_error("dag::Graph: duplicate node name '" + node.name() + "'");
        }
        int id = static_cast<int>(nodes_.size());
        name_to_id_.emplace(node.name(), id);
        nodes_.push_back(std::move(node));
        return id;
    }

    int id_of(const std::string& name) const {
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            throw std::runtime_error("dag::Graph: unknown node '" + name + "'");
        }
        return it->second;
    }

    const Node& node(int id) const { return nodes_.at(static_cast<std::size_t>(id)); }
    std::size_t size() const noexcept { return nodes_.size(); }

    /// Connect from.out_port -> to.in_port (by node name + port name).
    void connect(const std::string& from, const std::string& out_port,
                 const std::string& to, const std::string& in_port) {
        if (finalized_) throw std::logic_error("dag::Graph: connect after finalize()");
        int fn = id_of(from), tn = id_of(to);
        int fp = nodes_[fn].output_index(out_port);
        int tp = nodes_[tn].input_index(in_port);
        if (fp < 0) throw std::runtime_error("dag::Graph: node '" + from + "' has no output port '" + out_port + "'");
        if (tp < 0) throw std::runtime_error("dag::Graph: node '" + to + "' has no input port '" + in_port + "'");
        edges_.push_back(Edge{fn, fp, tn, tp});
    }

    /// Build adjacency, validate single-producer-per-input, reject cycles.
    void finalize() {
        const std::size_t n = nodes_.size();
        out_edges_.assign(n, {});
        in_edges_.assign(n, {});
        indegree_.assign(n, 0);

        for (int e = 0; e < static_cast<int>(edges_.size()); ++e) {
            const Edge& ed = edges_[static_cast<std::size_t>(e)];
            out_edges_[ed.from_node].push_back(e);
            in_edges_[ed.to_node].push_back(e);
        }
        for (std::size_t i = 0; i < n; ++i) {
            indegree_[i] = static_cast<int>(in_edges_[i].size());
        }
        validate_single_producer();
        detect_cycle();
        finalized_ = true;
    }

    bool finalized() const noexcept { return finalized_; }

    const std::vector<int>& out_edges(int node) const { return out_edges_[static_cast<std::size_t>(node)]; }
    const std::vector<int>& in_edges(int node) const { return in_edges_[static_cast<std::size_t>(node)]; }
    const Edge& edge(int e) const { return edges_[static_cast<std::size_t>(e)]; }
    int indegree(int node) const { return indegree_[static_cast<std::size_t>(node)]; }
    bool is_root(int node) const { return indegree_[static_cast<std::size_t>(node)] == 0; }

private:
    void validate_single_producer() const {
        for (std::size_t v = 0; v < nodes_.size(); ++v) {
            std::unordered_set<int> seen_ports;
            for (int e : in_edges_[v]) {
                int port = edges_[static_cast<std::size_t>(e)].to_port;
                if (!seen_ports.insert(port).second) {
                    throw std::runtime_error(
                        "dag::Graph: input port '" + nodes_[v].inputs()[static_cast<std::size_t>(port)] +
                        "' of node '" + nodes_[v].name() + "' has more than one producer");
                }
            }
        }
    }

    void detect_cycle() const {
        const std::size_t n = nodes_.size();
        std::vector<int> indeg = indegree_;
        std::vector<int> ready;
        for (std::size_t i = 0; i < n; ++i) {
            if (indeg[i] == 0) ready.push_back(static_cast<int>(i));
        }
        std::size_t visited = 0;
        while (!ready.empty()) {
            int u = ready.back();
            ready.pop_back();
            ++visited;
            for (int e : out_edges_[static_cast<std::size_t>(u)]) {
                int v = edges_[static_cast<std::size_t>(e)].to_node;
                if (--indeg[static_cast<std::size_t>(v)] == 0) ready.push_back(v);
            }
        }
        if (visited != n) throw std::runtime_error("dag::Graph: cycle detected");
    }

    std::vector<Node> nodes_;
    std::unordered_map<std::string, int> name_to_id_;
    std::vector<Edge> edges_;

    // Precomputed once in finalize():
    std::vector<std::vector<int>> out_edges_;  ///< node -> outgoing edge ids
    std::vector<std::vector<int>> in_edges_;   ///< node -> incoming edge ids
    std::vector<int> indegree_;                ///< node -> number of incoming edges
    bool finalized_ = false;
};

}  // namespace dag
