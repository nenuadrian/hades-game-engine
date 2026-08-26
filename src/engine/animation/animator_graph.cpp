#include "animator_graph.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

namespace hades
{
  namespace
  {
    // Persisted enum spellings. Order matches the enumerator order, so the
    // index into the table *is* the enumerator value — a new enumerator only
    // ever appends, which is what keeps old files readable.
    constexpr std::array<const char *, 4> kParamTypeNames = {
        "float", "int", "bool", "trigger"};

    constexpr std::array<const char *, 8> kConditionOpNames = {
        "greater", "less", "greaterOrEqual", "lessOrEqual",
        "equals", "notEquals", "isTrue", "isFalse"};

    constexpr std::array<const char *, 3> kStateKindNames = {
        "clip", "blendTree1D", "blendTree2D"};

    template <typename Enum, std::size_t N>
    const char *name_from_table(const std::array<const char *, N> &names, Enum value)
    {
      const auto index = static_cast<std::size_t>(value);
      return index < names.size() ? names[index] : "unknown";
    }

    template <typename Enum, std::size_t N>
    bool value_from_table(const std::array<const char *, N> &names, const std::string &name, Enum &out)
    {
      for (std::size_t i = 0; i < names.size(); ++i)
      {
        if (name == names[i])
        {
          out = static_cast<Enum>(i);
          return true;
        }
      }

      return false;
    }

    // ---- Serialisation helpers ---------------------------------------------
    // Every read is tolerant: a missing or mistyped field falls back to the
    // struct's own default rather than failing the load, so a file written by
    // an older editor still opens.
    //
    // `json::value()` cannot be used for that: it only falls back when the key
    // is ABSENT and throws type_error.302 when the key is present holding
    // another type -- which a hand-edited file produces trivially, and which
    // to_json() itself produces whenever a float has gone non-finite (dump()
    // writes NaN and infinity as `null`). Loading runs on the frame path via
    // AnimationClipCache::graph(), so the type is checked before every read.
    // Same rule, and the same reason, as animation_clip.cpp and rig_asset.cpp.

    float read_float(const nlohmann::json &in, const char *key, float fallback)
    {
      const auto it = in.find(key);
      return (it != in.end() && it->is_number()) ? it->get<float>() : fallback;
    }

    int read_int(const nlohmann::json &in, const char *key, int fallback)
    {
      const auto it = in.find(key);
      return (it != in.end() && it->is_number()) ? it->get<int>() : fallback;
    }

    bool read_bool(const nlohmann::json &in, const char *key, bool fallback)
    {
      const auto it = in.find(key);
      return (it != in.end() && it->is_boolean()) ? it->get<bool>() : fallback;
    }

    std::string read_string(const nlohmann::json &in, const char *key, const char *fallback = "")
    {
      const auto it = in.find(key);
      return (it != in.end() && it->is_string()) ? it->get<std::string>() : std::string(fallback);
    }

    nlohmann::json condition_to_json(const AnimCondition &condition)
    {
      nlohmann::json out;
      out["parameter"] = condition.parameter;
      out["op"] = anim_condition_op_name(condition.op);
      out["threshold"] = condition.threshold;
      return out;
    }

    AnimCondition condition_from_json(const nlohmann::json &in)
    {
      AnimCondition condition;
      if (!in.is_object())
      {
        return condition;
      }

      condition.parameter = read_string(in, "parameter");
      anim_condition_op_from_name(read_string(in, "op", "greater"), condition.op);
      condition.threshold = read_float(in, "threshold", 0.0f);
      return condition;
    }

    nlohmann::json transition_to_json(const AnimTransition &transition)
    {
      nlohmann::json conditions = nlohmann::json::array();
      for (const auto &condition : transition.conditions)
      {
        conditions.push_back(condition_to_json(condition));
      }

      nlohmann::json out;
      out["fromState"] = transition.fromState;
      out["toState"] = transition.toState;
      out["duration"] = transition.duration;
      out["hasExitTime"] = transition.hasExitTime;
      out["exitTime"] = transition.exitTime;
      out["canInterrupt"] = transition.canInterrupt;
      out["priority"] = transition.priority;
      out["conditions"] = std::move(conditions);
      return out;
    }

    AnimTransition transition_from_json(const nlohmann::json &in)
    {
      AnimTransition transition;
      if (!in.is_object())
      {
        return transition;
      }

      transition.fromState = read_int(in, "fromState", AnimTransition::kAnyState);
      transition.toState = read_int(in, "toState", 0);
      transition.duration = read_float(in, "duration", 0.2f);
      transition.hasExitTime = read_bool(in, "hasExitTime", false);
      transition.exitTime = read_float(in, "exitTime", 1.0f);
      transition.canInterrupt = read_bool(in, "canInterrupt", false);
      transition.priority = read_int(in, "priority", 0);

      if (in.contains("conditions") && in.at("conditions").is_array())
      {
        for (const auto &entry : in.at("conditions"))
        {
          transition.conditions.push_back(condition_from_json(entry));
        }
      }

      return transition;
    }

