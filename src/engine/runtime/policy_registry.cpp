#include "policy_registry.hpp"

#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace hades
{
  namespace
  {
    std::string normalize_key(const std::filesystem::path &path)
    {
      std::error_code ec;
      auto canonical = std::filesystem::weakly_canonical(path, ec);
      if (ec)
      {
        return path.lexically_normal().string();
      }
      return canonical.string();
    }

    std::string format_floats(const std::vector<float> &v)
    {
      if (v.empty())
      {
        return "[]";
      }
      std::ostringstream oss;
      oss << '[';
      const std::size_t shown = std::min<std::size_t>(v.size(), 4);
      for (std::size_t i = 0; i < shown; ++i)
      {
        if (i != 0)
        {
          oss << ',';
        }
        oss << v[i];
      }
      if (v.size() > shown)
      {
        oss << ",..";
      }
      oss << ']';
      return oss.str();
    }

    std::string format_ints(const std::vector<int32_t> &v)
    {
      if (v.empty())
      {
        return "[]";
      }
      std::ostringstream oss;
      oss << '[';
      for (std::size_t i = 0; i < v.size(); ++i)
      {
        if (i != 0)
        {
          oss << ',';
        }
        oss << v[i];
      }
      oss << ']';
      return oss.str();
    }
  }

  bool spec_equals(const hne::SpaceSpec &a, const hne::SpaceSpec &b)
  {
    if (a.index() != b.index())
    {
      return false;
    }

    if (const auto *ad = std::get_if<hne::DiscreteSpace>(&a))
    {
      const auto &bd = std::get<hne::DiscreteSpace>(b);
      return ad->n == bd.n;
    }
    if (const auto *am = std::get_if<hne::MultiDiscreteSpace>(&a))
    {
      const auto &bm = std::get<hne::MultiDiscreteSpace>(b);
      return am->nvec == bm.nvec;
    }
    if (const auto *ab = std::get_if<hne::BoxSpace>(&a))
    {
      const auto &bb = std::get<hne::BoxSpace>(b);
      return ab->shape == bb.shape && ab->low == bb.low && ab->high == bb.high;
    }
    return false;
  }

  std::string describe_space(const hne::SpaceSpec &s)
  {
    if (const auto *d = std::get_if<hne::DiscreteSpace>(&s))
    {
      return "Discrete(n=" + std::to_string(d->n) + ")";
    }
    if (const auto *m = std::get_if<hne::MultiDiscreteSpace>(&s))
    {
      return "MultiDiscrete(nvec=" + format_ints(m->nvec) + ")";
    }
    if (const auto *b = std::get_if<hne::BoxSpace>(&s))
    {
      std::ostringstream oss;
      oss << "Box(shape=" << format_ints(b->shape)
          << ", low=" << format_floats(b->low)
          << ", high=" << format_floats(b->high) << ')';
      return oss.str();
    }
    return "<unknown space>";
  }

  PolicyRegistry::Result PolicyRegistry::get_validated(
      const std::filesystem::path &absolutePath,
      const hne::SpaceSpec &expectedObs,
      const hne::SpaceSpec &expectedAct)
  {
#if !defined(HADES_HAS_HNE_INFERENCE)
    (void)absolutePath;
    (void)expectedObs;
    (void)expectedAct;
    return {nullptr,
            "Policy load requested but HadesEngine was built without "
            "HADES_ENABLE_HNE_INFERENCE. Reconfigure with "
            "-DHADES_ENABLE_HNE_INFERENCE=ON and rebuild."};
#else
    const std::string key = normalize_key(absolutePath);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (auto it = failures_.find(key); it != failures_.end())
      {
        return {nullptr, it->second};
      }
      if (auto it = cache_.find(key); it != cache_.end())
      {
        // Re-validate against the caller's expected specs every call — the
        // policy itself is fixed, but a different attachment with mismatched
        // specs may now be querying it.
        if (!spec_equals(it->second->observation_space(), expectedObs))
        {
          return {nullptr,
                  "Policy at " + key +
                      " observation space mismatch: policy=" +
                      describe_space(it->second->observation_space()) +
                      ", script expects=" + describe_space(expectedObs)};
        }
        if (!spec_equals(it->second->action_space(), expectedAct))
        {
          return {nullptr,
                  "Policy at " + key +
                      " action space mismatch: policy=" +
                      describe_space(it->second->action_space()) +
                      ", script expects=" + describe_space(expectedAct)};
        }
        return {it->second, {}};
      }
    }

    if (!std::filesystem::exists(absolutePath))
    {
      const std::string err =
          "Policy file does not exist: " + absolutePath.string();
      std::lock_guard<std::mutex> lock(mutex_);
      failures_.emplace(key, err);
      return {nullptr, err};
    }

    auto runtime = std::make_shared<hne::InferenceRuntime>();
    if (!runtime->load(absolutePath.string()))
    {
      const std::string err =
          "Failed to load TorchScript policy at " + absolutePath.string();
      std::lock_guard<std::mutex> lock(mutex_);
      failures_.emplace(key, err);
      return {nullptr, err};
    }

    const auto runtimeObs = runtime->observation_space();
    const auto runtimeAct = runtime->action_space();

    // Optional sidecar — if present, cross-check it against the runtime's
    // reported specs. A sidecar/runtime mismatch usually means the policy
    // was re-exported with stripped metadata; we surface it as a hard error
    // so the user re-runs the export.
    const auto sidecar = std::filesystem::path(absolutePath).replace_extension(".meta.json");
    if (std::filesystem::exists(sidecar))
    {
      try
      {
        std::ifstream in(sidecar);
        nlohmann::json meta;
        in >> meta;
        if (meta.contains("observation_space"))
        {
          hne::SpaceSpec sideObs = meta["observation_space"].get<hne::SpaceSpec>();
          if (!spec_equals(sideObs, runtimeObs))
          {
            const std::string err =
                "Sidecar/runtime obs-space mismatch for " + absolutePath.string() +
                ": sidecar=" + describe_space(sideObs) +
                ", runtime=" + describe_space(runtimeObs) +
                ". Re-export the policy.";
            std::lock_guard<std::mutex> lock(mutex_);
            failures_.emplace(key, err);
            return {nullptr, err};
          }
        }
        if (meta.contains("action_space"))
        {
          hne::SpaceSpec sideAct = meta["action_space"].get<hne::SpaceSpec>();
          if (!spec_equals(sideAct, runtimeAct))
          {
            const std::string err =
                "Sidecar/runtime action-space mismatch for " + absolutePath.string() +
                ": sidecar=" + describe_space(sideAct) +
                ", runtime=" + describe_space(runtimeAct) +
                ". Re-export the policy.";
            std::lock_guard<std::mutex> lock(mutex_);
            failures_.emplace(key, err);
            return {nullptr, err};
          }
        }
      }
      catch (const std::exception &e)
      {
        // A malformed sidecar is a soft failure — the runtime's own metadata
        // is still authoritative. Skip the cross-check rather than rejecting.
        (void)e;
      }
    }

    if (!spec_equals(runtimeObs, expectedObs))
    {
      const std::string err =
          "Policy at " + absolutePath.string() +
          " observation space mismatch: policy=" + describe_space(runtimeObs) +
          ", script expects=" + describe_space(expectedObs);
      std::lock_guard<std::mutex> lock(mutex_);
      failures_.emplace(key, err);
      return {nullptr, err};
    }
    if (!spec_equals(runtimeAct, expectedAct))
    {
      const std::string err =
          "Policy at " + absolutePath.string() +
          " action space mismatch: policy=" + describe_space(runtimeAct) +
          ", script expects=" + describe_space(expectedAct);
      std::lock_guard<std::mutex> lock(mutex_);
      failures_.emplace(key, err);
      return {nullptr, err};
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      cache_.emplace(key, runtime);
    }
    return {runtime, {}};
#endif
  }

  void PolicyRegistry::clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    failures_.clear();
  }
}
