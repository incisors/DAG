# dag — a high-throughput parallel compute engine

A small, header-only **C++17** library for running computations structured as a
directed acyclic graph (DAG). It is built for **throughput**: input is split into
batches that are *pipelined* through the graph (batch *i* can be at node C while
batch *i+1* is still at node B) and independent batches run in parallel on a
persistent **work-stealing** thread pool. A node that throws fails the run
cleanly instead of crashing the process, and a run can be cancelled mid-flight.

About 760 lines across six headers, no third-party dependencies.

```
include/dag/
  value.h        type-erased, zero-copy data with small-buffer optimization
  node.h         immutable node = ports + pure transform
  graph.h        port-to-port topology + validation
  thread_pool.h  persistent work-stealing pool
  executor.h     dependency-driven pipelined scheduler + results
  dag.h          umbrella header (include this)
```

---

## Quick start

```cpp
#include "dag/dag.h"

dag::Graph g;
g.add_node(dag::Node("multiply", {"in"}, {"out"},
    [](const std::vector<dag::Value>& in) {
        return std::vector<dag::Value>{ dag::make_value(in[0].as<double>() * 2.0) };
    }));
g.add_node(dag::Node("divide", {"out"}, {"result"},
    [](const std::vector<dag::Value>& in) {
        return std::vector<dag::Value>{ dag::make_value(in[0].as<double>() / 10.0) };
    }));
g.connect("multiply", "out", "divide", "out");   // producer port -> consumer port
g.finalize();                                     // validate + precompute topology

dag::ThreadPool pool;                             // persistent, reused across runs
dag::Executor exec(g, pool);

auto r = exec.run({                               // one map per batch: external inputs
    {{"in", dag::make_value(1.0)}},
    {{"in", dag::make_value(2.0)}},
    {{"in", dag::make_value(3.0)}},
});

if (r.ok()) double d = r.get(0, "divide", "result").as<double>();   // 0.2
else        r.throw_if_error();
```

### Errors & cancellation

