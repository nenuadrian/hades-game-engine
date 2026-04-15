#ifndef HADES_ENGINE_RUNTIME_POLICY_REGISTRY_HPP
#define HADES_ENGINE_RUNTIME_POLICY_REGISTRY_HPP

#include <hne/core/types.hpp>
#include <hne/inference/runtime.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace hades
{
  /// Process-wide cache of `hne::InferenceRuntime` instances keyed by absolute
  /// policy path. Loading a TorchScript policy is expensive (deserialization +
  /// graph construction) and the same policy is often shared across many
  /// entities — the registry amortizes that cost.
  ///
  /// Failures are also memoized: if a policy fails to load or its specs do
  /// not match the expected obs/action spaces, subsequent calls return the
  /// same error without re-attempting the load. Use `clear()` (test fixture
  /// support, or when the user explicitly retrains) to reset.
  class PolicyRegistry
  {
  public:
    struct Result
    {
      std::shared_ptr<hne::InferenceRuntime> runtime;
      std::string error; // empty on success
    };

    /// Look up (or load) the policy at `absolutePath`, validating its declared
    /// observation/action spaces against `expectedObs`/`expectedAct`. Returns
    /// `{runtime, ""}` on success, `{nullptr, descriptive_error}` on failure.
    ///
    /// When a `<absolutePath>.meta.json` sidecar exists, its declared spaces
    /// are also cross-checked against the runtime's reported spaces (catches
    /// the case where a policy was trained with one set of specs but the
    /// runtime metadata was stripped during export).
    Result get_validated(
        const std::filesystem::path &absolutePath,
        const hne::SpaceSpec &expectedObs,
        const hne::SpaceSpec &expectedAct);

    /// Drop all cached entries. Test fixtures and "Reload Policies" UI use this.
    void clear();

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<hne::InferenceRuntime>> cache_;
    std::unordered_map<std::string, std::string> failures_;
  };

  /// Structural equality on observation/action space descriptors.
  /// Two specs are equal iff they hold the same alternative and all their
  /// fields (shape/n/nvec/low/high) are element-wise equal.
  bool spec_equals(const hne::SpaceSpec &a, const hne::SpaceSpec &b);

  /// Short, human-readable description of a SpaceSpec — used in error messages
  /// when a script's declared spaces don't line up with a loaded policy.
  /// e.g. "Box(shape=[4], low=[-1,..], high=[1,..])" or "Discrete(n=2)".
  std::string describe_space(const hne::SpaceSpec &s);
}

#endif
