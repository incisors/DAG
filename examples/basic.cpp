// Minimal quick-start: build a two-node graph and run a few batches.

#include <iostream>
#include <map>
#include <vector>

#include "dag/dag.h"

using dag::Value;
using dag::make_value;
using Vals = std::vector<Value>;

int main() {
    dag::Graph g;

    g.add_node(dag::Node("multiply", {"in"}, {"out"},
                         [](const Vals& in) {
                             return Vals{make_value(in[0].as<double>() * 2.0)};
                         }));
    g.add_node(dag::Node("divide", {"out"}, {"result"},
                         [](const Vals& in) {
                             return Vals{make_value(in[0].as<double>() / 10.0)};
                         }));
    g.connect("multiply", "out", "divide", "out");
    g.finalize();

    dag::ThreadPool pool;
    dag::Executor exec(g, pool);

    auto r = exec.run({
        {{"in", make_value(1.0)}},
        {{"in", make_value(2.0)}},
        {{"in", make_value(3.0)}},
    });

    for (std::size_t b = 0; b < r.batches(); ++b) {
        std::cout << "batch " << b << " result = "
                  << r.get(b, "divide", "result").as<double>() << "\n";
    }
    return 0;
}
