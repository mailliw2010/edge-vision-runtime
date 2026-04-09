#include "evr/runtime/graph/graph.h"

#include <sstream>

namespace evr::runtime::graph {
namespace {

std::string EscapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

void AppendStringArray(std::ostringstream& out, const std::vector<std::string>& values) {
  out << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << '"' << EscapeJson(values[i]) << '"';
  }
  out << ']';
}

}  // namespace

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

std::string ToJson(const Graph& graph) {
  std::ostringstream out;
  out << '{';
  out << "\"id\":\"" << EscapeJson(graph.id) << "\",";
  out << "\"nodes\":";
  out << '[';
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    const auto& node = graph.nodes[i];
    if (i > 0) {
      out << ',';
    }
    out << '{';
    out << "\"id\":\"" << EscapeJson(node.id) << "\",";
    out << "\"type\":\"" << EscapeJson(node.type) << "\",";
    out << "\"name\":\"" << EscapeJson(node.name) << "\",";
    out << "\"inputs\":";
    AppendStringArray(out, node.inputs);
    out << ',';
    out << "\"outputs\":";
    AppendStringArray(out, node.outputs);
    out << ',';
    out << "\"config_ref\":\"" << EscapeJson(node.config_ref) << "\"";
    out << '}';
  }
  out << ']';
  out << ',';
  out << "\"edges\":";
  out << '[';
  for (std::size_t i = 0; i < graph.edges.size(); ++i) {
    const auto& edge = graph.edges[i];
    if (i > 0) {
      out << ',';
    }
    out << '{';
    out << "\"id\":\"" << EscapeJson(edge.id) << "\",";
    out << "\"from_node\":\"" << EscapeJson(edge.from_node) << "\",";
    out << "\"from_port\":\"" << EscapeJson(edge.from_port) << "\",";
    out << "\"to_node\":\"" << EscapeJson(edge.to_node) << "\",";
    out << "\"to_port\":\"" << EscapeJson(edge.to_port) << "\",";
    out << "\"type\":\"" << EscapeJson(edge.type) << "\"";
    out << '}';
  }
  out << ']';
  out << '}';
  return out.str();
}

}  // namespace evr::runtime::graph
