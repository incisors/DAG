/**
 * @file executor.h
 * @brief Dependency-driven, pipelined graph executor.
 *
 * Scheduling model (this is the heart of the rewrite):
 *   - The unit of work is a task = (batch, node).
 *   - Each task owns an atomic "pending" counter initialised to the node's
 *     indegree (number of incoming edges). A task becomes ready exactly when
 *     all its producers have written their results -- no polling, no requeuing,
 *     no busy-wait (the old executor re-pushed not-ready tasks and spun).
 *   - When a task finishes it writes each output into the matching input slot of
 *     each successor task and atomically decrements that successor's counter;
 *     the thread that drives a counter to zero schedules that successor.
 *
 * Pipelining: every (batch, node) is an independent task, so batch i can be at
 * node C while batch i+1 is still at node B. With B batches and a deep graph the
 * pipeline fills automatically -- this is the throughput win.
 *
 * Data race freedom:
 *   - Node definitions are immutable and shared read-only.
 *   - Each (batch, node) has its own input slots and result slot; different
 *     tasks never touch the same slot (single-producer-per-input is enforced by
 *     Graph::finalize), so concurrent writes target disjoint memory.
 *   - The pending counter's acq_rel decrement publishes the producer's slot
 *     write to whichever thread observes the zero and runs the consumer.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "graph.h"
#include "thread_pool.h"
#include "value.h"

namespace dag {

/// Outcome of a run.
enum class RunStatus { Ok, Failed, Cancelled };

/// A cancellation handle. Copy it into a run() call and trigger cancel() from
/// another thread; in-flight tasks stop launching work and the run drains.
/// Copies share one underlying flag (it is a shared_ptr internally).
class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() noexcept { flag_->store(true, std::memory_order_relaxed); }
    bool cancelled() const noexcept { return flag_->load(std::memory_order_relaxed); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

/// Holds every node's outputs for every batch after a run, plus the run status.
class ExecResult {
public:
    ExecResult(std::size_t batches, const Graph& graph) : graph_(&graph) {
        outputs_.assign(batches, std::vector<std::vector<Value>>(graph.size()));
    }

    std::size_t batches() const noexcept { return outputs_.size(); }

    RunStatus status() const noexcept { return status_; }
    bool ok() const noexcept { return status_ == RunStatus::Ok; }
    /// Name of the node whose transform threw (empty unless status()==Failed).
    const std::string& failed_node() const noexcept { return failed_node_; }
    /// Re-throw the node's exception if the run failed; throw on cancellation.
    void throw_if_error() const {
        if (status_ == RunStatus::Failed && error_) std::rethrow_exception(error_);
        if (status_ == RunStatus::Cancelled) throw std::runtime_error("dag: run was cancelled");
    }

    // Used by Executor only.
    void set_status(RunStatus s, std::exception_ptr e, std::string node) {
        status_ = s;
        error_ = std::move(e);
        failed_node_ = std::move(node);
    }

    /// Fetch a single output port value produced by @p node for @p batch.
    const Value& get(std::size_t batch, const std::string& node, const std::string& port) const {
        int id = graph_->id_of(node);
        int p = graph_->node(id).output_index(port);
        if (p < 0) throw std::runtime_error("dag::ExecResult: node '" + node + "' has no output port '" + port + "'");
        return outputs_.at(batch).at(static_cast<std::size_t>(id)).at(static_cast<std::size_t>(p));
    }

    /// All outputs of @p node for @p batch, ordered as node.outputs().
    const std::vector<Value>& outputs(std::size_t batch, const std::string& node) const {
        int id = graph_->id_of(node);
        return outputs_.at(batch).at(static_cast<std::size_t>(id));
    }

    // Used by Executor only.
    std::vector<Value>& slot(std::size_t batch, int node) {
        return outputs_[batch][static_cast<std::size_t>(node)];
    }

private:
    const Graph* graph_;
    std::vector<std::vector<std::vector<Value>>> outputs_;  // [batch][node][port]
    RunStatus status_ = RunStatus::Ok;
    std::exception_ptr error_;
    std::string failed_node_;
};

class Executor {
public:
    Executor(const Graph& graph, ThreadPool& pool) : graph_(graph), pool_(pool) {
        if (!graph_.finalized()) {
            throw std::logic_error("dag::Executor: call graph.finalize() before constructing an Executor");
        }
    }

    /// Run all @p batch_inputs through the graph and block until completion.
    /// batch_inputs[b] maps an external (unconnected) input-port name to a Value;
    /// those feed every node whose input port has no producer edge.
    /// Pass a CancelToken and call cancel() on it from another thread to abort.
    /// On a node throwing, the run stops launching new work, drains, and the
    /// returned ExecResult reports status()==Failed (never std::terminate).
    ExecResult run(const std::vector<std::map<std::string, Value>>& batch_inputs,
                   CancelToken cancel = {}) {
        // RunState lives on this stack frame and is owned solely by run(). Tasks
        // capture a raw pointer, never a shared_ptr, so a worker can never become
        // the thread that destroys it. run() blocks in wait() until the in-flight
        // task count drains to zero, then tears RunState down -- race-free.
        RunState state(graph_, pool_, batch_inputs, cancel);
        state.start();
        state.wait();

        ExecResult out = std::move(state.result);
        if (state.has_error.load(std::memory_order_relaxed)) {
            out.set_status(RunStatus::Failed, state.first_error,
                           graph_.node(state.failed_node).name());
        } else if (cancel.cancelled()) {
            out.set_status(RunStatus::Cancelled, nullptr, std::string{});
        }
        return out;
    }

private:
    struct RunState {
        const Graph& graph;
        ThreadPool& pool;
        CancelToken cancel;
        std::size_t B;  // batches
        std::size_t N;  // nodes

        std::unique_ptr<std::atomic<int>[]> pending;  // size B*N
        std::vector<std::vector<Value>> input_slots;  // size B*N, each sized to node's #inputs
        ExecResult result;

        // Completion is tracked by the number of tasks that are scheduled but not
        // yet finished. A task that fails (or is skipped on cancel) simply does
        // NOT schedule its successors, so those branches add nothing to the count
        // and it still drains to zero -- no deadlock, unlike a fixed total count.
        std::atomic<long> inflight{0};

        // First node failure wins the CAS and records the exception.
        std::atomic<bool> has_error{false};
        std::exception_ptr first_error;
        int failed_node = -1;

        std::mutex done_mu;
        std::condition_variable done_cv;
        bool finished = false;

        RunState(const Graph& g, ThreadPool& p,
                 const std::vector<std::map<std::string, Value>>& batch_inputs,
                 CancelToken ct)
            : graph(g),
              pool(p),
              cancel(std::move(ct)),
              B(batch_inputs.size()),
              N(g.size()),
              result(batch_inputs.size(), g) {
            const std::size_t T = B * N;
            pending.reset(new std::atomic<int>[T]);
            input_slots.assign(T, {});

            // Per-node mask of which input ports are fed by an edge.
            std::vector<std::vector<char>> connected(N);
            for (std::size_t v = 0; v < N; ++v) {
                connected[v].assign(graph.node(static_cast<int>(v)).inputs().size(), 0);
                for (int e : graph.in_edges(static_cast<int>(v))) {
                    connected[v][graph.edge(e).to_port] = 1;
                }
            }

            for (std::size_t b = 0; b < B; ++b) {
                const auto& inmap = batch_inputs[b];
                for (std::size_t v = 0; v < N; ++v) {
                    const Node& nd = graph.node(static_cast<int>(v));
                    std::size_t i = b * N + v;
                    input_slots[i].resize(nd.inputs().size());
                    pending[i].store(graph.indegree(static_cast<int>(v)), std::memory_order_relaxed);
                    // Pre-fill external (unconnected) input ports from this batch's map.
                    for (std::size_t p = 0; p < nd.inputs().size(); ++p) {
                        if (!connected[v][p]) {
                            auto it = inmap.find(nd.inputs()[p]);
                            if (it != inmap.end()) input_slots[i][p] = it->second;
                        }
                    }
                }
            }
        }

        std::size_t index(std::size_t b, int node) const {
            return b * N + static_cast<std::size_t>(node);
        }

        void wait() {
            std::unique_lock<std::mutex> lock(done_mu);
            done_cv.wait(lock, [this] { return finished; });
        }

        void signal_done() {
            // Notify under the lock so wait() cannot return (and destroy us) while
            // notify_one is still touching done_cv.
            std::lock_guard<std::mutex> lock(done_mu);
            finished = true;
            done_cv.notify_one();
        }

        void start() {
            // Start with one "starter" token so the count cannot hit zero while we
            // are still seeding root tasks. Dropped at the end of start().
            inflight.store(1, std::memory_order_relaxed);
            for (std::size_t b = 0; b < B; ++b) {
                for (std::size_t v = 0; v < N; ++v) {
                    if (graph.is_root(static_cast<int>(v))) schedule(b, static_cast<int>(v));
                }
            }
            finish_one();  // drop the starter; signals done if the graph was empty
        }

        void schedule(std::size_t b, int node) {
            inflight.fetch_add(1, std::memory_order_relaxed);
            // Pack (b, node) into the flat task index so the submitted lambda
            // captures just {this, size_t} == 16 bytes and fits std::function's
            // small-buffer storage -- no heap allocation per task submission.
            const std::size_t idx = index(b, node);
            pool.submit([this, idx] { this->execute(idx); });
        }

        // Decrement the in-flight count; the task that drives it to zero is the
        // last one and signals completion.
        void finish_one() {
            if (inflight.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                signal_done();
            }
        }

        void execute(std::size_t idx) {
            const std::size_t b = idx / N;
            const int node = static_cast<int>(idx % N);
            // Skip the work if the run has already failed or been cancelled: do not
            // run the node and do not schedule successors, so in-flight tasks drain
            // quickly. `outs` is scoped to be destroyed before finish_one().
            if (!has_error.load(std::memory_order_acquire) && !cancel.cancelled()) {
                try {
                    const Node& nd = graph.node(node);
                    std::vector<Value> outs = nd.run(input_slots[index(b, node)]);

                    result.slot(b, node) = outs;  // publish for querying

                    for (int e : graph.out_edges(node)) {
                        const Edge& ed = graph.edge(e);
                        std::size_t si = index(b, ed.to_node);
                        input_slots[si][static_cast<std::size_t>(ed.to_port)] =
                            outs[static_cast<std::size_t>(ed.from_port)];
                        // acq_rel: acquire prior producers' slot writes, release ours.
                        if (pending[si].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                            schedule(b, ed.to_node);
                        }
                    }
                } catch (...) {
                    // First failure records the exception + node; the rest are
                    // dropped. We deliberately do NOT schedule successors.
                    if (!has_error.exchange(true, std::memory_order_acq_rel)) {
                        first_error = std::current_exception();
                        failed_node = node;
                    }
                }
            }

            finish_one();
        }
    };

    const Graph& graph_;
    ThreadPool& pool_;
};

}  // namespace dag
