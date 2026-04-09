#include "evr/runtime/graph/graph.h"

#include <sstream>

namespace evr::runtime::graph {

std::string Describe(const Graph& graph) {
  std::ostringstream out;
  out << "graph[" << graph.id << "] nodes=" << graph.nodes.size() << " edges=" << graph.edges.size();
  for (const auto& node : graph.nodes) {
    out << "\n  node[" << node.id << "] type=" << node.type << " name=" << node.name;
  }
  for (const auto& edge : graph.edges) {
    out << "\n  edge[" << edge.id << "] " << edge.from_node << ":" << edge.from_port
        << " -> " << edge.to_node << ":" << edge.to_port << " type=" << edge.type;
  }
  return out.str();
}

}  // namespace evr::runtime::graph