    nlohmann::json blend_entry_to_json(const AnimBlendEntry &entry)
    {
      nlohmann::json out;
      out["clip"] = entry.clip;
      out["thresholdX"] = entry.thresholdX;
      out["thresholdY"] = entry.thresholdY;
      out["speed"] = entry.speed;
      return out;
    }

    AnimBlendEntry blend_entry_from_json(const nlohmann::json &in)
    {
      AnimBlendEntry entry;
      if (!in.is_object())
      {
        return entry;
      }

      entry.clip = read_string(in, "clip");
      entry.thresholdX = read_float(in, "thresholdX", 0.0f);
      entry.thresholdY = read_float(in, "thresholdY", 0.0f);
      entry.speed = read_float(in, "speed", 1.0f);
      return entry;
    }

    nlohmann::json state_to_json(const AnimState &state)
    {
      nlohmann::json entries = nlohmann::json::array();
      for (const auto &entry : state.entries)
      {
        entries.push_back(blend_entry_to_json(entry));
      }

      nlohmann::json out;
      out["name"] = state.name;
      out["kind"] = anim_state_kind_name(state.kind);
      out["clip"] = state.clip;
      out["speed"] = state.speed;
      out["looping"] = state.looping;
      out["blendParameterX"] = state.blendParameterX;
      out["blendParameterY"] = state.blendParameterY;
      out["entries"] = std::move(entries);
      out["x"] = state.x;
      out["y"] = state.y;
      return out;
    }

    AnimState state_from_json(const nlohmann::json &in)
    {
      AnimState state;
      if (!in.is_object())
      {
        return state;
      }

      state.name = read_string(in, "name");
      anim_state_kind_from_name(read_string(in, "kind", "clip"), state.kind);
      state.clip = read_string(in, "clip");
      state.speed = read_float(in, "speed", 1.0f);
      state.looping = read_bool(in, "looping", true);
      state.blendParameterX = read_string(in, "blendParameterX");
      state.blendParameterY = read_string(in, "blendParameterY");
      state.x = read_float(in, "x", 0.0f);
      state.y = read_float(in, "y", 0.0f);

      if (in.contains("entries") && in.at("entries").is_array())
      {
        for (const auto &entry : in.at("entries"))
        {
          state.entries.push_back(blend_entry_from_json(entry));
        }
      }

      return state;
    }

    nlohmann::json layer_to_json(const AnimLayer &layer)
    {
      nlohmann::json states = nlohmann::json::array();
      for (const auto &state : layer.states)
      {
        states.push_back(state_to_json(state));
      }

      nlohmann::json transitions = nlohmann::json::array();
      for (const auto &transition : layer.transitions)
      {
        transitions.push_back(transition_to_json(transition));
      }

      nlohmann::json mask = nlohmann::json::array();
      for (const auto &bone : layer.maskBones)
      {
        mask.push_back(bone);
      }

      nlohmann::json out;
      out["name"] = layer.name;
      out["weight"] = layer.weight;
      out["additive"] = layer.additive;
      out["maskBones"] = std::move(mask);
      out["maskIncludesDescendants"] = layer.maskIncludesDescendants;
      out["states"] = std::move(states);
      out["transitions"] = std::move(transitions);
      out["defaultState"] = layer.defaultState;
      return out;
    }

    AnimLayer layer_from_json(const nlohmann::json &in)
    {
      AnimLayer layer;
      if (!in.is_object())
      {
        return layer;
      }

      layer.name = read_string(in, "name", "Base");
      layer.weight = read_float(in, "weight", 1.0f);
      layer.additive = read_bool(in, "additive", false);
      layer.maskIncludesDescendants = read_bool(in, "maskIncludesDescendants", true);
      layer.defaultState = read_int(in, "defaultState", 0);

      if (in.contains("maskBones") && in.at("maskBones").is_array())
      {
        for (const auto &bone : in.at("maskBones"))
        {
          if (bone.is_string())
          {
            layer.maskBones.push_back(bone.get<std::string>());
          }
        }
      }

      if (in.contains("states") && in.at("states").is_array())
      {
        for (const auto &entry : in.at("states"))
        {
          layer.states.push_back(state_from_json(entry));
        }
      }

      if (in.contains("transitions") && in.at("transitions").is_array())
      {
        for (const auto &entry : in.at("transitions"))
        {
          layer.transitions.push_back(transition_from_json(entry));
        }
      }

      return layer;
    }

