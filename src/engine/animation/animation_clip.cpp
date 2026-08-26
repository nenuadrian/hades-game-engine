#include "animation_clip.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "../assets/model_asset.hpp"
#include "pose_ops.hpp"
#include "skeleton.hpp"

namespace hades
{
  namespace
  {
    /// Ordering used by every mutator: sub-tracks are kept sorted by time so
    /// evaluation can binary-search them.
    struct KeyTimeLess
    {
      template <typename KeyT>
      bool operator()(const KeyT &a, const KeyT &b) const
      {
        return a.time < b.time;
      }
    };

    /// upper_bound comparator: "does `time` fall before this key?".
    struct KeyTimeAfter
    {
      template <typename KeyT>
      bool operator()(float time, const KeyT &key) const
      {
        return time < key.time;
      }
    };

    template <typename KeyT>
    int index_of_key(const std::vector<KeyT> &keys, float time)
    {
      for (std::size_t i = 0; i < keys.size(); ++i)
      {
        if (std::fabs(keys[i].time - time) <= AnimationClipAsset::kKeyEpsilon)
        {
          return static_cast<int>(i);
        }
      }
      return -1;
    }

    template <typename KeyT>
    void sort_sub_track(std::vector<KeyT> &keys)
    {
      std::stable_sort(keys.begin(), keys.end(), KeyTimeLess{});
    }

    /// Insert or replace one key. A key within kKeyEpsilon of `time` keeps its
    /// own time and takes the new value/interpolation/ease, so repeatedly
    /// auto-keying the same frame does not drift the key off the frame grid.
    template <typename KeyT, typename ValueT>
    void set_key(std::vector<KeyT> &keys, float time, const ValueT &value,
                 Interpolation interpolation, const EaseCurve &ease)
    {
      const int existing = index_of_key(keys, time);
      if (existing >= 0)
      {
        KeyT &key = keys[static_cast<std::size_t>(existing)];
        key.value = value;
        key.interpolation = interpolation;
        key.ease = ease;
        return;
      }

      KeyT key;
      key.time = time;
      key.value = value;
      key.interpolation = interpolation;
      key.ease = ease;

      // Authoring appends far more often than it inserts, and an append is
      // already in order — but only if the track was in order to begin with.
      // `keys` is a public vector, so a caller may have pushed into it out of
      // sequence; comparing against keys.back() alone would then leave the
      // track unsorted and quietly break the binary search in
      // sample_channel(). The scan is O(n), which is what index_of_key()
      // above already costs, and it short-circuits on the first inversion.
      const bool appends = keys.empty() ||
                           (time > keys.back().time &&
                            std::is_sorted(keys.begin(), keys.end(), KeyTimeLess{}));
      keys.push_back(key);
      if (!appends)
      {
        sort_sub_track(keys);
      }
    }

    template <typename KeyT>
    bool erase_key(std::vector<KeyT> &keys, float time)
    {
      const int index = index_of_key(keys, time);
      if (index < 0)
      {
        return false;
      }
      keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
      return true;
    }

    template <typename KeyT>
    float move_key_in(std::vector<KeyT> &keys, float fromTime, float toTime)
    {
      const int index = index_of_key(keys, fromTime);
      if (index < 0)
      {
        return -1.0f;
      }

      KeyT moved = keys[static_cast<std::size_t>(index)];
      keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
      moved.time = toTime;

      // Dropping onto an existing key overwrites it — the dope sheet has no
      // way to show two keys on one frame.
      const int collision = index_of_key(keys, toTime);
      if (collision >= 0)
      {
        keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(collision));
      }

      keys.push_back(moved);
      sort_sub_track(keys);
      return toTime;
    }

    math::Vec3 blend_key_values(const math::Vec3 &a, const math::Vec3 &b, float t)
    {
      return math::lerp(a, b, t);
    }

    math::Quat blend_key_values(const math::Quat &a, const math::Quat &b, float t)
    {
      return math::slerp(a, b, t);
    }

