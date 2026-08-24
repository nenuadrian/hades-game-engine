#include "blueprint_graph.hpp"

#include <algorithm>
#include <unordered_set>

namespace hades
{
  namespace
  {
    nlohmann::json variable_to_json(const BlueprintVariable &variable)
    {
      nlohmann::json out;
      out["name"] = variable.name;
      out["type"] = value_type_name(variable.type);
      out["default"] = variable.defaultValue.to_json();
      out["exposed"] = variable.exposed;
      if (!variable.tooltip.empty())
      {
        out["tooltip"] = variable.tooltip;
      }
      return out;
    }

    BlueprintVariable variable_from_json(const nlohmann::json &in)
    {
      BlueprintVariable variable;
      variable.name = in.value("name", std::string());

      ValueType type = ValueType::Float;
      value_type_from_name(in.value("type", std::string("float")), type);
      variable.type = type;

      variable.defaultValue = BlueprintValue::from_json(
          in.contains("default") ? in.at("default") : nlohmann::json(nullptr), type);
      variable.exposed = in.value("exposed", true);
      variable.tooltip = in.value("tooltip", std::string());
      return variable;
    }

    nlohmann::json graph_to_json(const BlueprintGraph &graph)
    {
      nlohmann::json nodes = nlohmann::json::array();
      for (const auto &node : graph.nodes)
      {
        nlohmann::json entry;
        entry["id"] = node.id;
        entry["type"] = node.type;
        entry["x"] = node.x;
        entry["y"] = node.y;
        if (!node.comment.empty())
        {
          entry["comment"] = node.comment;
        }
        if (!node.config.is_null() && !node.config.empty())
        {
          entry["config"] = node.config;
        }
        if (!node.pinDefaults.empty())
        {
          nlohmann::json defaults = nlohmann::json::object();
          for (const auto &[pin, value] : node.pinDefaults)
          {
            nlohmann::json slot;
            slot["type"] = value_type_name(value.type());
            slot["value"] = value.to_json();
            defaults[pin] = slot;
          }
          entry["defaults"] = defaults;
        }
        nodes.push_back(std::move(entry));
      }

      nlohmann::json links = nlohmann::json::array();
      for (const auto &link : graph.links)
      {
        links.push_back(nlohmann::json{
            {"kind", link.kind == BlueprintLinkKind::Exec ? "exec" : "data"},
            {"from", nlohmann::json{{"node", link.from.node}, {"pin", link.from.pin}}},
            {"to", nlohmann::json{{"node", link.to.node}, {"pin", link.to.pin}}}});
      }

      return nlohmann::json{{"nodes", std::move(nodes)}, {"links", std::move(links)}};
    }

    bool graph_from_json(const nlohmann::json &in, BlueprintGraph &out, std::string *errorMessage)
    {
      out.nodes.clear();
      out.links.clear();

      if (!in.is_object())
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "graph must be a JSON object";
        }
        return false;
      }