    nlohmann::json parameter_to_json(const AnimParameter &parameter)
    {
      nlohmann::json out;
      out["name"] = parameter.name;
      out["type"] = anim_param_type_name(parameter.type);
      out["floatValue"] = parameter.floatValue;
      out["intValue"] = parameter.intValue;
      out["boolValue"] = parameter.boolValue;
      return out;
    }

    AnimParameter parameter_from_json(const nlohmann::json &in)
    {
      AnimParameter parameter;
      if (!in.is_object())
      {
        return parameter;
      }

      parameter.name = read_string(in, "name");
      anim_param_type_from_name(read_string(in, "type", "float"), parameter.type);
      parameter.floatValue = read_float(in, "floatValue", 0.0f);
      parameter.intValue = read_int(in, "intValue", 0);
      parameter.boolValue = read_bool(in, "boolValue", false);
      return parameter;
    }
  }

  const char *anim_param_type_name(AnimParamType type)
  {
    return name_from_table(kParamTypeNames, type);
  }

  bool anim_param_type_from_name(const std::string &name, AnimParamType &out)
  {
    return value_from_table(kParamTypeNames, name, out);
  }

  const char *anim_condition_op_name(AnimConditionOp op)
  {
    return name_from_table(kConditionOpNames, op);
  }

  bool anim_condition_op_from_name(const std::string &name, AnimConditionOp &out)
  {
    return value_from_table(kConditionOpNames, name, out);
  }

  const char *anim_state_kind_name(AnimStateKind kind)
  {
    return name_from_table(kStateKindNames, kind);
  }

  bool anim_state_kind_from_name(const std::string &name, AnimStateKind &out)
  {
    return value_from_table(kStateKindNames, name, out);
  }

  int AnimLayer::find_state(const std::string &name) const
  {
    for (std::size_t i = 0; i < states.size(); ++i)
    {
      if (states[i].name == name)
      {
        return static_cast<int>(i);
      }
    }

    return -1;
  }

  int AnimatorGraph::parameter_index(const std::string &name) const
  {
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
      if (parameters[i].name == name)
      {
        return static_cast<int>(i);
      }
    }