    /// Value of one sub-track at `time`, holding the first/last key outside
    /// the keyed range. `keys` must be non-empty and sorted.
    template <typename KeyT>
    auto sample_channel(const std::vector<KeyT> &keys, float time)
    {
      const KeyT &first = keys.front();
      if (keys.size() == 1 || time <= first.time)
      {
        return first.value;
      }

      const KeyT &last = keys.back();
      if (time >= last.time)
      {
        return last.value;
      }

      // Binary search rather than a walk: a baked clip carries thousands of
      // keys per joint and the timeline scrubs to arbitrary times.
      const auto upper = std::upper_bound(keys.begin(), keys.end(), time, KeyTimeAfter{});
      const KeyT &a = *(upper - 1);
      const KeyT &b = *upper;

      const float span = b.time - a.time;
      const float raw = span > 1e-6f ? (time - a.time) / span : 0.0f;
      // The key that starts the segment owns the segment's easing.
      const float u = apply_easing(a.interpolation, raw, a.ease);
      return blend_key_values(a.value, b.value, u);
    }

    void sort_events_from(std::vector<const AnimationEventKey *> &out, std::size_t begin)
    {
      std::stable_sort(
          out.begin() + static_cast<std::ptrdiff_t>(begin), out.end(),
          [](const AnimationEventKey *a, const AnimationEventKey *b) { return a->time < b->time; });
    }

    // ---- JSON helpers ------------------------------------------------------

    /// `json::value()` only falls back when the key is ABSENT — it throws when
    /// it is present with another type, which a hand-edited file easily
    /// produces. These three check the type first so loading stays a
    /// bool-returning operation rather than an exception.
    float read_float(const nlohmann::json &in, const char *key, float fallback)
    {
      if (!in.contains(key) || !in.at(key).is_number())
      {
        return fallback;
      }
      return in.value(key, fallback);
    }

    int read_int(const nlohmann::json &in, const char *key, int fallback)
    {
      if (!in.contains(key) || !in.at(key).is_number())
      {
        return fallback;
      }
      return in.at(key).get<int>();
    }

    bool read_bool(const nlohmann::json &in, const char *key, bool fallback)
    {
      if (!in.contains(key) || !in.at(key).is_boolean())
      {
        return fallback;
      }
      return in.value(key, fallback);
    }

    std::string read_string(const nlohmann::json &in, const char *key, const std::string &fallback)
    {
      if (!in.contains(key) || !in.at(key).is_string())
      {
        return fallback;
      }
      return in.value(key, fallback);
    }

    float number_at(const nlohmann::json &array, std::size_t index, float fallback)
    {
      if (!array.is_array() || index >= array.size() || !array[index].is_number())
      {
        return fallback;
      }
      return array[index].get<float>();
    }

    nlohmann::json vec3_to_json(const math::Vec3 &value)
    {
      return nlohmann::json::array({value.x, value.y, value.z});
    }

    math::Vec3 vec3_from_json(const nlohmann::json &in, const math::Vec3 &fallback)
    {
      return math::Vec3{number_at(in, 0, fallback.x),
                        number_at(in, 1, fallback.y),
                        number_at(in, 2, fallback.z)};
    }

    nlohmann::json quat_to_json(const math::Quat &value)
    {
      return nlohmann::json::array({value.x, value.y, value.z, value.w});
    }

    math::Quat quat_from_json(const nlohmann::json &in)
    {
      const math::Quat value{number_at(in, 0, 0.0f),
                             number_at(in, 1, 0.0f),
                             number_at(in, 2, 0.0f),
                             number_at(in, 3, 1.0f)};

      // A rotation key is written straight into the pose and from there into
      // Quat::toMat4(), which assumes unit length: a key of [1,1,1,1] does not
      // rotate the joint, it scales and mirrors it and every joint beneath it.
      // A short key list (`"v": [1]`) produces exactly that, so repair a key
      // that is clearly off the unit sphere. A well-formed key is returned
      // bit-identical, so re-saving an untouched clip still reproduces the
      // file it was loaded from.
      const float lengthSq =
          value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
      if (lengthSq < 1e-8f)
      {
        return math::Quat{};
      }
      if (std::fabs(lengthSq - 1.0f) > 1e-3f)
      {
        return value.normalized();
      }
      return value;
    }