If a node's transform throws, the run does **not** `std::terminate` or deadlock:
it stops launching new work, drains the tasks already in flight, and reports the
failure (downstream nodes of the failure simply don't run).

```cpp
auto r = exec.run(batches);
r.status();         // dag::RunStatus::Ok | Failed | Cancelled
r.ok();             // bool
r.failed_node();    // name of the node that threw (when Failed)
r.throw_if_error(); // re-throw the node's exception, or throw on cancellation

dag::CancelToken token;                          // cancel from another thread:
std::thread([&]{ token.cancel(); }).detach();
auto r2 = exec.run(batches, token);              // returns promptly; status()==Cancelled
```

---

## Build

Header-only — put `include/` on your include path and link threads. With CMake:

```
cmake -S . -B build -G Ninja        # any generator works; Ninja shown
cmake --build build
ctest --test-dir build              # correctness tests
./build/example_pipeline            # the pipelining speedup, live
```

Or just compile directly:

```
g++ -std=c++17 -O2 -pthread -Iinclude tests/test_executor.cpp -o test && ./test
```

---

## Core concepts

| Type | Role |
|---|---|
| `dag::Value` | One immutable, type-erased datum flowing along an edge. `make_value(x)` to create, `v.as<T>()` to read, `v.is<T>()` / `v.empty()` to inspect. Holds any type, including a whole chunk like `std::vector<T>`. |
| `dag::Node` | `Node(name, inputs, outputs, fn)`. `inputs`/`outputs` are **port names**; `fn` maps input-port values to output-port values: `std::vector<Value>(const std::vector<Value>&)`. |
| `dag::Graph` | `add_node`, `connect(from, outPort, to, inPort)`, then `finalize()` once. |
| `dag::ThreadPool` | `ThreadPool(n = hardware_concurrency())`. Create once, reuse for many runs. |
| `dag::Executor` | `Executor(graph, pool)`; `run(batchInputs, cancel = {})` → `ExecResult`. |
| `dag::ExecResult` | `get(batch, node, port).as<T>()`, plus `status()` / `ok()` / `failed_node()` / `throw_if_error()`. |

A **batch** is one unit of data fed through the whole graph; a run processes many
batches. Each node's **input/output ports** are named; an edge wires one
producer port to one consumer port. A node input port with no incoming edge is an
*external* input, supplied per batch in the `run()` argument by port name.

> Tip: for SIMD-style bulk work, put a `std::vector<T>` *chunk* in each `Value`
> and let the node loop over it — the per-task overhead is then amortized across
> the whole chunk. (A dedicated "listwise" API would add nothing for throughput.)

---

## How it works (principles)

### 1. Zero-copy data with small-buffer optimization
`Value` carries data so that **fan-out never deep-copies**. Internally it has both
a 16-byte inline buffer and a `std::shared_ptr<const void>`:

- A **small, trivially-copyable** type (`int`, `double`, a small struct — ≤ 16 B)
  is stored **inline**, with *no heap allocation*. Copying such a `Value` is a
  16-byte `memcpy`.
- Anything **larger or non-trivial** (e.g. `std::string`, `std::vector<T>`) is
  held by the `shared_ptr`, so copying it is just a refcount bump — a producer
  feeding three consumers shares one payload.

The two storages *coexist* (it is not a `union`), and the inline buffer only ever
holds trivially-copyable/destructible types, so the **compiler-generated**
copy/move/destructor are automatically correct — no hand-written lifetime code.
The cost is a larger `Value` (48 bytes); the win is dropping one heap allocation
per scalar, which dominates at high throughput. Type recovery is `typeid` compare
+ `static_cast` (no `dynamic_cast`).

### 2. Immutable nodes, per-task state → no data race
A `Node` holds only a *definition*: ports + a pure function. It has **no mutable
per-run state**, so one node object is shared read-only across every batch and
every worker thread. All mutable data lives in per-`(batch, node)` slots created
fresh for each run. (The old prototype mutated one shared node object across
batches — a data race; separating definition from state is the fix.)

### 3. Dependency-driven scheduling, automatic pipelining
The unit of work is a **task = `(batch, node)`**. Each task has an atomic
`pending` counter initialized to the node's indegree. When a producer task
finishes it writes its output into each successor's input slot and atomically
decrements that successor's counter; the thread that drives a counter to **zero**
schedules that successor. So a task runs *exactly* when its inputs are ready —
**no polling, no busy-wait, no requeuing**.

Because every `(batch, node)` is an independent task, batches **overlap across
stages for free**: while batch *i* is at the last node, batch *i+1* is at the
first. With a deep graph and many batches the pipeline fills itself. (The
`pipeline` example: a 4-stage / 8-batch graph runs ~7× faster than fully serial.)

Topology is precomputed once in `finalize()` — successor/predecessor edge lists
and indegrees — and `finalize()` rejects cycles (Kahn's algorithm) and rejects
two producers feeding one input port. After that the graph is read-only and a
single graph can drive many concurrent runs.

### 4. In-flight completion counter (this is what makes failure/cancel safe)
Completion is tracked by the number of tasks **scheduled but not yet finished**,
*not* by a fixed total. `schedule()` increments it; every task decrements it when
done; a one-time "starter" token keeps it from hitting zero while roots are still
being seeded. The task that drives it to zero signals the waiting `run()`.

This is precisely what lets **errors and cancellation drain cleanly**: a node
that throws is caught (the first failure wins a CAS and records the node id +
`std::exception_ptr`) and simply **does not schedule its successors** — those
branches add nothing to the in-flight count, so it still reaches zero. A *fixed
total* counter would wait forever for tasks that can never run → deadlock.
Cancellation works the same way: in-flight tasks see the flag, skip their work,
and don't schedule successors, so the count drains fast and `run()` returns
`Cancelled`.

### 5. Work-stealing thread pool
Each worker owns its **own task deque** (with its own mutex):

- A task submitted **from a worker** goes to that worker's own deque, pushed and
  popped at the **front (LIFO)** — a freshly scheduled successor runs on the same
  worker while its producer's output is still hot in cache.
- A task submitted **from outside** (the main thread seeding roots) is
  round-robined across deques.
- An idle worker **steals** from the *back* of another worker's deque.

So the common case touches only a lightly-contended local lock instead of one
global queue. Two non-obvious pitfalls had to be solved (both were caught by
benchmarking — the naive versions were *slower* with more threads):

- **Waking sleepers cheaply (eventcount).** `submit` publishes the task, then
  wakes a worker *only if one is actually asleep* (`sleepers_ > 0`). When every
  worker is busy — the common case under load — a submit is just two atomics: no
  global lock, no `notify` syscall per task. Correctness against a worker
  going to sleep concurrently is a seq-cst *Dekker handshake*: `submit` does
  `pending_++` then reads `sleepers_`; the worker does `sleepers_++` then re-reads
  `pending_` — at least one side observes the other.
- **No sleep/wake churn (spin-before-sleep).** A worker that finds no work
  **spins briefly** (yielding) before sleeping. Without this, a fast many-core
  pool fed by a single producer would churn sleep↔wake *per task* and collapse
  under oversubscription; the short spin rides out brief work droughts.

For compute-bound nodes this scales near-linearly — about **14× on 24 logical
cores**. (Very fine-grained tasks stay overhead/Amdahl-bound, as expected for any
scheduler; batch your work into chunks if so.)

### 6. Race-free teardown
`RunState` (all the per-run mutable data) lives on `run()`'s stack and is owned
**solely** by `run()`. Tasks capture a **raw pointer**, never a `shared_ptr`, so a
worker can never become the thread that destroys it. `run()` blocks until the
in-flight count hits zero; the final task signals completion under a mutex
(notify-under-lock) only after it has finished touching all shared state, so the
stack frame is torn down synchronously with no worker still inside.

---

## Status & roadmap

**Done:** dependency-driven pipelined scheduling · port-to-port graph with cycle &
single-producer validation · zero-copy `Value` with small-buffer optimization ·
no-alloc task submission (16-byte lambdas in `std::function`'s inline storage) ·
work-stealing pool (eventcount wake + spin-before-sleep, no oversubscription
collapse) · error propagation & cancellation · tests, examples, benchmarks.

**Deliberately deferred:** a listwise / whole-batch node API — putting a
`std::vector<T>` chunk in a `Value` already gives SIMD-friendly bulk processing,
so it would be P3 ergonomics, not throughput.

**Next candidates:** eliminate the remaining per-task allocation (`Node::run`
returns a fresh `std::vector<Value>`); async / non-blocking I/O nodes (yield the
worker while waiting on I/O); backpressure via bounded queues; conditional /
dynamic routing; Chrome-trace profiling; a generic GPU path (memory pool +
streams); optional config-driven (YAML) graph construction.

---

> **Toolchain note (development).** mingw-w64 ships no ASan/TSan runtime. For UB
> detection use clang's **trap mode** (needs no runtime):
> `clang++ -std=c++17 -O1 -fsanitize=undefined,bounds,integer -fsanitize-trap=all -Iinclude tests/test_executor.cpp`
