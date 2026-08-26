#ifndef HADES_ENGINE_ANIMATION_ANIMATOR_GRAPH_HPP
#define HADES_ENGINE_ANIMATION_ANIMATOR_GRAPH_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hades
{
  /// Parameter types a graph can branch on. `Trigger` is a bool that the
  /// animator consumes (resets to false) as soon as a transition uses it —
  /// the standard "fire once" input for jumps, attacks and hits.
  enum class AnimParamType : std::uint8_t
  {
    Float = 0,
    Int,
    Bool,
    Trigger,
  };

  const char *anim_param_type_name(AnimParamType type);
  bool anim_param_type_from_name(const std::string &name, AnimParamType &out);

  struct AnimParameter
  {
    std::string name;
    AnimParamType type = AnimParamType::Float;
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
  };

  enum class AnimConditionOp : std::uint8_t
  {
    Greater = 0,
    Less,
    GreaterOrEqual,
    LessOrEqual,
    Equals,
    NotEquals,
    IsTrue,
    IsFalse,
  };

  const char *anim_condition_op_name(AnimConditionOp op);
  bool anim_condition_op_from_name(const std::string &name, AnimConditionOp &out);

  /// One term of a transition's guard. All conditions on a transition must
  /// hold for it to be taken.
  struct AnimCondition
  {
    std::string parameter;
    AnimConditionOp op = AnimConditionOp::Greater;
    float threshold = 0.0f;
  };

  /// A directed edge between two states. `fromState == kAnyState` makes it an
  /// "any state" transition, evaluated from whatever state is current.
  struct AnimTransition
  {
    static constexpr int kAnyState = -1;

    int fromState = kAnyState;
    int toState = 0;
    /// Crossfade length in seconds. 0 snaps.
    float duration = 0.2f;
    /// When set, the transition only becomes eligible once the source state's
    /// normalised time has passed `exitTime`.
    bool hasExitTime = false;
    float exitTime = 1.0f;
    /// When false, the transition is ignored while another one is running.
    bool canInterrupt = false;
    /// Ties are broken by priority (higher first), then declaration order.
    int priority = 0;
    std::vector<AnimCondition> conditions;
  };

  enum class AnimStateKind : std::uint8_t
  {
    /// Plays one clip.
    Clip = 0,
    /// Blends clips along one parameter (locomotion: idle -> walk -> run).
    BlendTree1D,
    /// Blends clips across two parameters (strafing).
    BlendTree2D,
  };

  const char *anim_state_kind_name(AnimStateKind kind);
  bool anim_state_kind_from_name(const std::string &name, AnimStateKind &out);

  /// One clip inside a blend tree, positioned by its threshold(s).
  struct AnimBlendEntry
  {
    std::string clip;
    float thresholdX = 0.0f;
    float thresholdY = 0.0f;
    float speed = 1.0f;
  };

  struct AnimState
  {
    std::string name;
    AnimStateKind kind = AnimStateKind::Clip;
    /// Clip path for AnimStateKind::Clip.
    std::string clip;
    float speed = 1.0f;
    bool looping = true;
    /// Parameters driving a blend tree.
    std::string blendParameterX;
    std::string blendParameterY;
    std::vector<AnimBlendEntry> entries;
    /// Canvas position, editor-only.
    float x = 0.0f;
    float y = 0.0f;
  };

  /// A stack of independent state machines. Layer 0 is the base pose; higher
  /// layers blend or add over it, optionally restricted to a bone mask (upper
  /// body aiming over a full-body run, for example).
  struct AnimLayer
  {
    std::string name = "Base";
    float weight = 1.0f;
    bool additive = false;
    /// Joint names the layer affects. Empty means the whole skeleton.
    std::vector<std::string> maskBones;
    /// Extend the mask down the hierarchy from each named joint.
    bool maskIncludesDescendants = true;

    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    int defaultState = 0;

    int find_state(const std::string &name) const;
  };

  /// The animator asset: parameters plus one or more layers. Stored as JSON
  /// under `<assets>/.hades/animators/`.
  class AnimatorGraph
  {
  public:
    static constexpr int kFormatVersion = 1;

    std::string name;
    std::string description;
    /// Model this graph was authored against — a hint for the editor.
    std::string sourceModel;
    std::vector<AnimParameter> parameters;
    std::vector<AnimLayer> layers;

    const AnimParameter *find_parameter(const std::string &name) const;
    AnimParameter *find_parameter(const std::string &name);
    int parameter_index(const std::string &name) const;

    /// Every clip path referenced by any state, deduplicated. The cache
    /// warms these when the graph is loaded.
    std::vector<std::string> referenced_clips() const;

    /// Report structural problems (dangling state indices, unknown
    /// parameters, blend trees with no entries). Returns true when clean.
    bool validate(std::vector<std::string> &outProblems) const;

    /// Ensure there is at least one layer with one state, so a freshly
    /// created graph is playable.
    void ensure_default_layer();

    nlohmann::json to_json() const;
    static bool from_json(const nlohmann::json &document, AnimatorGraph &out,
                          std::string *errorMessage = nullptr);
  };
}

#endif
