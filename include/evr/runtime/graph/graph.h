#pragma once

#include <string>
#include <vector>

namespace evr::runtime::graph {

struct Node {
  std::string id;
  std::string type;
  std::string subtype;
  std::string name;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::string config_ref;
};

struct Edge {
  std::string id;
  std::string from_node;
  std::string from_port;
  std::string to_node;
  std::string to_port;
  std::string type;
};

struct Graph {
  std::string id;
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};

std::string Describe(const Graph& graph);
std::string ToJson(const Graph& graph);

}  // namespace evr::runtime::graph