    /// Serialise the parts every key shares. `e` is only written for bezier
    /// keys, where it is the only place the curve can come from.
    void write_key_common(nlohmann::json &out, float time, Interpolation interpolation, const EaseCurve &ease)
    {
      out["t"] = time;
      out["i"] = interpolation_name(interpolation);
      if (interpolation == Interpolation::Bezier)
      {
        out["e"] = nlohmann::json::array({ease.x1, ease.y1, ease.x2, ease.y2});
      }
    }

    void read_key_common(const nlohmann::json &in, float &time, Interpolation &interpolation, EaseCurve &ease)
    {
      time = read_float(in, "t", 0.0f);

      Interpolation parsed = Interpolation::Linear;
      interpolation = interpolation_from_name(read_string(in, "i", "linear"), parsed)
                          ? parsed
                          : Interpolation::Linear;

      if (in.contains("e"))
      {
        const nlohmann::json &curve = in.at("e");
        ease.x1 = number_at(curve, 0, ease.x1);
        ease.y1 = number_at(curve, 1, ease.y1);
        ease.x2 = number_at(curve, 2, ease.x2);
        ease.y2 = number_at(curve, 3, ease.y2);
      }
    }

    nlohmann::json vec3_key_to_json(const AnimVec3Key &key)
    {
      nlohmann::json out;
      write_key_common(out, key.time, key.interpolation, key.ease);
      out["v"] = vec3_to_json(key.value);
      return out;
    }

    nlohmann::json quat_key_to_json(const AnimQuatKey &key)
    {
      nlohmann::json out;
      write_key_common(out, key.time, key.interpolation, key.ease);
      out["v"] = quat_to_json(key.value);
      return out;
    }

    void read_vec3_keys(const nlohmann::json &in, std::vector<AnimVec3Key> &out)
    {
      if (!in.is_array())
      {
        return;
      }
      out.reserve(out.size() + in.size());
      for (const auto &entry : in)
      {
        if (!entry.is_object())
        {
          continue;
        }
        AnimVec3Key key;
        read_key_common(entry, key.time, key.interpolation, key.ease);
        if (entry.contains("v"))
        {
          key.value = vec3_from_json(entry.at("v"), key.value);
        }
        out.push_back(key);
      }
    }