    return -1;
  }

  const AnimParameter *AnimatorGraph::find_parameter(const std::string &name) const
  {
    const int index = parameter_index(name);
    return index >= 0 ? &parameters[static_cast<std::size_t>(index)] : nullptr;
  }

  AnimParameter *AnimatorGraph::find_parameter(const std::string &name)
  {
    const int index = parameter_index(name);
    return index >= 0 ? &parameters[static_cast<std::size_t>(index)] : nullptr;
  }

  std::vector<std::string> AnimatorGraph::referenced_clips() const
  {
    std::vector<std::string> clips;
    std::unordered_set<std::string> seen;

    // Insertion order is kept so the cache warms clips in authoring order,
    // which makes the log readable when one of them fails.
    const auto add = [&clips, &seen](const std::string &clip)
    {
      if (clip.empty() || !seen.insert(clip).second)
      {
        return;
      }
      clips.push_back(clip);
    };

    for (const auto &layer : layers)
    {
      for (const auto &state : layer.states)
      {
        if (state.kind == AnimStateKind::Clip)
        {
          add(state.clip);
          continue;
        }

        for (const auto &entry : state.entries)
        {
          add(entry.clip);
        }
      }
    }

    return clips;
  }

  bool AnimatorGraph::validate(std::vector<std::string> &outProblems) const
  {
    outProblems.clear();

    std::unordered_set<std::string> parameterNames;
    for (const auto &parameter : parameters)
    {
      if (!parameterNames.insert(parameter.name).second)
      {
        outProblems.push_back("duplicate parameter name '" + parameter.name + "'");
      }
    }

    for (const auto &layer : layers)
    {
      const std::string prefix = "layer '" + layer.name + "': ";
      const int stateCount = static_cast<int>(layer.states.size());

      std::unordered_set<std::string> stateNames;
      for (const auto &state : layer.states)
      {
        if (!stateNames.insert(state.name).second)
        {
          outProblems.push_back(prefix + "duplicate state name '" + state.name + "'");
        }

        if (state.kind == AnimStateKind::Clip)
        {
          continue;
        }

        if (state.entries.empty())
        {
          outProblems.push_back(prefix + "state '" + state.name + "' is a blend tree with no entries");
        }

        if (state.blendParameterX.empty())
        {
          outProblems.push_back(prefix + "state '" + state.name + "' has no blend parameter");
        }
        else if (find_parameter(state.blendParameterX) == nullptr)
        {
          outProblems.push_back(prefix + "state '" + state.name + "' blends on unknown parameter '" +
                                state.blendParameterX + "'");
        }

        if (state.kind == AnimStateKind::BlendTree2D)
        {
          if (state.blendParameterY.empty())
          {
            outProblems.push_back(prefix + "state '" + state.name + "' has no second blend parameter");
          }
          else if (find_parameter(state.blendParameterY) == nullptr)
          {
            outProblems.push_back(prefix + "state '" + state.name + "' blends on unknown parameter '" +
                                  state.blendParameterY + "'");
          }
        }
      }

      if (layer.defaultState < 0 || layer.defaultState >= stateCount)
      {
        outProblems.push_back(prefix + "defaultState " + std::to_string(layer.defaultState) +
                              " is out of range (" + std::to_string(stateCount) + " states)");
      }

      for (std::size_t i = 0; i < layer.transitions.size(); ++i)
      {
        const AnimTransition &transition = layer.transitions[i];
        const std::string label = "transition " + std::to_string(i);

        if (transition.toState < 0 || transition.toState >= stateCount)
        {
          outProblems.push_back(prefix + label + " targets state " + std::to_string(transition.toState) +
                                ", which does not exist");
        }

        if (transition.fromState != AnimTransition::kAnyState &&
            (transition.fromState < 0 || transition.fromState >= stateCount))
        {
          outProblems.push_back(prefix + label + " comes from state " + std::to_string(transition.fromState) +
                                ", which does not exist");
        }

        for (std::size_t c = 0; c < transition.conditions.size(); ++c)
        {
          const AnimCondition &condition = transition.conditions[c];
          const std::string conditionLabel = label + " condition " + std::to_string(c);

          if (condition.parameter.empty())
          {
            outProblems.push_back(prefix + conditionLabel + " names no parameter");
          }
          else if (find_parameter(condition.parameter) == nullptr)
          {
            outProblems.push_back(prefix + conditionLabel + " uses unknown parameter '" +
                                  condition.parameter + "'");
          }
        }
      }
    }

    return outProblems.empty();
  }

  void AnimatorGraph::ensure_default_layer()
  {
    if (layers.empty())
    {
      AnimLayer layer;
      layer.name = "Base";
      layers.push_back(std::move(layer));
    }

    for (auto &layer : layers)
    {
      if (!layer.states.empty())
      {
        continue;
      }

      AnimState state;
      state.name = "New State";
      layer.states.push_back(std::move(state));
      layer.defaultState = 0;
    }
  }

  nlohmann::json AnimatorGraph::to_json() const
  {
    nlohmann::json parameterArray = nlohmann::json::array();
    for (const auto &parameter : parameters)
    {
      parameterArray.push_back(parameter_to_json(parameter));
    }

    nlohmann::json layerArray = nlohmann::json::array();
    for (const auto &layer : layers)
    {
      layerArray.push_back(layer_to_json(layer));
    }

    nlohmann::json out;
    out["version"] = kFormatVersion;
    out["name"] = name;
    out["description"] = description;
    out["sourceModel"] = sourceModel;
    out["parameters"] = std::move(parameterArray);
    out["layers"] = std::move(layerArray);
    return out;
  }

  bool AnimatorGraph::from_json(const nlohmann::json &document, AnimatorGraph &out, std::string *errorMessage)
  {
    out = AnimatorGraph{};

    if (!document.is_object())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "animator graph must be a JSON object";
      }
      return false;
    }

    out.name = read_string(document, "name");
    out.description = read_string(document, "description");
    out.sourceModel = read_string(document, "sourceModel");

    if (document.contains("parameters") && document.at("parameters").is_array())
    {
      for (const auto &entry : document.at("parameters"))
      {
        out.parameters.push_back(parameter_from_json(entry));
      }
    }

    if (document.contains("layers") && document.at("layers").is_array())
    {
      for (const auto &entry : document.at("layers"))
      {
        out.layers.push_back(layer_from_json(entry));
      }
    }

    // Loading has to be robust rather than strict: a hand-edited or
    // stale file still opens, minus the edges that point nowhere. Authoring
    // feedback about the remaining problems is validate()'s job.
    for (auto &layer : out.layers)
    {
      const int stateCount = static_cast<int>(layer.states.size());

      layer.transitions.erase(
          std::remove_if(layer.transitions.begin(), layer.transitions.end(),
                         [stateCount](const AnimTransition &transition)
                         {
                           return transition.toState < 0 || transition.toState >= stateCount;
                         }),
          layer.transitions.end());

      layer.defaultState = stateCount > 0 ? std::clamp(layer.defaultState, 0, stateCount - 1) : 0;
    }

    return true;
  }
}