      if (in.contains("nodes"))
      {
        if (!in.at("nodes").is_array())
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "graph.nodes must be an array";
          }
          return false;
        }

        for (const auto &entry : in.at("nodes"))
        {
          BlueprintNode node;
          node.id = entry.value("id", kInvalidBlueprintNode);
          node.type = entry.value("type", std::string());
          node.x = entry.value("x", 0.0f);
          node.y = entry.value("y", 0.0f);
          node.comment = entry.value("comment", std::string());
          node.config = entry.contains("config") && entry.at("config").is_object()
                            ? entry.at("config")
                            : nlohmann::json::object();

          if (entry.contains("defaults") && entry.at("defaults").is_object())
          {
            for (const auto &[pin, slot] : entry.at("defaults").items())
            {
              ValueType type = ValueType::Float;
              if (slot.is_object())
              {
                value_type_from_name(slot.value("type", std::string("float")), type);
                node.pinDefaults[pin] = BlueprintValue::from_json(
                    slot.contains("value") ? slot.at("value") : nlohmann::json(nullptr), type);
              }
            }
          }

          out.nodes.push_back(std::move(node));
        }
      }

      if (in.contains("links"))
      {
        if (!in.at("links").is_array())
        {
          if (errorMessage != nullptr)
          {
            *errorMessage = "graph.links must be an array";
          }
          return false;
        }

        for (const auto &entry : in.at("links"))
        {
          BlueprintLink link;
          link.kind = entry.value("kind", std::string("exec")) == "data"
                          ? BlueprintLinkKind::Data
                          : BlueprintLinkKind::Exec;

          if (entry.contains("from") && entry.at("from").is_object())
          {
            link.from.node = entry.at("from").value("node", kInvalidBlueprintNode);
            link.from.pin = entry.at("from").value("pin", std::string());
          }
          if (entry.contains("to") && entry.at("to").is_object())
          {
            link.to.node = entry.at("to").value("node", kInvalidBlueprintNode);
            link.to.pin = entry.at("to").value("pin", std::string());
          }

          out.links.push_back(std::move(link));
        }
      }

      return true;
    }
  }

  BlueprintNode *BlueprintGraph::find_node(BlueprintNodeId id)
  {
    for (auto &node : nodes)
    {
      if (node.id == id)
      {
        return &node;
      }
    }
    return nullptr;
  }

  const BlueprintNode *BlueprintGraph::find_node(BlueprintNodeId id) const
  {
    for (const auto &node : nodes)
    {
      if (node.id == id)
      {
        return &node;
      }
    }
    return nullptr;
  }

  int BlueprintGraph::node_index(BlueprintNodeId id) const
  {
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      if (nodes[i].id == id)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  void BlueprintGraph::remove_node(BlueprintNodeId id)
  {
    links.erase(
        std::remove_if(
            links.begin(),
            links.end(),
            [id](const BlueprintLink &link)
            { return link.from.node == id || link.to.node == id; }),
        links.end());

    nodes.erase(
        std::remove_if(
            nodes.begin(),
            nodes.end(),
            [id](const BlueprintNode &node)
            { return node.id == id; }),
        nodes.end());
  }

  bool BlueprintGraph::remove_link(const BlueprintLink &link)
  {
    const auto it = std::find(links.begin(), links.end(), link);
    if (it == links.end())
    {
      return false;
    }

    links.erase(it);
    return true;
  }

  void BlueprintGraph::remove_links_into(const BlueprintPinRef &pin, BlueprintLinkKind kind)
  {
    links.erase(
        std::remove_if(
            links.begin(),
            links.end(),
            [&](const BlueprintLink &link)
            { return link.kind == kind && link.to == pin; }),
        links.end());
  }

  void BlueprintGraph::remove_links_out_of(const BlueprintPinRef &pin, BlueprintLinkKind kind)
  {
    links.erase(
        std::remove_if(
            links.begin(),
            links.end(),
            [&](const BlueprintLink &link)
            { return link.kind == kind && link.from == pin; }),
        links.end());
  }

  const BlueprintLink *BlueprintGraph::incoming_data_link(const BlueprintPinRef &pin) const
  {
    for (const auto &link : links)
    {
      if (link.kind == BlueprintLinkKind::Data && link.to == pin)
      {
        return &link;
      }
    }
    return nullptr;
  }

  const BlueprintLink *BlueprintGraph::outgoing_exec_link(const BlueprintPinRef &pin) const
  {
    for (const auto &link : links)
    {
      if (link.kind == BlueprintLinkKind::Exec && link.from == pin)
      {
        return &link;
      }
    }
    return nullptr;
  }

  bool BlueprintGraph::pin_has_link(
      const BlueprintPinRef &pin,
      BlueprintLinkKind kind,
      bool asInput) const
  {
    for (const auto &link : links)
    {
      if (link.kind != kind)
      {
        continue;
      }
      if (asInput ? (link.to == pin) : (link.from == pin))
      {
        return true;
      }
    }
    return false;
  }

  BlueprintNodeId Blueprint::allocate_node_id()
  {
    return nextNodeId++;
  }

  const BlueprintVariable *Blueprint::find_variable(const std::string &name) const
  {
    for (const auto &variable : variables)
    {
      if (variable.name == name)
      {
        return &variable;
      }
    }
    return nullptr;
  }

  BlueprintVariable *Blueprint::find_variable(const std::string &name)
  {
    for (auto &variable : variables)
    {
      if (variable.name == name)
      {
        return &variable;
      }
    }
    return nullptr;
  }

  int Blueprint::variable_index(const std::string &name) const
  {
    for (std::size_t i = 0; i < variables.size(); ++i)
    {
      if (variables[i].name == name)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  const BlueprintFunction *Blueprint::find_function(const std::string &name) const
  {
    for (const auto &function : functions)
    {
      if (function.name == name)
      {
        return &function;
      }
    }
    return nullptr;
  }

  BlueprintFunction *Blueprint::find_function(const std::string &name)
  {
    for (auto &function : functions)
    {
      if (function.name == name)
      {
        return &function;
      }
    }
    return nullptr;
  }

  std::vector<BlueprintGraph *> Blueprint::all_graphs()
  {
    std::vector<BlueprintGraph *> graphs;
    graphs.reserve(functions.size() + 1);
    graphs.push_back(&eventGraph);
    for (auto &function : functions)
    {
      graphs.push_back(&function.graph);
    }
    return graphs;
  }

  std::vector<const BlueprintGraph *> Blueprint::all_graphs() const
  {
    std::vector<const BlueprintGraph *> graphs;
    graphs.reserve(functions.size() + 1);
    graphs.push_back(&eventGraph);
    for (const auto &function : functions)
    {
      graphs.push_back(&function.graph);
    }
    return graphs;
  }

  nlohmann::json Blueprint::to_json() const
  {
    nlohmann::json out;
    out["version"] = kFormatVersion;
    out["name"] = name;
    if (!description.empty())
    {
      out["description"] = description;
    }
    out["nextNodeId"] = nextNodeId;

    nlohmann::json variablesJson = nlohmann::json::array();
    for (const auto &variable : variables)
    {
      variablesJson.push_back(variable_to_json(variable));
    }
    out["variables"] = std::move(variablesJson);

    nlohmann::json functionsJson = nlohmann::json::array();
    for (const auto &function : functions)
    {
      nlohmann::json entry;
      entry["name"] = function.name;
      entry["allowRecursion"] = function.allowRecursion;

      nlohmann::json inputs = nlohmann::json::array();
      for (const auto &parameter : function.inputs)
      {
        inputs.push_back(variable_to_json(parameter));
      }
      entry["inputs"] = std::move(inputs);

      nlohmann::json outputs = nlohmann::json::array();
      for (const auto &parameter : function.outputs)
      {
        outputs.push_back(variable_to_json(parameter));
      }
      entry["outputs"] = std::move(outputs);

      entry["graph"] = graph_to_json(function.graph);
      functionsJson.push_back(std::move(entry));
    }
    out["functions"] = std::move(functionsJson);

    out["graph"] = graph_to_json(eventGraph);
    return out;
  }

  bool Blueprint::from_json(
      const nlohmann::json &document,
      Blueprint &out,
      std::string *errorMessage)
  {
    out = Blueprint();

    if (!document.is_object())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "blueprint document must be a JSON object";
      }
      return false;
    }

    const int version = document.value("version", 0);
    if (version > kFormatVersion)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage =
            "blueprint format version " + std::to_string(version) +
            " is newer than this engine supports (" + std::to_string(kFormatVersion) + ")";
      }
      return false;
    }

    out.name = document.value("name", std::string());
    out.description = document.value("description", std::string());
    out.nextNodeId = document.value("nextNodeId", static_cast<BlueprintNodeId>(1));

    if (document.contains("variables") && document.at("variables").is_array())
    {
      for (const auto &entry : document.at("variables"))
      {
        out.variables.push_back(variable_from_json(entry));
      }
    }

    if (document.contains("functions") && document.at("functions").is_array())
    {
      for (const auto &entry : document.at("functions"))
      {
        BlueprintFunction function;
        function.name = entry.value("name", std::string());
        function.allowRecursion = entry.value("allowRecursion", false);

        if (entry.contains("inputs") && entry.at("inputs").is_array())
        {
          for (const auto &parameter : entry.at("inputs"))
          {
            function.inputs.push_back(variable_from_json(parameter));
          }
        }
        if (entry.contains("outputs") && entry.at("outputs").is_array())
        {
          for (const auto &parameter : entry.at("outputs"))
          {
            function.outputs.push_back(variable_from_json(parameter));
          }
        }

        if (entry.contains("graph"))
        {
          std::string graphError;
          if (!graph_from_json(entry.at("graph"), function.graph, &graphError))
          {
            if (errorMessage != nullptr)
            {
              *errorMessage = "function '" + function.name + "': " + graphError;
            }
            return false;
          }
        }

        out.functions.push_back(std::move(function));
      }
    }

    if (document.contains("graph"))
    {
      std::string graphError;
      if (!graph_from_json(document.at("graph"), out.eventGraph, &graphError))
      {
        if (errorMessage != nullptr)
        {
          *errorMessage = "event graph: " + graphError;
        }
        return false;
      }
    }

    out.normalize();
    return true;
  }

  int Blueprint::normalize()
  {
    int repairs = 0;

    // Pass 1: every node needs a unique, non-zero id.
    std::unordered_set<BlueprintNodeId> seen;
    for (auto *graph : all_graphs())
    {
      for (auto &node : graph->nodes)
      {
        if (node.id == kInvalidBlueprintNode || seen.count(node.id) != 0)
        {
          node.id = nextNodeId++;
          ++repairs;
        }
        seen.insert(node.id);
        nextNodeId = std::max(nextNodeId, node.id + 1);
      }
    }

    // Pass 2: drop wires that point at nodes which are not in the same graph,
    // and collapse exact duplicates.
    for (auto *graph : all_graphs())
    {
      const auto before = graph->links.size();

      graph->links.erase(
          std::remove_if(
              graph->links.begin(),
              graph->links.end(),
              [graph](const BlueprintLink &link)
              {
                if (link.from.pin.empty() || link.to.pin.empty())
                {
                  return true;
                }
                if (link.from.node == link.to.node)
                {
                  return true;
                }
                return graph->find_node(link.from.node) == nullptr ||
                       graph->find_node(link.to.node) == nullptr;
              }),
          graph->links.end());

      std::vector<BlueprintLink> unique;
      unique.reserve(graph->links.size());
      for (const auto &link : graph->links)
      {
        if (std::find(unique.begin(), unique.end(), link) == unique.end())
        {
          unique.push_back(link);
        }
      }
      graph->links = std::move(unique);

      repairs += static_cast<int>(before - graph->links.size());
    }

    return repairs;
  }
}