    void read_quat_keys(const nlohmann::json &in, std::vector<AnimQuatKey> &out)
    {
      if (!in.is_array())
      {
        return;
      }
      out.reserve(out.size() + in.size());
      for (const auto &entry : in)
      {
        if (!entry.is_object())
        {
          continue;
        }
        AnimQuatKey key;
        read_key_common(entry, key.time, key.interpolation, key.ease);
        if (entry.contains("v"))
        {
          key.value = quat_from_json(entry.at("v"));
        }
        out.push_back(key);
      }
    }
  }

  // ---- Track access --------------------------------------------------------

  const AnimationBoneTrack *AnimationClipAsset::find_track(const std::string &bone) const
  {
    for (const auto &track : tracks)
    {
      if (track.bone == bone)
      {
        return &track;
      }
    }
    return nullptr;
  }

  AnimationBoneTrack *AnimationClipAsset::find_track(const std::string &bone)
  {
    for (auto &track : tracks)
    {
      if (track.bone == bone)
      {
        return &track;
      }
    }
    return nullptr;
  }

  AnimationBoneTrack &AnimationClipAsset::track_for(const std::string &bone)
  {
    if (AnimationBoneTrack *existing = find_track(bone))
    {
      return *existing;
    }

    AnimationBoneTrack track;
    track.bone = bone;
    tracks.push_back(std::move(track));
    return tracks.back();
  }

  void AnimationClipAsset::remove_track(const std::string &bone)
  {
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [&bone](const AnimationBoneTrack &track) { return track.bone == bone; }),
        tracks.end());
  }

  // ---- Key editing ---------------------------------------------------------

  void AnimationClipAsset::set_translation_key(const std::string &bone, float time, const math::Vec3 &value,
                                               Interpolation interpolation, const EaseCurve &ease)
  {
    set_key(track_for(bone).translations, time, value, interpolation, ease);
  }

  void AnimationClipAsset::set_rotation_key(const std::string &bone, float time, const math::Quat &value,
                                            Interpolation interpolation, const EaseCurve &ease)
  {
    set_key(track_for(bone).rotations, time, value, interpolation, ease);
  }

  void AnimationClipAsset::set_scale_key(const std::string &bone, float time, const math::Vec3 &value,
                                         Interpolation interpolation, const EaseCurve &ease)
  {
    set_key(track_for(bone).scales, time, value, interpolation, ease);
  }

  void AnimationClipAsset::set_pose_key(const std::string &bone, float time,
                                        const math::Vec3 &translation, const math::Quat &rotation,
                                        const math::Vec3 &scale,
                                        Interpolation interpolation, const EaseCurve &ease)
  {
    // One lookup for all three channels: auto-key runs per dragged joint per
    // frame of the drag.
    AnimationBoneTrack &track = track_for(bone);
    set_key(track.translations, time, translation, interpolation, ease);
    set_key(track.rotations, time, rotation, interpolation, ease);
    set_key(track.scales, time, scale, interpolation, ease);
  }

  bool AnimationClipAsset::remove_key(const std::string &bone, TrackChannel channel, float time)
  {
    AnimationBoneTrack *track = find_track(bone);
    if (track == nullptr)
    {
      return false;
    }

    switch (channel)
    {
    case TrackChannel::Translation:
      return erase_key(track->translations, time);
    case TrackChannel::Rotation:
      return erase_key(track->rotations, time);
    case TrackChannel::Scale:
      return erase_key(track->scales, time);
    }
    return false;
  }

  float AnimationClipAsset::move_key(const std::string &bone, TrackChannel channel, float fromTime, float toTime)
  {
    AnimationBoneTrack *track = find_track(bone);
    if (track == nullptr)
    {
      return -1.0f;
    }

    switch (channel)
    {
    case TrackChannel::Translation:
      return move_key_in(track->translations, fromTime, toTime);
    case TrackChannel::Rotation:
      return move_key_in(track->rotations, fromTime, toTime);
    case TrackChannel::Scale:
      return move_key_in(track->scales, fromTime, toTime);
    }
    return -1.0f;
  }

  int AnimationClipAsset::key_index_at(const std::string &bone, TrackChannel channel, float time) const
  {
    const AnimationBoneTrack *track = find_track(bone);
    if (track == nullptr)
    {
      return -1;
    }

    switch (channel)
    {
    case TrackChannel::Translation:
      return index_of_key(track->translations, time);
    case TrackChannel::Rotation:
      return index_of_key(track->rotations, time);
    case TrackChannel::Scale:
      return index_of_key(track->scales, time);
    }
    return -1;
  }

  void AnimationClipAsset::key_times(std::vector<float> &out) const
  {
    out.clear();
    out.reserve(total_key_count() + events.size());

    for (const auto &track : tracks)
    {
      for (const auto &key : track.translations)
      {
        out.push_back(key.time);
      }
      for (const auto &key : track.rotations)
      {
        out.push_back(key.time);
      }
      for (const auto &key : track.scales)
      {
        out.push_back(key.time);
      }
    }
    for (const auto &event : events)
    {
      out.push_back(event.time);
    }

    std::sort(out.begin(), out.end());

    // Collapse against the last time kept, not the previous element, so a run
    // of near-identical times becomes one entry.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < out.size(); ++i)
    {
      if (kept == 0 || out[i] - out[kept - 1] > kKeyEpsilon)
      {
        out[kept] = out[i];
        ++kept;
      }
    }
    out.resize(kept);
  }

  // ---- Evaluation ----------------------------------------------------------

  void AnimationClipAsset::sample(const Skeleton &skeleton, float time, Pose &inOutPose) const
  {
    const float upperBound = duration > 0.0f ? duration : 0.0f;
    const float clamped = std::clamp(time, 0.0f, upperBound);

    for (const auto &track : tracks)
    {
      // Binding by name is what makes a clip retargetable: a joint this
      // skeleton does not have simply is not driven, rather than failing.
      const int joint = skeleton.find(track.bone);
      if (joint < 0)
      {
        continue;
      }
      const std::size_t index = static_cast<std::size_t>(joint);

      if (!track.translations.empty() && index < inOutPose.translations.size())
      {
        inOutPose.translations[index] = sample_channel(track.translations, clamped);
      }
      if (!track.rotations.empty() && index < inOutPose.rotations.size())
      {
        inOutPose.rotations[index] = sample_channel(track.rotations, clamped);
      }
      if (!track.scales.empty() && index < inOutPose.scales.size())
      {
        inOutPose.scales[index] = sample_channel(track.scales, clamped);
      }
    }
  }

  void AnimationClipAsset::sample_additive(const Skeleton &skeleton, float time, float weight, Pose &inOutPose) const
  {
    if (weight == 0.0f)
    {
      return;
    }

    // Both scratch poses start from rest so channels this clip does not key
    // cancel out of the delta exactly.
    Pose sampled = skeleton.rest_pose();
    Pose reference = sampled;
    sample(skeleton, time, sampled);
    sample(skeleton, additiveReferenceTime, reference);

    // Masking is a layer decision, not a clip one.
    const BoneMask mask;
    pose_ops::add_additive(inOutPose, sampled, reference, weight, mask);
  }

  void AnimationClipAsset::events_in_range(float fromTime, float toTime, bool looped,
                                           std::vector<const AnimationEventKey *> &out) const
  {
    out.clear();
    if (events.empty())
    {
      return;
    }

    // Half-open (fromTime, toTime]: an event fires the frame the play head
    // reaches it and never twice, and a paused frame fires nothing.
    if (fromTime <= toTime)
    {
      for (const auto &event : events)
      {
        if (event.time > fromTime && event.time <= toTime)
        {
          out.push_back(&event);
        }
      }
      sort_events_from(out, 0);
      return;
    }

    // Time went backwards. Without a loop that is a scrub or a manual seek,
    // which must not fire anything.
    if (!looped)
    {
      return;
    }

    // Wrapped frame: the tail of the clip happened before the head of it.
    for (const auto &event : events)
    {
      if (event.time > fromTime)
      {
        out.push_back(&event);
      }
    }
    sort_events_from(out, 0);

    const std::size_t headBegin = out.size();
    for (const auto &event : events)
    {
      if (event.time <= toTime)
      {
        out.push_back(&event);
      }
    }
    sort_events_from(out, headBegin);
  }

  // ---- Maintenance ---------------------------------------------------------

  void AnimationClipAsset::sort_keys()
  {
    for (auto &track : tracks)
    {
      sort_sub_track(track.translations);
      sort_sub_track(track.rotations);
      sort_sub_track(track.scales);
    }
  }

  float AnimationClipAsset::last_key_time() const
  {
    float last = 0.0f;
    for (const auto &track : tracks)
    {
      for (const auto &key : track.translations)
      {
        last = std::max(last, key.time);
      }
      for (const auto &key : track.rotations)
      {
        last = std::max(last, key.time);
      }
      for (const auto &key : track.scales)
      {
        last = std::max(last, key.time);
      }
    }
    for (const auto &event : events)
    {
      last = std::max(last, event.time);
    }
    return last;
  }

  void AnimationClipAsset::recompute_duration()
  {
    const float last = last_key_time();
    if (last > duration)
    {
      duration = last;
    }
    // A zero-length clip would divide by zero everywhere downstream.
    if (duration < 0.001f)
    {
      duration = 0.001f;
    }
  }

  void AnimationClipAsset::prune_empty_tracks()
  {
    tracks.erase(
        std::remove_if(tracks.begin(), tracks.end(),
                       [](const AnimationBoneTrack &track) { return track.empty(); }),
        tracks.end());
  }

  std::size_t AnimationClipAsset::total_key_count() const
  {
    std::size_t count = 0;
    for (const auto &track : tracks)
    {
      count += track.keyCount();
    }
    return count;
  }

  // ---- Persistence ---------------------------------------------------------

  nlohmann::json AnimationClipAsset::to_json() const
  {
    nlohmann::json out;
    out["version"] = kFormatVersion;
    out["name"] = name;
    out["sourceModel"] = sourceModel;
    out["duration"] = duration;
    out["frameRate"] = frameRate;
    out["looping"] = looping;
    out["additive"] = additive;
    out["additiveReferenceTime"] = additiveReferenceTime;

    nlohmann::json tracksJson = nlohmann::json::array();
    for (const auto &track : tracks)
    {
      nlohmann::json entry;
      entry["bone"] = track.bone;

      nlohmann::json translations = nlohmann::json::array();
      for (const auto &key : track.translations)
      {
        translations.push_back(vec3_key_to_json(key));
      }
      entry["translations"] = std::move(translations);

      nlohmann::json rotations = nlohmann::json::array();
      for (const auto &key : track.rotations)
      {
        rotations.push_back(quat_key_to_json(key));
      }
      entry["rotations"] = std::move(rotations);

      nlohmann::json scales = nlohmann::json::array();
      for (const auto &key : track.scales)
      {
        scales.push_back(vec3_key_to_json(key));
      }
      entry["scales"] = std::move(scales);

      tracksJson.push_back(std::move(entry));
    }
    out["tracks"] = std::move(tracksJson);

    nlohmann::json eventsJson = nlohmann::json::array();
    for (const auto &event : events)
    {
      nlohmann::json entry;
      entry["time"] = event.time;
      entry["name"] = event.name;
      entry["stringValue"] = event.stringValue;
      entry["floatValue"] = event.floatValue;
      eventsJson.push_back(std::move(entry));
    }
    out["events"] = std::move(eventsJson);

    return out;
  }

  bool AnimationClipAsset::from_json(const nlohmann::json &document, AnimationClipAsset &out,
                                     std::string *errorMessage)
  {
    out = AnimationClipAsset();

    if (!document.is_object())
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "animation clip document must be a JSON object";
      }
      return false;
    }

    // to_json() stamps the format version, so refusing a newer one is the only
    // thing that stops this build from loading a file it half-understands and
    // then writing it back as version 1 — silently deleting whatever the newer
    // format added. Matches RigAsset::from_json.
    const int version = read_int(document, "version", kFormatVersion);
    if (version > kFormatVersion)
    {
      if (errorMessage != nullptr)
      {
        *errorMessage = "animation clip format version " + std::to_string(version) +
                        " is newer than this build understands (" +
                        std::to_string(kFormatVersion) + ")";
      }
      return false;
    }

    out.name = read_string(document, "name", std::string());
    out.sourceModel = read_string(document, "sourceModel", std::string());
    out.duration = read_float(document, "duration", 1.0f);
    out.frameRate = read_float(document, "frameRate", 30.0f);
    out.looping = read_bool(document, "looping", true);
    out.additive = read_bool(document, "additive", false);
    out.additiveReferenceTime = read_float(document, "additiveReferenceTime", 0.0f);

    if (document.contains("tracks") && document.at("tracks").is_array())
    {
      for (const auto &entry : document.at("tracks"))
      {
        if (!entry.is_object())
        {
          continue;
        }

        // Merge rather than append: two entries naming the same joint would
        // otherwise produce two tracks, and the whole clip API disagrees about
        // which one is real — find_track()/remove_key()/move_key() (what the
        // editor drives) see the first, while sample() applies both and the
        // last one wins. The reader appends, so a repeated bone accumulates
        // into one track and sort_keys() below puts it back in order.
        AnimationBoneTrack &track = out.track_for(read_string(entry, "bone", std::string()));
        if (entry.contains("translations"))
        {
          read_vec3_keys(entry.at("translations"), track.translations);
        }
        if (entry.contains("rotations"))
        {
          read_quat_keys(entry.at("rotations"), track.rotations);
        }
        if (entry.contains("scales"))
        {
          read_vec3_keys(entry.at("scales"), track.scales);
        }
      }
    }

    if (document.contains("events") && document.at("events").is_array())
    {
      for (const auto &entry : document.at("events"))
      {
        if (!entry.is_object())
        {
          continue;
        }
        AnimationEventKey event;
        event.time = read_float(entry, "time", 0.0f);
        event.name = read_string(entry, "name", std::string());
        event.stringValue = read_string(entry, "stringValue", std::string());
        event.floatValue = read_float(entry, "floatValue", 0.0f);
        out.events.push_back(std::move(event));
      }
    }

    // A hand-edited file may list keys out of order; evaluation binary-searches.
    out.sort_keys();
    return true;
  }

  // ---- Import --------------------------------------------------------------

  bool AnimationClipAsset::bake_from_model(const ModelAsset &asset, int clipIndex, AnimationClipAsset &out)
  {
    out = AnimationClipAsset();

    if (clipIndex < 0 || clipIndex >= static_cast<int>(asset.clips.size()))
    {
      return false;
    }

    const AnimationClip &clip = asset.clips[static_cast<std::size_t>(clipIndex)];
    out.name = clip.name;
    out.duration = clip.duration;
    // sourceModel is the caller's to fill: only it knows the workspace path
    // the asset was loaded from.

    for (const auto &channel : clip.channels)
    {
      if (channel.nodeIndex < 0 || channel.nodeIndex >= static_cast<int>(asset.nodes.size()))
      {
        continue;
      }

      // Bind by node name, not index: a re-import may renumber the nodes.
      AnimationBoneTrack &track = out.track_for(asset.nodes[static_cast<std::size_t>(channel.nodeIndex)].name);

      // Two channels can resolve to one node -- the loader binds channels
      // through a first-name-wins map, so a file with two nodes called "Bone"
      // folds both onto the first. Appending would interleave two unrelated
      // curves at the same key times and the binary search would walk a
      // zig-zag that neither channel describes. Replace instead, which is
      // also what the preview path does: evaluateNodeGlobals rebuilds the
      // node's whole local matrix per channel, so the last channel wins
      // outright. All three sub-tracks are cleared for the same reason --
      // there the sub-channels the winning channel does not carry fall back
      // to the node's rest TRS, which is exactly what an emptied sub-track
      // leaves the pose holding.
      track.translations.clear();
      track.rotations.clear();
      track.scales.clear();

      track.translations.reserve(channel.positions.size());
      for (const auto &source : channel.positions)
      {
        AnimVec3Key key;
        key.time = source.time;
        key.value = source.value;
        key.interpolation = Interpolation::Linear;
        track.translations.push_back(key);
      }

      track.rotations.reserve(channel.rotations.size());
      for (const auto &source : channel.rotations)
      {
        AnimQuatKey key;
        key.time = source.time;
        key.value = source.value;
        key.interpolation = Interpolation::Linear;
        track.rotations.push_back(key);
      }

      track.scales.reserve(channel.scales.size());
      for (const auto &source : channel.scales)
      {
        AnimVec3Key key;
        key.time = source.time;
        key.value = source.value;
        key.interpolation = Interpolation::Linear;
        track.scales.push_back(key);
      }
    }

    // Assimp emits keys in order, but a hand-authored file need not.
    out.sort_keys();
    out.recompute_duration();
    return true;
  }

  AnimationClipAsset AnimationClipAsset::from_rest_pose(const Skeleton &skeleton, const std::string &clipName)
  {
    AnimationClipAsset clip;
    clip.name = clipName;
    clip.duration = 1.0f;
    clip.frameRate = 30.0f;
    clip.looping = true;

    clip.tracks.reserve(skeleton.size());
    const std::vector<SkeletonJoint> &joints = skeleton.joints();
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
      const SkeletonJoint &joint = joints[i];

      // Exported rigs routinely carry duplicate node names, and Skeleton::find
      // resolves a name to the FIRST joint that carries it. Keying every joint
      // blindly would let a later duplicate overwrite the track that the first
      // one owns, so sampling this clip would hand joint N the rest transform
      // of some unrelated joint M. Only key joints the name lookup can
      // actually reach: a joint no clip can bind to keeps whatever the pose
      // already holds, which is its own rest transform.
      if (skeleton.find(joint.name) != static_cast<int>(i))
      {
        continue;
      }
      clip.set_pose_key(joint.name, 0.0f, joint.restTranslation, joint.restRotation, joint.restScale);
    }
    return clip;
  }
}
