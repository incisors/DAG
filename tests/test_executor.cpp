// Correctness tests for the dag engine. No external test framework: a tiny
// CHECK macro reports failures and the process exit code signals pass/fail.
//
// The mingw-w64 toolchain ships no ASan/TSan runtime; for UB detection use
// clang's trap mode (needs no runtime):
//   clang++ -std=c++17 -O1 -fsanitize=undefined,bounds,integer -fsanitize-trap=all ...

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "dag/dag.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << "CHECK FAILED: " << #cond << "  @ " << __FILE__ << ":"  \
                      << __LINE__ << "\n";                                       \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

inline bool approx(double a, double b) { return std::abs(a - b) < 1e-9; }

using dag::Value;
using dag::make_value;
using Vals = std::vector<Value>;

// in[0] is a double; out[0] = in * factor.
dag::Node scale_node(const std::string& name, const std::string& in_port,
                     const std::string& out_port, double factor) {
    return dag::Node(name, {in_port}, {out_port},
                     [factor](const Vals& in) {
                         return Vals{make_value(in[0].as<double>() * factor)};
                     });
}

// ---- Test 1: simple chain  multiply(x2) -> divide(/10) ---------------------
void test_chain() {
    dag::Graph g;
    g.add_node(scale_node("mul", "in", "mout", 2.0));
    g.add_node(scale_node("div", "mout", "dout", 0.1));
    g.connect("mul", "mout", "div", "mout");
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    std::vector<std::map<std::string, Value>> batches = {
        {{"in", make_value(1.0)}},
        {{"in", make_value(2.0)}},
        {{"in", make_value(3.0)}},
    };
    auto r = exec.run(batches);

    CHECK(r.batches() == 3);
    CHECK(approx(r.get(0, "div", "dout").as<double>(), 0.2));
    CHECK(approx(r.get(1, "div", "dout").as<double>(), 0.4));
    CHECK(approx(r.get(2, "div", "dout").as<double>(), 0.6));
}

// ---- Test 2: diamond fan-out / fan-in (exercises a multi-input join) --------
// src(v=x) -> mul(m=v*2), add(a=v+10) -> sink(r = m + a)
void test_diamond() {
    dag::Graph g;
    g.add_node(dag::Node("src", {"x"}, {"v"},
                         [](const Vals& in) { return Vals{in[0]}; }));
    g.add_node(dag::Node("mul", {"v"}, {"m"},
                         [](const Vals& in) { return Vals{make_value(in[0].as<int>() * 2)}; }));
    g.add_node(dag::Node("add", {"v"}, {"a"},
                         [](const Vals& in) { return Vals{make_value(in[0].as<int>() + 10)}; }));
    g.add_node(dag::Node("sink", {"m", "a"}, {"r"},
                         [](const Vals& in) {
                             return Vals{make_value(in[0].as<int>() + in[1].as<int>())};
                         }));
    g.connect("src", "v", "mul", "v");
    g.connect("src", "v", "add", "v");
    g.connect("mul", "m", "sink", "m");
    g.connect("add", "a", "sink", "a");
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    // x=5 -> v=5, m=10, a=15, r=25
    std::vector<std::map<std::string, Value>> batches;
    const int kBatches = 200;
    for (int i = 0; i < kBatches; ++i) batches.push_back({{"x", make_value(i)}});
    auto r = exec.run(batches);

    for (int i = 0; i < kBatches; ++i) {
        CHECK(r.get(static_cast<std::size_t>(i), "sink", "r").as<int>() == (i * 2) + (i + 10));
    }
}

// ---- Test 3: heavy concurrency -- many wide independent chains --------------
// Each batch fans through a wide layer; run with TSan to prove no data race on
// shared node definitions.
void test_heavy() {
    dag::Graph g;
    const int width = 16;
    g.add_node(dag::Node("root", {"x"}, {"x"},
                         [](const Vals& in) { return Vals{in[0]}; }));
    std::vector<std::string> leaf_inputs, leaf_names;
    for (int i = 0; i < width; ++i) {
        std::string n = "w" + std::to_string(i);
        g.add_node(dag::Node(n, {"x"}, {"y"},
                             [i](const Vals& in) { return Vals{make_value(in[0].as<long>() + i)}; }));
        g.connect("root", "x", n, "x");
        leaf_names.push_back(n);
    }
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    std::vector<std::map<std::string, Value>> batches;
    const int kBatches = 500;
    for (int i = 0; i < kBatches; ++i) batches.push_back({{"x", make_value(static_cast<long>(i))}});
    auto r = exec.run(batches);

    for (int b = 0; b < kBatches; ++b) {
        for (int i = 0; i < width; ++i) {
            CHECK(r.get(static_cast<std::size_t>(b), leaf_names[i], "y").as<long>() == b + i);
        }
    }
}

