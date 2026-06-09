// Demonstrates batch pipelining: a deep chain of "slow" stages. Pushing many
// batches lets stage k of batch i overlap stage k-1 of batch i+1, so wall-clock
// approaches (stages + batches - 1) * stage_time instead of stages * batches *
// stage_time. This is the throughput property the rewrite was built to get.

#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "dag/dag.h"

using dag::Value;
using dag::make_value;
using Vals = std::vector<Value>;

int main() {
    const int kStages = 4;
    const int kBatches = 8;
    const auto kStageWork = std::chrono::milliseconds(25);

    dag::Graph g;
    std::string prev_node, prev_port = "x";
    for (int s = 0; s < kStages; ++s) {
        std::string name = "stage" + std::to_string(s);
        std::string in_port = (s == 0) ? "x" : ("y" + std::to_string(s - 1));
        std::string out_port = "y" + std::to_string(s);
        g.add_node(dag::Node(name, {in_port}, {out_port},
                             [=](const Vals& in) {
                                 std::this_thread::sleep_for(kStageWork);  // simulate work
                                 return Vals{make_value(in[0].as<long>() + 1)};
                             }));
        if (s > 0) g.connect(prev_node, prev_port, name, in_port);
        prev_node = name;
        prev_port = out_port;
    }
    g.finalize();

    std::vector<std::map<std::string, Value>> batches;
    for (int i = 0; i < kBatches; ++i) batches.push_back({{"x", make_value(static_cast<long>(i))}});

    dag::ThreadPool pool;  // hardware_concurrency workers
    dag::Executor exec(g, pool);

    auto t0 = std::chrono::steady_clock::now();
    auto r = exec.run(batches);
    auto t1 = std::chrono::steady_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    long serial_ms = static_cast<long>(kStages) * kBatches * kStageWork.count();

    std::cout << "stages=" << kStages << " batches=" << kBatches
              << " stage_work=" << kStageWork.count() << "ms\n";
    std::cout << "wall clock : " << ms << " ms\n";
    std::cout << "fully serial: " << serial_ms << " ms (upper bound)\n";
    std::cout << "last result : " << r.get(kBatches - 1, "stage" + std::to_string(kStages - 1),
                                           "y" + std::to_string(kStages - 1)).as<long>()
              << " (expected " << (kBatches - 1) + kStages << ")\n";
    return 0;
}
