/**
 * @file node.h
 * @brief Immutable definition of a graph node.
 *
 * A Node holds ONLY a definition: a name, its input/output port names, and a
 * pure transform function. It carries no per-execution data, so a single Node
 * instance is safe to share across every batch and every worker thread.
 *
 * (The previous design stored mutable input/output maps inside the node and
 * mutated them during execution, which produced a data race whenever the same
 * node ran for two batches concurrently. Separating definition from state is
 * what eliminates that race.)
 *
 * The compute function maps input port values to output port values, both
 * ordered to match inputs()/outputs(). It MUST be thread-safe / not rely on
 * shared mutable state, because the executor may invoke it concurrently for
 * different batches.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "value.h"

namespace dag {

/// inputs are ordered as Node::inputs(); the returned vector must be ordered as
/// Node::outputs() and have exactly outputs().size() elements.
using Compute = std::function<std::vector<Value>(const std::vector<Value>& inputs)>;

class Node {
public:
    Node(std::string name,
         std::vector<std::string> inputs,
         std::vector<std::string> outputs,
         Compute fn)
        : name_(std::move(name)),
          inputs_(std::move(inputs)),
          outputs_(std::move(outputs)),
          fn_(std::move(fn)) {}

    const std::string& name() const noexcept { return name_; }
    const std::vector<std::string>& inputs() const noexcept { return inputs_; }
    const std::vector<std::string>& outputs() const noexcept { return outputs_; }

    /// Run the transform. const: never touches shared mutable state.
    std::vector<Value> run(const std::vector<Value>& inputs) const {
        std::vector<Value> out = fn_(inputs);
        if (out.size() != outputs_.size()) {
            throw std::runtime_error("dag::Node '" + name_ + "' returned " +
                                     std::to_string(out.size()) + " outputs, expected " +
                                     std::to_string(outputs_.size()));
        }
        return out;
    }

    /// Index of a named input port, or -1 if absent.
    int input_index(const std::string& port) const noexcept {
        return index_of(inputs_, port);
    }
    /// Index of a named output port, or -1 if absent.
    int output_index(const std::string& port) const noexcept {
        return index_of(outputs_, port);
    }

private:
    static int index_of(const std::vector<std::string>& v, const std::string& port) noexcept {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] == port) return static_cast<int>(i);
        }
        return -1;
    }

    std::string name_;
    std::vector<std::string> inputs_;
    std::vector<std::string> outputs_;
    Compute fn_;
};

}  // namespace dag
