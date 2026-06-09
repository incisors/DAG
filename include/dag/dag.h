/**
 * @file dag.h
 * @brief Umbrella header for the dag parallel compute engine. Include this.
 *
 *   #include "dag/dag.h"
 *
 * Quick start:
 *   dag::Graph g;
 *   g.add_node(dag::Node("double", {"x"}, {"y"},
 *       [](const std::vector<dag::Value>& in) {
 *           return std::vector<dag::Value>{ dag::make_value(in[0].as<int>() * 2) };
 *       }));
 *   g.finalize();
 *   dag::ThreadPool pool;
 *   dag::Executor exec(g, pool);
 *   auto r = exec.run({ {{"x", dag::make_value(21)}} });
 *   int y = r.get(0, "double", "y").as<int>();  // 42
 */
#pragma once

#include "value.h"
#include "node.h"
#include "graph.h"
#include "thread_pool.h"
#include "executor.h"