// ---- Test 4: structural validation ----------------------------------------
void test_validation() {
    // cycle rejected
    {
        dag::Graph g;
        g.add_node(scale_node("a", "in", "out", 1.0));
        g.add_node(scale_node("b", "out", "out2", 1.0));
        g.connect("a", "out", "b", "out");
        g.connect("b", "out2", "a", "in");  // back edge -> cycle
        bool threw = false;
        try { g.finalize(); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    // two producers into one input port rejected
    {
        dag::Graph g;
        g.add_node(scale_node("p1", "in", "o", 1.0));
        g.add_node(scale_node("p2", "in", "o", 1.0));
        g.add_node(dag::Node("c", {"o"}, {"z"}, [](const Vals& in) { return Vals{in[0]}; }));
        g.connect("p1", "o", "c", "o");
        g.connect("p2", "o", "c", "o");
        bool threw = false;
        try { g.finalize(); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

// ---- Test 5: a throwing node fails the run (never terminates / deadlocks) ---
void test_error() {
    dag::Graph g;
    g.add_node(scale_node("mul", "in", "mout", 2.0));
    g.add_node(dag::Node("boom", {"mout"}, {"dout"},
                         [](const Vals&) -> Vals { throw std::runtime_error("kaboom"); }));
    g.connect("mul", "mout", "boom", "mout");
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    std::vector<std::map<std::string, Value>> batches;
    for (int i = 0; i < 50; ++i) batches.push_back({{"in", make_value(1.0)}});

    auto r = exec.run(batches);  // must return -- not std::terminate, not hang
    CHECK(!r.ok());
    CHECK(r.status() == dag::RunStatus::Failed);
    CHECK(r.failed_node() == "boom");

    bool rethrew = false;
    try {
        r.throw_if_error();
    } catch (const std::runtime_error& e) {
        rethrew = (std::string(e.what()) == "kaboom");
    }
    CHECK(rethrew);

    // Diamond where one branch throws still drains (no hang) and reports Failed.
    dag::Graph g2;
    g2.add_node(dag::Node("src", {"x"}, {"v"}, [](const Vals& in) { return Vals{in[0]}; }));
    g2.add_node(dag::Node("ok", {"v"}, {"a"}, [](const Vals& in) { return Vals{in[0]}; }));
    g2.add_node(dag::Node("bad", {"v"}, {"b"},
                          [](const Vals&) -> Vals { throw std::runtime_error("x"); }));
    g2.add_node(dag::Node("sink", {"a", "b"}, {"r"}, [](const Vals& in) { return Vals{in[0]}; }));
    g2.connect("src", "v", "ok", "v");
    g2.connect("src", "v", "bad", "v");
    g2.connect("ok", "a", "sink", "a");
    g2.connect("bad", "b", "sink", "b");
    g2.finalize();
    dag::Executor exec2(g2, pool);
    std::vector<std::map<std::string, Value>> b2;
    for (int i = 0; i < 100; ++i) b2.push_back({{"x", make_value(i)}});
    auto r2 = exec2.run(b2);
    CHECK(r2.status() == dag::RunStatus::Failed);
    CHECK(r2.failed_node() == "bad");
}

// ---- Test 6: cancellation drains and reports Cancelled ----------------------
void test_cancel() {
    dag::Graph g;
    g.add_node(dag::Node("slow", {"in"}, {"out"},
                         [](const Vals& in) {
                             std::this_thread::sleep_for(std::chrono::milliseconds(5));
                             return Vals{in[0]};
                         }));
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    std::vector<std::map<std::string, Value>> batches;
    for (int i = 0; i < 100; ++i) batches.push_back({{"in", make_value(i)}});

    // (a) Pre-cancelled token: every task skips, returns Cancelled quickly.
    dag::CancelToken pre;
    pre.cancel();
    auto r1 = exec.run(batches, pre);
    CHECK(r1.status() == dag::RunStatus::Cancelled);

    // (b) Cancel mid-run from another thread: must return without hanging.
    dag::CancelToken tok;
    std::thread canceller([&tok] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tok.cancel();
    });
    auto r2 = exec.run(batches, tok);
    canceller.join();
    CHECK(r2.status() == dag::RunStatus::Cancelled || r2.ok());
}

}  // namespace

int main() {
    test_chain();
    test_diamond();
    test_heavy();
    test_validation();
    test_error();
    test_cancel();

    if (g_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
