#include "animation_timeline.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "../engine/animation/skeleton.hpp"

namespace hades
{
  namespace
  {
    // ---- Look and feel -------------------------------------------------------

    constexpr float kKeyHalfExtent = 4.5f;
    /// Click slack around a key, in pixels. Slightly wider than the diamond so
    /// a key is grabbable without pixel hunting.
    constexpr float kKeyPickSlack = 5.5f;
    constexpr float kMinTickSpacing = 60.0f;
    constexpr float kMinFrameTickSpacing = 6.0f;
    constexpr float kMinSpan = 0.05f;
    constexpr float kMinViewStart = -0.5f;
    /// Rows past this count are drawn through an ImGuiListClipper: a 200 bone
    /// skeleton expands to well over a thousand rows.
    constexpr int kClipperThreshold = 60;
    constexpr int kCurveSubdivisions = 16;
    constexpr int kMaxCurveRows = 12;
    constexpr int kMaxCurveHandles = 256;
    /// Hard stop on tick loops so a degenerate view can never spin forever.
    constexpr int kMaxTicks = 1024;

    constexpr ImU32 kColRulerBg = IM_COL32(26, 28, 34, 255);
    constexpr ImU32 kColHeaderBg = IM_COL32(34, 36, 44, 255);
    constexpr ImU32 kColRowEven = IM_COL32(255, 255, 255, 6);
    constexpr ImU32 kColRowOdd = IM_COL32(0, 0, 0, 26);
    constexpr ImU32 kColSummaryRow = IM_COL32(90, 120, 170, 34);
    constexpr ImU32 kColEventRow = IM_COL32(170, 130, 60, 34);
    constexpr ImU32 kColSeparator = IM_COL32(0, 0, 0, 140);
    constexpr ImU32 kColGrid = IM_COL32(255, 255, 255, 16);
    constexpr ImU32 kColTick = IM_COL32(190, 195, 205, 190);
    constexpr ImU32 kColFrameTick = IM_COL32(150, 155, 165, 90);
    constexpr ImU32 kColText = IM_COL32(220, 224, 232, 255);
    constexpr ImU32 kColTextDim = IM_COL32(150, 155, 165, 255);
    constexpr ImU32 kColLabelUnbound = IM_COL32(150, 110, 110, 210);
    constexpr ImU32 kColKey = IM_COL32(226, 230, 238, 255);
    constexpr ImU32 kColKeyUnbound = IM_COL32(130, 132, 138, 255);
    constexpr ImU32 kColKeyEvent = IM_COL32(240, 190, 90, 255);
    constexpr ImU32 kColKeySelected = IM_COL32(255, 158, 58, 255);
    constexpr ImU32 kColKeyOutline = IM_COL32(20, 20, 24, 200);
    constexpr ImU32 kColKeySelectedOutline = IM_COL32(255, 255, 255, 235);
    constexpr ImU32 kColPlayhead = IM_COL32(255, 92, 92, 220);
    constexpr ImU32 kColBoxSelectFill = IM_COL32(120, 170, 255, 40);
    constexpr ImU32 kColBoxSelectLine = IM_COL32(140, 185, 255, 190);
    constexpr ImU32 kColOutOfRange = IM_COL32(0, 0, 0, 70);
    constexpr ImU32 kColBorder = IM_COL32(0, 0, 0, 160);
    constexpr ImU32 kColCurveBg = IM_COL32(20, 21, 26, 255);
    constexpr ImU32 kColAxis = IM_COL32(255, 255, 255, 28);
    // Component colours, ABGR as ImGui packs them: 0xFF4040FF and friends.
    constexpr ImU32 kColAxisX = IM_COL32(255, 80, 80, 255);
    constexpr ImU32 kColAxisY = IM_COL32(96, 220, 96, 255);
    constexpr ImU32 kColAxisZ = IM_COL32(96, 150, 255, 255);

    /// Per-frame geometry of the widget. Recomputed every frame — the panel is
    /// free to resize the label column or the window between frames.
    struct TimelineGeometry
    {
      ImVec2 origin{0.0f, 0.0f};
      float width = 0.0f;
      float labelWidth = 0.0f;
      float keyX = 0.0f;
      float keyWidth = 0.0f;
      float rowHeight = 20.0f;
      float rulerHeight = 24.0f;
    };

    /// One editable key, resolved to the track it actually lives on. A summary
    /// row stands for many of these.
    ///
    /// The track is an index into `clip.tracks`, not its name: dragging a
    /// summary diamond resolves one address per channel per selected key per
    /// frame, and copying a bone name into each of them cost more than the
    /// search that found it. An index only means anything next to the clip it
    /// came from, so `address_bone` below is the only way it is ever read back.
    struct KeyAddress
    {
      int track = -1;
      TrackChannel channel = TrackChannel::Translation;
    };

    std::string lower_copy(const std::string &text)
    {
      std::string out = text;
      for (char &c : out)
      {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return out;
    }

    ImU32 component_colour(int component)
    {
      switch (component)
      {
      case 0:
        return kColAxisX;
      case 1:
        return kColAxisY;
      default:
        return kColAxisZ;
      }
    }

    float vec3_component(const math::Vec3 &value, int component)
    {
      switch (component)
      {
      case 0:
        return value.x;
      case 1:
        return value.y;
      default:
        return value.z;
      }
    }

    void set_vec3_component(math::Vec3 &value, int component, float amount)
    {
      switch (component)
      {
      case 0:
        value.x = amount;
        break;
      case 1:
        value.y = amount;
        break;
      default:
        value.z = amount;
        break;
      }
    }

    const char *interpolation_display_name(Interpolation mode)
    {
      switch (mode)
      {
      case Interpolation::Step:
        return "Step";
      case Interpolation::Linear:
        return "Linear";
      case Interpolation::EaseIn:
        return "Ease in";
      case Interpolation::EaseOut:
        return "Ease out";
      case Interpolation::EaseInOut:
        return "Ease in-out";
      case Interpolation::Bezier:
        return "Bezier";
      }
      return interpolation_name(mode);
    }

    /// Sorted, de-duplicated in place with the same tolerance the clip uses, so
    /// a summary row shows one diamond per authored frame.
    void collapse_times(std::vector<float> &times)
    {
      std::sort(times.begin(), times.end());
      std::size_t kept = 0;
      for (std::size_t i = 0; i < times.size(); ++i)
      {
        if (kept == 0 || times[i] - times[kept - 1] > AnimationClipAsset::kKeyEpsilon)
        {
          times[kept] = times[i];
          ++kept;
        }
      }
      times.resize(kept);
    }

    /// Hash of every key time in the clip, events included.
    ///
    /// The Summary row is the union of exactly these times, so this is what
    /// decides whether a cached union is still good. It deliberately hashes the
    /// times themselves rather than the much cheaper track and key counts:
    /// undoing a key drag restores a retimed key without changing any count,
    /// any track name or the clip's duration, and a cache that missed that
    /// would draw the wrong diamonds for as long as the panel stayed open.
    /// Reading `.time` into a register is several times cheaper than the
    /// collect-sort-dedupe it stands in for, which is where the win comes from.
    std::uint64_t clip_time_fingerprint(const AnimationClipAsset &clip)
    {
      constexpr std::uint64_t kPrime = 0x100000001b3ull;
      std::uint64_t hash = 0xcbf29ce484222325ull;
      for (const AnimationBoneTrack &track : clip.tracks)
      {
        hash = (hash ^ (track.translations.size() * 3u + track.rotations.size() * 5u +
                        track.scales.size() * 7u)) *
               kPrime;
        for (const AnimVec3Key &key : track.translations)
        {
          hash = (hash ^ std::bit_cast<std::uint32_t>(key.time)) * kPrime;
        }
        for (const AnimQuatKey &key : track.rotations)
        {
          hash = (hash ^ std::bit_cast<std::uint32_t>(key.time)) * kPrime;
        }
        for (const AnimVec3Key &key : track.scales)
        {
          hash = (hash ^ std::bit_cast<std::uint32_t>(key.time)) * kPrime;
        }
      }
      for (const AnimationEventKey &event : clip.events)
      {
        hash = (hash ^ std::bit_cast<std::uint32_t>(event.time)) * kPrime;
      }
      return hash;
    }

    /// The Summary row's key times for the whole clip, collapsed and sorted,
    /// carried across frames — and, on demand, the keys behind each of them.
    ///
    /// `times` is what the row draws. The rest is what a drag over that row
    /// needs: `entries[slots[i] .. slots[i + 1])` are the keys filed under
    /// `times[i]`, tracks outer and channels in Translation/Rotation/Scale
    /// order — the order the row's old per-track scan emitted them in. Both
    /// come out of the same scan and are governed by the same fingerprint, so
    /// the two consumers share one pass over the clip per frame instead of
    /// taking one each.
    struct SummaryTimeCache
    {
      std::uint64_t fingerprint = 0;
      bool primed = false;
      /// Whether the three vectors below were built for this `fingerprint`.
      bool indexed = false;
      std::vector<float> times;
      std::vector<std::uint32_t> slots;
      std::vector<KeyAddress> entries;
      /// Each entry's own key time. A key is filed under a collapsed time it
      /// only matches to within `kKeyEpsilon`, and the answer this index
      /// stands in for was an exact epsilon test against the key itself.
      /// Parallel to `entries` rather than folded into it so the common case —
      /// a whole slot answering at once — stays a copy of a flat range.
      std::vector<float> entryTimes;
      /// Whether no slot holds two keys of the same track and channel. When
      /// that holds, a slot is its own answer and needs no de-duplication.
      bool distinct = true;
    };

    /// Per-widget state that has to outlive a frame. This belongs in
    /// `AnimationTimelineState`, but that lives in a header this file does not
    /// own; keying on the widget's own ImGui id is the same thing the drag and
    /// context-menu bookkeeping already does through ImGuiStorage, which can
    /// only hold PODs. Entries are never dropped — a widget id is stable for
    /// the life of the panel and there are only ever a handful of them.
    SummaryTimeCache &summary_cache_for(ImGuiID id)
    {
      // unique_ptr so growing the table cannot move a cache a caller is holding.
      static std::vector<std::pair<ImGuiID, std::unique_ptr<SummaryTimeCache>>> caches;
      for (const auto &entry : caches)
      {
        if (entry.first == id)
        {
          return *entry.second;
        }
      }
      caches.emplace_back(id, std::make_unique<SummaryTimeCache>());
      return *caches.back().second;
    }

    /// Times of one sub-track that fall inside [from, to]. The window is what
    /// keeps a big rig cheap: off-screen keys are neither drawn nor clickable,
    /// so they never reach the sort below.
    void append_channel_times(const AnimationBoneTrack &track, TrackChannel channel,
                              float from, float to, std::vector<float> &out)
    {
      const auto push = [from, to, &out](float time)
      {
        if (time >= from && time <= to)
        {
          out.push_back(time);
        }
      };

      switch (channel)
      {
      case TrackChannel::Translation:
        for (const AnimVec3Key &key : track.translations)
        {
          push(key.time);
        }
        break;
      case TrackChannel::Rotation:
        for (const AnimQuatKey &key : track.rotations)
        {
          push(key.time);
        }
        break;
      case TrackChannel::Scale:
        for (const AnimVec3Key &key : track.scales)
        {
          push(key.time);
        }
        break;
      }
    }

    /// Does one sub-track carry a key at `time`? Same tolerance the clip uses.
    ///
    /// Deliberately still a walk. `AnimationClipAsset` does keep every
    /// sub-track sorted, so std::lower_bound would answer this identically (I
    /// checked: 58,604 probes on and either side of every key, zero
    /// disagreements) — but it is *slower* here. The editor ships with no `-O`
    /// flag at all (build/CMakeFiles/Hades.dir/flags.make is `-std=c++20
    /// -arch arm64 -pthread`), and un-inlined lower_bound only overtakes the
    /// walk past ~120 keys per channel; at the 60 a two-second 30 fps clip
    /// carries it measured 0.74x the walk's speed, and the real
    /// `collect_keys_at` loop below went from 21.6 to 24.1 ms per drag frame
    /// when I tried it. Revisit if a build type ever gets set.
    bool track_has_key_at(const AnimationBoneTrack &track, TrackChannel channel, float time)
    {
      const auto hit = [time](float keyTime)
      { return std::fabs(keyTime - time) <= AnimationClipAsset::kKeyEpsilon; };

      switch (channel)
      {
      case TrackChannel::Translation:
        for (const AnimVec3Key &key : track.translations)
        {
          if (hit(key.time))
          {
            return true;
          }
        }
        break;
      case TrackChannel::Rotation:
        for (const AnimQuatKey &key : track.rotations)
        {
          if (hit(key.time))
          {
            return true;
          }
        }
        break;
      case TrackChannel::Scale:
        for (const AnimVec3Key &key : track.scales)
        {
          if (hit(key.time))
          {
            return true;
          }
        }
        break;
      }
      return false;
    }

    bool channel_has_keys(const AnimationBoneTrack &track, TrackChannel channel)
    {
      switch (channel)
      {
      case TrackChannel::Translation:
        return !track.translations.empty();
      case TrackChannel::Rotation:
        return !track.rotations.empty();
      case TrackChannel::Scale:
        return !track.scales.empty();
      }
      return false;
    }

    /// Every key time in the clip, collapsed and sorted — the Summary row's
    /// content, rebuilt only when a key time somewhere in the clip changed.
    ///
    /// Doing it whole-clip rather than per view window is what makes the cache
    /// worth having: the window moves with every zoom and scroll, the key times
    /// do not, so the result stays good while the user navigates.
    const std::vector<float> &summary_times(const AnimationClipAsset &clip, SummaryTimeCache &cache)
    {
      const std::uint64_t fingerprint = clip_time_fingerprint(clip);
      if (cache.primed && cache.fingerprint == fingerprint)
      {
        return cache.times;
      }

      constexpr float kAll = std::numeric_limits<float>::max();
      cache.times.clear();
      for (const AnimationBoneTrack &track : clip.tracks)
      {
        append_channel_times(track, TrackChannel::Translation, -kAll, kAll, cache.times);
        append_channel_times(track, TrackChannel::Rotation, -kAll, kAll, cache.times);
        append_channel_times(track, TrackChannel::Scale, -kAll, kAll, cache.times);
      }
      for (const AnimationEventKey &event : clip.events)
      {
        cache.times.push_back(event.time);
      }
      collapse_times(cache.times);
      cache.fingerprint = fingerprint;
      cache.primed = true;
      // The grouping below is filed under these times, so it dies with them.
      cache.indexed = false;
      return cache.times;
    }

    /// The slot one key belongs to: the last collapsed time at or below it.
    ///
    /// `cursor` walks forward across a whole channel instead of restarting per
    /// key, because a channel's keys and the slot list are both sorted. The
    /// rewind is for a track whose vectors were filled by hand rather than
    /// through the clip's own API and so are not in order — it keeps the
    /// answer right at the cost of a longer walk. `times` is built from these
    /// very key times, so a slot at or below the key always exists and is
    /// within one `kKeyEpsilon` of it.
    std::size_t slot_of_key(const float *times, std::size_t slotCount, std::size_t &cursor, float time)
    {
      std::size_t slot = cursor;
      if (time < times[slot])
      {
        slot = 0;
      }
      while (slot + 1 < slotCount && times[slot + 1] <= time)
      {
        ++slot;
      }
      cursor = slot;
      return slot;
    }

    /// Tally one channel's keys into `counts[slot + 1]`.
    ///
    /// Written per channel rather than per key — through raw pointers, and
    /// duplicated for the counting and filling passes — because the editor
    /// ships with no `-O` flag at all, where a callback and a bounds-checked
    /// `operator[]` per key each cost more than the slot walk they carry.
    template <typename KeyT>
    void count_channel_slots(const std::vector<KeyT> &keys, const std::vector<float> &times,
                             std::size_t &cursor, std::vector<std::uint32_t> &counts)
    {
      const float *slotTimes = times.data();
      const std::size_t slotCount = times.size();
      std::uint32_t *out = counts.data();
      for (const KeyT &key : keys)
      {
        ++out[slot_of_key(slotTimes, slotCount, cursor, key.time) + 1];
      }
    }

    /// File one channel's keys into their slots. Returns true when two of them
    /// landed in the same slot, which is the only way one channel can end up
    /// answering a time twice.
    template <typename KeyT>
    bool fill_channel_slots(const std::vector<KeyT> &keys, const std::vector<float> &times,
                            std::size_t &cursor, std::vector<std::uint32_t> &next,
                            int track, TrackChannel channel, SummaryTimeCache &cache)
    {
      const float *slotTimes = times.data();
      const std::size_t slotCount = times.size();
      std::uint32_t *at = next.data();
      float *outTimes = cache.entryTimes.data();
      KeyAddress *outEntries = cache.entries.data();
      bool repeated = false;
      std::size_t previous = slotCount;
      for (const KeyT &key : keys)
      {
        const std::size_t slot = slot_of_key(slotTimes, slotCount, cursor, key.time);
        repeated = repeated || slot == previous;
        previous = slot;
        const std::uint32_t index = at[slot]++;
        outTimes[index] = key.time;
        outEntries[index] = KeyAddress{track, channel};
      }
      return repeated;
    }

    /// The same cache, with every key filed under the collapsed time it sits
    /// on. `collect_keys_at` reads it instead of rescanning the clip.
    ///
    /// Built on top of `summary_times` and lazily: only a drag, a delete or an
    /// interpolation change over a Summary selection ever reads the grouping,
    /// and a redraw of a clip that was just edited should not pay to build
    /// what it never looks at. Once built it is free for the rest of the
    /// frame, which is the point — a Summary drag asks this question once per
    /// selected diamond.
    const SummaryTimeCache &summary_index(const AnimationClipAsset &clip, SummaryTimeCache &cache)
    {
      summary_times(clip, cache);
      if (cache.indexed)
      {
        return cache;
      }

      const std::size_t slotCount = cache.times.size();
      cache.slots.assign(slotCount + 1, 0);
      cache.entries.clear();
      cache.entryTimes.clear();
      cache.distinct = true;
      cache.indexed = true;
      if (slotCount == 0)
      {
        return cache;
      }

      // Count, prefix-sum, fill: one flat vector rather than a vector per
      // slot, so a rebuild costs two passes over the keys and no allocation
      // per slot.
      std::size_t cursor = 0;
      for (const AnimationBoneTrack &track : clip.tracks)
      {
        count_channel_slots(track.translations, cache.times, cursor, cache.slots);
        count_channel_slots(track.rotations, cache.times, cursor, cache.slots);
        count_channel_slots(track.scales, cache.times, cursor, cache.slots);
      }
      for (std::size_t slot = 1; slot <= slotCount; ++slot)
      {
        cache.slots[slot] += cache.slots[slot - 1];
      }

      const std::size_t total = static_cast<std::size_t>(cache.slots[slotCount]);
      cache.entries.resize(total);
      cache.entryTimes.resize(total);
      std::vector<std::uint32_t> next(cache.slots.begin(), cache.slots.end() - 1);
      cursor = 0;
      for (std::size_t index = 0; index < clip.tracks.size(); ++index)
      {
        const AnimationBoneTrack &track = clip.tracks[index];
        const int owner = static_cast<int>(index);
        bool repeated = fill_channel_slots(track.translations, cache.times, cursor, next, owner,
                                           TrackChannel::Translation, cache);
        repeated = fill_channel_slots(track.rotations, cache.times, cursor, next, owner,
                                      TrackChannel::Rotation, cache) ||
                   repeated;
        repeated = fill_channel_slots(track.scales, cache.times, cursor, next, owner,
                                      TrackChannel::Scale, cache) ||
                   repeated;
        cache.distinct = cache.distinct && !repeated;
      }
      return cache;
    }

    /// Index of the track named `bone`, or -1.
    int find_track_index(const AnimationClipAsset &clip, const std::string &bone)
    {
      for (std::size_t index = 0; index < clip.tracks.size(); ++index)
      {
        if (clip.tracks[index].bone == bone)
        {
          return static_cast<int>(index);
        }
      }
      return -1;
    }

    /// The bone name an address stands for, or nullptr when the address does
    /// not belong to `clip`. The bounds test is what makes an index-only
    /// address safe: the Summary index is keyed on a hash of the clip's key
    /// times, and a hash can collide, so a stale index must fall out of a
    /// selection pass rather than off the end of the track list.
    const std::string *address_bone(const AnimationClipAsset &clip, const KeyAddress &address)
    {
      if (address.track < 0 || static_cast<std::size_t>(address.track) >= clip.tracks.size())
      {
        return nullptr;
      }
      return &clip.tracks[static_cast<std::size_t>(address.track)].bone;
    }

    /// Key times drawn on one row, restricted to the visible window. Summary
    /// rows show the union of everything they stand for, which is what makes a
    /// collapsed skeleton readable.
    void row_key_times(const AnimationClipAsset &clip, const TimelineRow &row,
                       float from, float to, SummaryTimeCache &cache, std::vector<float> &out)
    {
      out.clear();
      switch (row.kind)
      {
      case TimelineRow::Kind::Summary:
      {
        // Sliced out of the cached whole-clip union instead of rescanned: this
        // row stands for every key in the clip, so scanning and sorting it per
        // frame cost more than every other drawn row put together.
        //
        // Collapsing before the window rather than after only shows up when two
        // keys sit within kKeyEpsilon of each other *and* the window edge falls
        // between them, in which case the pair's representative can differ by
        // up to one epsilon — far below one pixel at any zoom the margin below
        // permits. In exchange the row stops reshuffling its diamonds as the
        // view scrolls, which the old per-window collapse did.
        const std::vector<float> &all = summary_times(clip, cache);
        const auto first = std::lower_bound(all.begin(), all.end(), from);
        const auto last = std::upper_bound(first, all.end(), to);
        out.assign(first, last);
        break;
      }
      case TimelineRow::Kind::BoneSummary:
      {
        const AnimationBoneTrack *track = clip.find_track(row.bone);
        if (track == nullptr)
        {
          break;
        }
        append_channel_times(*track, TrackChannel::Translation, from, to, out);
        append_channel_times(*track, TrackChannel::Rotation, from, to, out);
        append_channel_times(*track, TrackChannel::Scale, from, to, out);
        collapse_times(out);
        break;
      }
      case TimelineRow::Kind::Channel:
      {
        const AnimationBoneTrack *track = clip.find_track(row.bone);
        if (track == nullptr)
        {
          break;
        }
        append_channel_times(*track, row.channel, from, to, out);
        break;
      }
      case TimelineRow::Kind::Events:
        for (const AnimationEventKey &event : clip.events)
        {
          if (event.time >= from && event.time <= to)
          {
            out.push_back(event.time);
          }
        }
        collapse_times(out);
        break;
      }
    }

    /// Every real key sitting at `time` on `row`. Summary rows resolve to the
    /// keys of every channel they cover, which is what makes dragging a summary
    /// diamond drag the whole frame.
    ///
    /// `summary` must be the index for `clip` whenever `row` is a Summary row.
    /// It is a pointer, and resolved by the callers immediately before their
    /// first Summary key rather than taken here, for two reasons: validating it
    /// costs a pass over every key time in the clip, which a selection holding
    /// no summary diamond should not pay; and the callers below mutate the clip
    /// as they go, so re-validating per key would rebuild it once per selected
    /// diamond — the very cost this replaces.
    void collect_keys_at(const AnimationClipAsset &clip, const TimelineRow &row, float time,
                         const SummaryTimeCache *summary, std::vector<KeyAddress> &out)
    {
      out.clear();
      const TrackChannel kChannels[] = {TrackChannel::Translation, TrackChannel::Rotation, TrackChannel::Scale};

      switch (row.kind)
      {
      case TimelineRow::Kind::Summary:
      {
        // Read out of the shared index rather than rescanned. This runs once
        // per selected diamond per frame of a drag, so the walk it replaces was
        // O(tracks x channels x keys) *per selected key*: 2.2M key comparisons
        // and 36k bone-name copies a frame on a 200 track rig.
        if (summary == nullptr)
        {
          break;
        }

        const std::vector<float> &times = summary->times;
        if (times.empty())
        {
          break;
        }
        constexpr float kEpsilon = AnimationClipAsset::kKeyEpsilon;

        // Which slots can hold a key that answers `time` at all. A key sits at
        // or up to one epsilon above the collapsed time it is filed under, so a
        // slot qualifies when its time is no more than one epsilon above `time`
        // or two below it. Walked outwards from the search rather than searched
        // for with `time +- epsilon` as the bound: adding an epsilon to a time
        // rounds, and a bound that lands a fraction of an ulp the wrong side of
        // a slot drops keys the old scan found, whereas the difference of two
        // neighbouring times does not.
        const std::size_t after = static_cast<std::size_t>(
            std::upper_bound(times.begin(), times.end(), time) - times.begin());
        std::size_t firstSlot = after;
        while (firstSlot > 0 && time - times[firstSlot - 1] <= 2.0f * kEpsilon)
        {
          --firstSlot;
        }
        std::size_t lastSlot = after;
        while (lastSlot < times.size() && times[lastSlot] - time <= kEpsilon)
        {
          ++lastSlot;
        }

        if (summary->distinct && firstSlot + 1 == lastSlot && times[firstSlot] == time)
        {
          // The ordinary case, and the whole point of the index: the drag is
          // sitting exactly on a collapsed time, so every key filed under it is
          // inside the tolerance by construction and none of them repeats a
          // channel. The slot is the answer, verbatim.
          const auto begin = summary->entries.begin() + static_cast<std::ptrdiff_t>(summary->slots[firstSlot]);
          const auto end = summary->entries.begin() + static_cast<std::ptrdiff_t>(summary->slots[firstSlot + 1]);
          out.assign(begin, end);
          break;
        }

        std::size_t contributing = 0;
        for (std::size_t slot = firstSlot; slot < lastSlot; ++slot)
        {
          const std::size_t begin = static_cast<std::size_t>(summary->slots[slot]);
          const std::size_t end = static_cast<std::size_t>(summary->slots[slot + 1]);
          const std::size_t before = out.size();
          for (std::size_t entry = begin; entry < end; ++entry)
          {
            if (std::fabs(summary->entryTimes[entry] - time) > kEpsilon)
            {
              continue;
            }
            const KeyAddress &address = summary->entries[entry];
            // The old scan asked "does this channel hold such a key" once per
            // channel, so a channel holding two keys inside the tolerance still
            // owes exactly one address — and move_key would otherwise move both
            // of them. Repeats of a channel are adjacent within a slot.
            if (out.size() > before && out.back().track == address.track &&
                out.back().channel == address.channel)
            {
              continue;
            }
            out.push_back(address);
          }
          if (out.size() > before)
          {
            ++contributing;
          }
        }

        if (contributing > 1)
        {
          // Two slots answered, so the run came out slot-major where the scan
          // was track-major, and one channel can now appear in both. Sorting
          // and uniquing restores both properties. Unreachable unless two keys
          // sit closer together than the collapse tolerance.
          const auto earlier = [](const KeyAddress &a, const KeyAddress &b)
          {
            return a.track != b.track ? a.track < b.track
                                      : static_cast<int>(a.channel) < static_cast<int>(b.channel);
          };
          const auto same = [](const KeyAddress &a, const KeyAddress &b)
          { return a.track == b.track && a.channel == b.channel; };
          std::sort(out.begin(), out.end(), earlier);
          out.erase(std::unique(out.begin(), out.end(), same), out.end());
        }
        break;
      }
      case TimelineRow::Kind::BoneSummary:
      {
        const int trackIndex = find_track_index(clip, row.bone);
        if (trackIndex < 0)
        {
          break;
        }
        const AnimationBoneTrack &track = clip.tracks[static_cast<std::size_t>(trackIndex)];
        for (TrackChannel channel : kChannels)
        {
          if (track_has_key_at(track, channel, time))
          {
            out.push_back(KeyAddress{trackIndex, channel});
          }
        }
        break;
      }
      case TimelineRow::Kind::Channel:
      {
        const int trackIndex = find_track_index(clip, row.bone);
        if (trackIndex >= 0 &&
            track_has_key_at(clip.tracks[static_cast<std::size_t>(trackIndex)], row.channel, time))
        {
          out.push_back(KeyAddress{trackIndex, row.channel});
        }
        break;
      }
      case TimelineRow::Kind::Events:
        break;
      }
    }

    bool row_covers_events(const TimelineRow &row)
    {
      return row.kind == TimelineRow::Kind::Events || row.kind == TimelineRow::Kind::Summary;
    }

    bool move_events_at(AnimationClipAsset &clip, float fromTime, float toTime)
    {
      bool moved = false;
      for (AnimationEventKey &event : clip.events)
      {
        if (std::fabs(event.time - fromTime) <= AnimationClipAsset::kKeyEpsilon)
        {
          event.time = toTime;
          moved = true;
        }
      }
      if (moved)
      {
        std::stable_sort(clip.events.begin(), clip.events.end(),
                         [](const AnimationEventKey &a, const AnimationEventKey &b)
                         { return a.time < b.time; });
      }
      return moved;
    }

    bool remove_events_at(AnimationClipAsset &clip, float time)
    {
      const std::size_t before = clip.events.size();
      clip.events.erase(
          std::remove_if(clip.events.begin(), clip.events.end(),
                         [time](const AnimationEventKey &event)
                         { return std::fabs(event.time - time) <= AnimationClipAsset::kKeyEpsilon; }),
          clip.events.end());
      return clip.events.size() != before;
    }

    // ---- View ----------------------------------------------------------------

    void clamp_view(AnimationTimelineState &state, const AnimationClipAsset &clip)
    {
      const float maxSpan = std::max(clip.duration * 4.0f, 10.0f);
      float span = state.viewEnd - state.viewStart;
      if (!(span > 0.0f))
      {
        span = 1.0f;
      }
      span = std::clamp(span, kMinSpan, maxSpan);
      if (state.viewStart < kMinViewStart)
      {
        state.viewStart = kMinViewStart;
      }
      state.viewEnd = state.viewStart + span;
    }

    float tick_step_for(float pixelsPerSecond)
    {
      if (!(pixelsPerSecond > 0.0f))
      {
        return 1.0f;
      }

      const float kSteps[] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
      for (float step : kSteps)
      {
        if (step * pixelsPerSecond >= kMinTickSpacing)
        {
          return step;
        }
      }

      // Zoomed out past the table: keep doubling so the ruler stays readable.
      float step = 10.0f;
      while (step * pixelsPerSecond < kMinTickSpacing && step < 1.0e6f)
      {
        step *= 2.0f;
      }
      return step;
    }

    void draw_key_diamond(ImDrawList *drawList, float cx, float cy, ImU32 fill, ImU32 outline)
    {
      const ImVec2 top(cx, cy - kKeyHalfExtent);
      const ImVec2 right(cx + kKeyHalfExtent, cy);
      const ImVec2 bottom(cx, cy + kKeyHalfExtent);
      const ImVec2 left(cx - kKeyHalfExtent, cy);
      drawList->AddQuadFilled(top, right, bottom, left, fill);
      drawList->AddQuad(top, right, bottom, left, outline, 1.0f);
    }

    // ---- Selection -----------------------------------------------------------

    /// Bucket a key time onto the clip's key grid. Only a bucket — membership
    /// is still decided by the same kKeyEpsilon comparison isSelected() makes,
    /// so a time sitting a hair either side of a bucket boundary still matches.
    ///
    /// Floor, deliberately, not round-to-nearest. `contains()` below only probes
    /// the bucket and its two neighbours, which is sound exactly when two times
    /// within kKeyEpsilon of each other can never land more than one bucket
    /// apart. Flooring gives that for every pair of reals; rounding does not,
    /// because std::lround rounds halves *away from zero* and so splits a pair
    /// straddling t=0 by two buckets (t = +eps/2 and t = -eps/2 are one epsilon
    /// apart yet quantise to +1 and -1). That pair is within tolerance, so
    /// isSelected() calls it selected and the three probes would have missed it.
    std::int64_t quantise_key_time(float time)
    {
      // std::floor on a NaN is a NaN and the cast would then be undefined; a
      // corrupt clip must not be able to take the editor down. Such a time
      // lands in bucket 0 and then fails the exact test, which is what
      // isSelected() does with it too.
      if (!(time > -1.0e9f && time < 1.0e9f))
      {
        return 0;
      }
      return static_cast<std::int64_t>(
          std::floor(static_cast<double>(time) / static_cast<double>(AnimationClipAsset::kKeyEpsilon)));
    }

    std::uint64_t selection_bucket_hash(int row, std::int64_t bucket)
    {
      std::uint64_t hash = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(row)) << 32) ^
                           static_cast<std::uint64_t>(bucket);
      // splitmix64 finaliser. Row and bucket are both dense small integers and
      // linear probing needs the low bits spread.
      hash ^= hash >> 30;
      hash *= 0xbf58476d1ce4e5b9ull;
      hash ^= hash >> 27;
      hash *= 0x94d049bb133111ebull;
      hash ^= hash >> 31;
      return hash;
    }

    /// Constant-time "is this key selected?" for the dope sheet.
    ///
    /// `AnimationTimelineState::selection` is a flat vector and isSelected() is
    /// a linear scan of it. That scan runs once per drawn diamond and once per
    /// candidate key on a marquee release, so both the draw and the box select
    /// grow quadratically with the selection: a marquee over a 200 bone clip
    /// selects ~11,600 keys and takes ~290 ms to close.
    ///
    /// Backed by a flat open-addressed table from (row, quantised time) to an
    /// index into the selection, rebuilt lazily whenever something this class
    /// did not do changes the selection. Small selections skip the table
    /// entirely — building one would cost more than the scan it saves.
    class SelectionLookup
    {
    public:
      explicit SelectionLookup(AnimationTimelineState &state)
          : state_(state)
      {
      }

      bool contains(const TimelineKeyRef &key)
      {
        if (state_.selection.size() < kIndexThreshold)
        {
          return state_.isSelected(key);
        }
        rebuild_if_stale();
        const std::int64_t bucket = quantise_key_time(key.time);
        // A selection entry within kKeyEpsilon of `key` can only have landed in
        // this bucket or one of its neighbours, so three probes cover it.
        return probe(key.row, bucket - 1, key.time) ||
               probe(key.row, bucket, key.time) ||
               probe(key.row, bucket + 1, key.time);
      }

      /// Select `key`, keeping the table in step so selecting n keys stays O(n).
      void add(const TimelineKeyRef &key)
      {
        state_.selection.push_back(key);
        if (!primed_)
        {
          return;
        }
        if ((state_.selection.size() + 1) * 2 > slots_.size())
        {
          // Out of room: rehash on the next query rather than mid-loop.
          primed_ = false;
          return;
        }
        insert(state_.selection.size() - 1);
      }

      void clear()
      {
        state_.selection.clear();
        primed_ = false;
      }

      /// Call after any change to the selection made behind this class's back.
      void invalidate()
      {
        primed_ = false;
      }

    private:
      /// Below this the scan wins: the table costs one allocation to build.
      static constexpr std::size_t kIndexThreshold = 64;

      void rebuild_if_stale()
      {
        if (primed_)
        {
          return;
        }
        std::size_t capacity = 64;
        while (capacity < (state_.selection.size() + 1) * 2)
        {
          capacity <<= 1;
        }
        slots_.assign(capacity, 0);
        mask_ = capacity - 1;
        primed_ = true;
        for (std::size_t i = 0; i < state_.selection.size(); ++i)
        {
          insert(i);
        }
      }

      void insert(std::size_t index)
      {
        const TimelineKeyRef &ref = state_.selection[index];
        std::size_t slot =
            static_cast<std::size_t>(selection_bucket_hash(ref.row, quantise_key_time(ref.time))) & mask_;
        while (slots_[slot] != 0)
        {
          slot = (slot + 1) & mask_;
        }
        slots_[slot] = static_cast<std::uint32_t>(index + 1);
      }

      bool probe(int row, std::int64_t bucket, float time) const
      {
        std::size_t slot = static_cast<std::size_t>(selection_bucket_hash(row, bucket)) & mask_;
        while (slots_[slot] != 0)
        {
          const TimelineKeyRef &ref = state_.selection[slots_[slot] - 1];
          if (ref.row == row && quantise_key_time(ref.time) == bucket &&
              std::fabs(ref.time - time) <= AnimationClipAsset::kKeyEpsilon)
          {
            return true;
          }
          slot = (slot + 1) & mask_;
        }
        return false;
      }

      AnimationTimelineState &state_;
      /// Index into `state_.selection`, plus one; zero means the slot is free.
      std::vector<std::uint32_t> slots_;
      std::size_t mask_ = 0;
      bool primed_ = false;
    };

    // ---- Clip mutation -------------------------------------------------------

    /// Shift every selected key by `delta` seconds and update the selection to
    /// follow. Returns true when the clip changed.
    bool move_selection(AnimationClipAsset &clip, const std::vector<TimelineRow> &rows,
                        AnimationTimelineState &state, SummaryTimeCache &cache, float delta)
    {
      if (state.selection.empty() || delta == 0.0f)
      {
        return false;
      }

      // A key dropped onto another key replaces it, so the order matters: when
      // moving right, the rightmost key has to go first or it would be eaten by
      // the key behind it before it ever moved.
      std::vector<int> order(state.selection.size());
      std::iota(order.begin(), order.end(), 0);
      std::sort(order.begin(), order.end(),
                [&state, delta](int a, int b)
                {
                  const float ta = state.selection[static_cast<std::size_t>(a)].time;
                  const float tb = state.selection[static_cast<std::size_t>(b)].time;
                  return delta > 0.0f ? ta > tb : ta < tb;
                });

      std::vector<KeyAddress> addresses;
      // Resolved on the first Summary key of the pass and then held for the
      // rest of it. It stays right across the moves below because the order
      // above puts every target strictly beyond every source time still to be
      // visited, so a key that has already moved can neither leave nor join a
      // time this loop has yet to ask about; and because nothing here adds or
      // removes a track, so the indices keep their meaning. Re-validating per
      // key instead would re-hash every key time in the clip once per selected
      // diamond.
      const SummaryTimeCache *summary = nullptr;
      bool changed = false;
      for (int index : order)
      {
        TimelineKeyRef &ref = state.selection[static_cast<std::size_t>(index)];
        if (ref.row < 0 || static_cast<std::size_t>(ref.row) >= rows.size())
        {
          continue;
        }

        const TimelineRow &row = rows[static_cast<std::size_t>(ref.row)];
        const float target = std::max(ref.time + delta, 0.0f);
        if (row.kind == TimelineRow::Kind::Summary && summary == nullptr)
        {
          summary = &summary_index(clip, cache);
        }

        collect_keys_at(clip, row, ref.time, summary, addresses);
        for (const KeyAddress &address : addresses)
        {
          const std::string *bone = address_bone(clip, address);
          if (bone == nullptr)
          {
            continue;
          }
          if (clip.move_key(*bone, address.channel, ref.time, target) >= 0.0f)
          {
            changed = true;
          }
        }
        if (row_covers_events(row) && move_events_at(clip, ref.time, target))
        {
          changed = true;
        }

        ref.time = target;
      }
      return changed;
    }

    bool delete_selection(AnimationClipAsset &clip, const std::vector<TimelineRow> &rows,
                          AnimationTimelineState &state, SummaryTimeCache &cache)
    {
      if (state.selection.empty())
      {
        return false;
      }

      std::vector<KeyAddress> addresses;
      // Same contract as move_selection's: resolved once, then held. Erasing a
      // key at one selected time cannot change which keys sit at another, and
      // no track is added or removed here, so one snapshot answers the pass.
      const SummaryTimeCache *summary = nullptr;
      bool changed = false;
      for (const TimelineKeyRef &ref : state.selection)
      {
        if (ref.row < 0 || static_cast<std::size_t>(ref.row) >= rows.size())
        {
          continue;
        }

        const TimelineRow &row = rows[static_cast<std::size_t>(ref.row)];
        if (row.kind == TimelineRow::Kind::Summary && summary == nullptr)
        {
          summary = &summary_index(clip, cache);
        }
        collect_keys_at(clip, row, ref.time, summary, addresses);
        for (const KeyAddress &address : addresses)
        {
          const std::string *bone = address_bone(clip, address);
          if (bone == nullptr)
          {
            continue;
          }
          if (clip.remove_key(*bone, address.channel, ref.time))
          {
            changed = true;
          }
        }
        if (row_covers_events(row) && remove_events_at(clip, ref.time))
        {
          changed = true;
        }
      }
      return changed;
    }

    bool apply_interpolation(AnimationClipAsset &clip, const std::vector<TimelineRow> &rows,
                             const AnimationTimelineState &state, SummaryTimeCache &cache,
                             Interpolation mode)
    {
      std::vector<KeyAddress> addresses;
      // Nothing here retimes a key, so the index cannot go stale at all.
      const SummaryTimeCache *summary = nullptr;
      bool changed = false;
      for (const TimelineKeyRef &ref : state.selection)
      {
        if (ref.row < 0 || static_cast<std::size_t>(ref.row) >= rows.size())
        {
          continue;
        }

        const TimelineRow &row = rows[static_cast<std::size_t>(ref.row)];
        if (row.kind == TimelineRow::Kind::Summary && summary == nullptr)
        {
          summary = &summary_index(clip, cache);
        }
        collect_keys_at(clip, row, ref.time, summary, addresses);
        for (const KeyAddress &address : addresses)
        {
          const std::string *bone = address_bone(clip, address);
          if (bone == nullptr)
          {
            continue;
          }
          AnimationBoneTrack &track = clip.tracks[static_cast<std::size_t>(address.track)];
          const int index = clip.key_index_at(*bone, address.channel, ref.time);
          if (index < 0)
          {
            continue;
          }
          const std::size_t slot = static_cast<std::size_t>(index);

          switch (address.channel)
          {
          case TrackChannel::Translation:
            if (slot < track.translations.size())
            {
              track.translations[slot].interpolation = mode;
              changed = true;
            }
            break;
          case TrackChannel::Rotation:
            if (slot < track.rotations.size())
            {
              track.rotations[slot].interpolation = mode;
              changed = true;
            }
            break;
          case TrackChannel::Scale:
            if (slot < track.scales.size())
            {
              track.scales[slot].interpolation = mode;
              changed = true;
            }
            break;
          }
        }
      }
      return changed;
    }

    // ---- Curve sampling ------------------------------------------------------

    struct CurveSample
    {
      float time = 0.0f;
      float value[3] = {0.0f, 0.0f, 0.0f};
    };

    struct CurveTrack
    {
      int row = -1;
      std::vector<CurveSample> samples;
      std::vector<CurveSample> keys;
    };

    template <typename KeyT, typename BlendFn, typename ToComponentsFn>
    void sample_key_list(const std::vector<KeyT> &keys, float viewStart, float viewEnd,
                         BlendFn blend, ToComponentsFn toComponents, CurveTrack &out)
    {
      if (keys.empty())
      {
        return;
      }

      for (const KeyT &key : keys)
      {
        CurveSample sample;
        sample.time = key.time;
        toComponents(key.value, sample.value);
        out.keys.push_back(sample);
      }

      // Outside the keyed range a clip holds the first/last value; draw that so
      // the curve does not appear to start in mid-air.
      if (viewStart < keys.front().time)
      {
        CurveSample sample = out.keys.front();
        sample.time = viewStart;
        out.samples.push_back(sample);
      }

      for (std::size_t i = 0; i + 1 < keys.size(); ++i)
      {
        const KeyT &key = keys[i];
        const KeyT &next = keys[i + 1];
        const float dt = next.time - key.time;
        for (int step = 0; step < kCurveSubdivisions; ++step)
        {
          const float u = static_cast<float>(step) / static_cast<float>(kCurveSubdivisions);
          // Sample through the easing so the drawn curve is the motion that
          // will actually play, not the straight line between two keys.
          const float eased = apply_easing(key.interpolation, u, key.ease);
          CurveSample sample;
          sample.time = key.time + dt * u;
          toComponents(blend(key.value, next.value, eased), sample.value);
          out.samples.push_back(sample);
        }
      }

      out.samples.push_back(out.keys.back());
      if (viewEnd > keys.back().time)
      {
        CurveSample sample = out.keys.back();
        sample.time = viewEnd;
        out.samples.push_back(sample);
      }
    }

    void sample_curve_track(const AnimationClipAsset &clip, const TimelineRow &row,
                            float viewStart, float viewEnd, CurveTrack &out)
    {
      out.samples.clear();
      out.keys.clear();

      const AnimationBoneTrack *track = clip.find_track(row.bone);
      if (track == nullptr)
      {
        return;
      }

      const auto vecComponents = [](const math::Vec3 &value, float (&components)[3])
      {
        components[0] = value.x;
        components[1] = value.y;
        components[2] = value.z;
      };
      const auto vecBlend = [](const math::Vec3 &a, const math::Vec3 &b, float t)
      { return math::lerp(a, b, t); };

      switch (row.channel)
      {
      case TrackChannel::Translation:
        sample_key_list(track->translations, viewStart, viewEnd, vecBlend, vecComponents, out);
        break;
      case TrackChannel::Scale:
        sample_key_list(track->scales, viewStart, viewEnd, vecBlend, vecComponents, out);
        break;
      case TrackChannel::Rotation:
        sample_key_list(
            track->rotations, viewStart, viewEnd,
            [](const math::Quat &a, const math::Quat &b, float t)
            { return math::slerp(a, b, t); },
            [](const math::Quat &value, float (&components)[3])
            {
              // Euler degrees are the only rotation representation an author can
              // read off a curve.
              const math::Vec3 euler = value.toEulerDegrees();
              components[0] = euler.x;
              components[1] = euler.y;
              components[2] = euler.z;
            },
            out);
        break;
      }
    }

    /// A single pending curve edit. Collected while drawing and applied after,
    /// because writing a key re-sorts the very vector being iterated.
    struct CurveEdit
    {
      bool valid = false;
      bool retime = false;
      int row = -1;
      int key = -1;
      int component = 0;
      float deltaTime = 0.0f;
      float deltaValue = 0.0f;
    };
  }

  // ---- State helpers ---------------------------------------------------------

  bool AnimationTimelineState::isCollapsed(const std::string &bone) const
  {
    return std::find(collapsedBones.begin(), collapsedBones.end(), bone) != collapsedBones.end();
  }

  void AnimationTimelineState::toggleCollapsed(const std::string &bone)
  {
    const auto it = std::find(collapsedBones.begin(), collapsedBones.end(), bone);
    if (it == collapsedBones.end())
    {
      collapsedBones.push_back(bone);
    }
    else
    {
      collapsedBones.erase(it);
    }
  }

  bool AnimationTimelineState::isSelected(const TimelineKeyRef &key) const
  {
    for (const TimelineKeyRef &selected : selection)
    {
      // Compared with the clip's own key tolerance: a key that was retimed to a
      // snapped frame must still match the selection entry that moved it.
      if (selected.row == key.row &&
          std::fabs(selected.time - key.time) <= AnimationClipAsset::kKeyEpsilon)
      {
        return true;
      }
    }
    return false;
  }

  // ---- Mapping ---------------------------------------------------------------

  float timeline_time_to_x(const AnimationTimelineState &state, float regionX, float regionWidth, float time)
  {
    const float span = state.viewEnd - state.viewStart;
    if (!(span > 1.0e-6f) || !(regionWidth > 0.0f))
    {
      return regionX;
    }
    return regionX + ((time - state.viewStart) / span) * regionWidth;
  }

  float timeline_x_to_time(const AnimationTimelineState &state, float regionX, float regionWidth, float x)
  {
    const float span = state.viewEnd - state.viewStart;
    if (!(span > 1.0e-6f) || !(regionWidth > 0.0f))
    {
      return state.viewStart;
    }
    return state.viewStart + ((x - regionX) / regionWidth) * span;
  }

  float timeline_snap(const AnimationTimelineState &state, const AnimationClipAsset &clip, float time)
  {
    if (!state.snapToFrames || !(clip.frameRate > 0.0f))
    {
      return time;
    }
    return std::round(time * clip.frameRate) / clip.frameRate;
  }

  // ---- Rows ------------------------------------------------------------------

  void build_timeline_rows(
      const AnimationClipAsset &clip,
      const Skeleton &skeleton,
      const std::string &filter,
      int selectedJoint,
      bool selectedOnly,
      std::vector<TimelineRow> &out)
  {
    // Rebuilt in place rather than cleared and re-pushed. The panel calls this
    // every frame and row indices are load-bearing (the selection stores them),
    // so it stays a pure recomputation with nothing to invalidate — but each
    // row's two std::strings keep their buffers, which turns a bone row from
    // four allocations into none. 200 tracks went from 2,000 allocations a
    // frame to zero.
    out.reserve(clip.tracks.size() * 4 + 2);
    std::size_t count = 0;
    const auto next_row = [&out, &count]() -> TimelineRow &
    {
      if (count == out.size())
      {
        out.emplace_back();
      }
      return out[count++];
    };

    TimelineRow &summary = next_row();
    summary.kind = TimelineRow::Kind::Summary;
    summary.label = "Summary";
    summary.bone.clear();
    summary.channel = TrackChannel::Translation;
    summary.joint = -1;
    summary.depth = 0;

    const std::string needle = lower_copy(filter);
    const TrackChannel kChannels[] = {TrackChannel::Translation, TrackChannel::Rotation, TrackChannel::Scale};

    for (const AnimationBoneTrack &track : clip.tracks)
    {
      if (track.empty())
      {
        continue;
      }

      // Bind by name: a clip authored against another rig still lists its
      // tracks, they simply resolve to -1 and draw greyed out.
      const int joint = skeleton.find(track.bone);
      // `selectedJoint < 0` means nothing is selected. Without that test the
      // comparison below matches every unbound track (joint == -1), so "only
      // the selected track" would list the entire retarget mismatch.
      if (selectedOnly && (selectedJoint < 0 || joint != selectedJoint))
      {
        continue;
      }
      if (!needle.empty() && lower_copy(track.bone).find(needle) == std::string::npos)
      {
        continue;
      }

      TimelineRow &boneRow = next_row();
      boneRow.kind = TimelineRow::Kind::BoneSummary;
      boneRow.label = track.bone;
      boneRow.bone = track.bone;
      boneRow.channel = TrackChannel::Translation;
      boneRow.joint = joint;
      boneRow.depth = 1;

      for (TrackChannel channel : kChannels)
      {
        if (!channel_has_keys(track, channel))
        {
          continue;
        }

        // Assigned from a literal, not a temporary std::string: the row's
        // existing buffer is long enough for any of the three names.
        const char *channelLabel = "Scale";
        switch (channel)
        {
        case TrackChannel::Translation:
          channelLabel = "Translation";
          break;
        case TrackChannel::Rotation:
          channelLabel = "Rotation";
          break;
        case TrackChannel::Scale:
          channelLabel = "Scale";
          break;
        }

        TimelineRow &channelRow = next_row();
        channelRow.kind = TimelineRow::Kind::Channel;
        channelRow.label = channelLabel;
        channelRow.bone = track.bone;
        channelRow.channel = channel;
        channelRow.joint = joint;
        channelRow.depth = 2;
      }
    }

    if (!clip.events.empty())
    {
      TimelineRow &eventRow = next_row();
      eventRow.kind = TimelineRow::Kind::Events;
      eventRow.label = "Events";
      eventRow.bone.clear();
      eventRow.channel = TrackChannel::Translation;
      eventRow.joint = -1;
      eventRow.depth = 0;
    }

    out.resize(count);
  }

  // ---- Dope sheet ------------------------------------------------------------

  AnimationTimelineResult draw_animation_timeline(
      const char *id,
      AnimationClipAsset &clip,
      const std::vector<TimelineRow> &rows,
      AnimationTimelineState &state,
      float availableHeight)
  {
    AnimationTimelineResult result;

    ImGuiIO &io = ImGui::GetIO();
    ImGui::PushID(id);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;
    const float fontSize = ImGui::GetFontSize();
    const float rowHeight = std::max(state.rowHeight, 12.0f);
    const float rulerHeight = ImGui::GetTextLineHeight() * 2.0f + 8.0f;
    const float width = std::max(avail.x, 64.0f);
    float height = availableHeight > 0.0f ? availableHeight : avail.y;
    height = std::max(height, rulerHeight + rowHeight * 2.0f);

    state.labelWidth = std::clamp(state.labelWidth, 60.0f, std::max(60.0f, width - 96.0f));

    TimelineGeometry geo;
    geo.origin = origin;
    geo.width = width;
    geo.labelWidth = state.labelWidth;
    geo.keyX = origin.x + state.labelWidth;
    // The rows child always shows its scrollbar, so the ruler and the key area
    // agree on where time zero sits no matter how many rows there are.
    geo.keyWidth = std::max(width - state.labelWidth - scrollbarWidth, 16.0f);
    geo.rowHeight = rowHeight;
    geo.rulerHeight = rulerHeight;

    clamp_view(state, clip);
    const float duration = std::max(clip.duration, 0.0f);
    // Keys are only drawn, hit-tested and box-selected inside the view, plus
    // the slack that makes a key on the edge still grabbable. Refreshed after
    // the view may have moved, so a zoom shows its new keys the same frame.
    const auto pixels_per_second = [&geo, &state]()
    {
      const float span = state.viewEnd - state.viewStart;
      return span > 1.0e-6f ? geo.keyWidth / span : 0.0f;
    };
    float windowStart = 0.0f;
    float windowEnd = 0.0f;
    const auto refresh_window = [&]()
    {
      const float pps = pixels_per_second();
      const float margin = pps > 0.0f ? (kKeyPickSlack + kKeyHalfExtent) / pps : 0.0f;
      windowStart = state.viewStart - margin;
      windowEnd = state.viewEnd + margin;
    };
    refresh_window();

    // Collapsed bones are filtered here rather than in build_timeline_rows: the
    // builder has no access to the view state, and row indices must stay stable
    // for the selection, so the full list is kept and only the drawing skips.
    std::vector<int> visible;
    visible.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
      const TimelineRow &row = rows[i];
      if (row.kind == TimelineRow::Kind::Channel && !row.bone.empty() && state.isCollapsed(row.bone))
      {
        continue;
      }
      visible.push_back(static_cast<int>(i));
    }
    const int visibleCount = static_cast<int>(visible.size());

    ImDrawList *headerDrawList = ImGui::GetWindowDrawList();

    // ---- Ruler ---------------------------------------------------------------

    headerDrawList->AddRectFilled(origin, ImVec2(geo.keyX, origin.y + rulerHeight), kColHeaderBg);
    {
      char readout[64];
      const int frame = clip.frameRate > 0.0f ? static_cast<int>(std::lround(state.time * clip.frameRate)) : 0;
      ImFormatString(readout, sizeof(readout), "%.3f s", static_cast<double>(state.time));
      headerDrawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 3.0f), kColText, readout);
      ImFormatString(readout, sizeof(readout), "frame %d   %s", frame, state.snapToFrames ? "snap" : "free");
      headerDrawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 5.0f + fontSize), kColTextDim, readout);
    }

    ImGui::SetCursorScreenPos(ImVec2(geo.keyX, origin.y));
    ImGui::InvisibleButton("##ruler", ImVec2(geo.keyWidth, rulerHeight));
    const ImGuiID rulerOwner = ImGui::GetItemID();
    const bool rulerHovered = ImGui::IsItemHovered();
    const bool rulerActive = ImGui::IsItemActive();

    // The wheel over the ruler zooms and pans the view (handled by hand below),
    // so it must not also scroll the panel this widget sits in.
    if (rulerHovered || rulerActive)
    {
      ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, rulerOwner, ImGuiInputFlags_LockThisFrame);
    }

    // Drawn once the child below has had its say on zoom and pan, so the ruler
    // and the key rows can never disagree about where a second sits.
    const auto draw_ruler = [&]()
    {
      const float pixelsPerSecond = pixels_per_second();
      const float tickStep = tick_step_for(pixelsPerSecond);
      const bool frameTicksVisible =
          clip.frameRate > 0.0f && (pixelsPerSecond / clip.frameRate) >= kMinFrameTickSpacing;

      const ImVec2 rulerMin(geo.keyX, origin.y);
      const ImVec2 rulerMax(geo.keyX + geo.keyWidth, origin.y + rulerHeight);
      headerDrawList->AddRectFilled(rulerMin, rulerMax, kColRulerBg);
      headerDrawList->PushClipRect(rulerMin, rulerMax, true);

      // Everything past the clip duration is not playable; shade it.
      {
        const float endX = timeline_time_to_x(state, geo.keyX, geo.keyWidth, duration);
        if (endX < rulerMax.x)
        {
          headerDrawList->AddRectFilled(ImVec2(std::max(endX, rulerMin.x), rulerMin.y), rulerMax, kColOutOfRange);
        }
        const float startX = timeline_time_to_x(state, geo.keyX, geo.keyWidth, 0.0f);
        if (startX > rulerMin.x)
        {
          headerDrawList->AddRectFilled(rulerMin, ImVec2(std::min(startX, rulerMax.x), rulerMax.y), kColOutOfRange);
        }
      }

      if (frameTicksVisible)
      {
        const float frameStep = 1.0f / clip.frameRate;
        const float firstFrame = std::floor(state.viewStart / frameStep) * frameStep;
        int guard = 0;
        for (float t = firstFrame; t <= state.viewEnd && guard < kMaxTicks; t += frameStep, ++guard)
        {
          const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, t);
          headerDrawList->AddLine(ImVec2(x, rulerMax.y - 4.0f), ImVec2(x, rulerMax.y), kColFrameTick, 1.0f);
        }
      }

      {
        const float firstTick = std::floor(state.viewStart / tickStep) * tickStep;
        char label[48];
        int guard = 0;
        for (float t = firstTick; t <= state.viewEnd && guard < kMaxTicks; t += tickStep, ++guard)
        {
          const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, t);
          headerDrawList->AddLine(ImVec2(x, rulerMin.y + rulerHeight * 0.45f), ImVec2(x, rulerMax.y), kColTick, 1.0f);

          // Steps below a second need decimals; the table's whole-second steps
          // are integral, so "%.0f" never lies about where the tick is.
          ImFormatString(label, sizeof(label), tickStep < 1.0f ? "%.2fs" : "%.0fs", static_cast<double>(t));
          headerDrawList->AddText(ImVec2(x + 3.0f, rulerMin.y + 2.0f), kColText, label);
          if (frameTicksVisible)
          {
            ImFormatString(label, sizeof(label), "f%d", static_cast<int>(std::lround(t * clip.frameRate)));
            headerDrawList->AddText(ImVec2(x + 3.0f, rulerMin.y + 3.0f + fontSize), kColTextDim, label);
          }
        }
      }

      // Play head handle.
      {
        const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, state.time);
        headerDrawList->AddLine(ImVec2(x, rulerMin.y), ImVec2(x, rulerMax.y), kColPlayhead, 1.5f);
        headerDrawList->AddTriangleFilled(
            ImVec2(x - 6.0f, rulerMax.y - 9.0f),
            ImVec2(x + 6.0f, rulerMax.y - 9.0f),
            ImVec2(x, rulerMax.y),
            kColPlayhead);
      }
      headerDrawList->PopClipRect();
      headerDrawList->AddLine(ImVec2(origin.x, rulerMax.y), ImVec2(origin.x + width, rulerMax.y), kColSeparator, 1.0f);
    };

    // Clicking or dragging anywhere in the ruler scrubs.
    if (rulerActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      state.draggingPlayhead = true;
      float scrubbed = timeline_x_to_time(state, geo.keyX, geo.keyWidth, io.MousePos.x);
      scrubbed = std::clamp(timeline_snap(state, clip, scrubbed), 0.0f, duration);
      if (scrubbed != state.time)
      {
        state.time = scrubbed;
        result.timeChanged = true;
      }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      state.draggingPlayhead = false;
    }

    // ---- Rows ----------------------------------------------------------------

    const float rowsHeight = std::max(height - rulerHeight, rowHeight);
    // Kept so the context menu below can be given the padding back: the zero
    // padding the rows child needs would otherwise leak into every popup and
    // submenu opened while it is pushed.
    const ImVec2 popupPadding = ImGui::GetStyle().WindowPadding;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rulerHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool childOpen = ImGui::BeginChild(
        "##rows",
        ImVec2(width, rowsHeight),
        ImGuiChildFlags_None,
        // The wheel belongs to this widget: ctrl zooms, shift pans, plain
        // scrolls the rows — all of it applied by hand below.
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (childOpen)
    {
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 viewMin = ImGui::GetWindowPos();
      const ImVec2 viewMax(viewMin.x + ImGui::GetWindowSize().x, viewMin.y + ImGui::GetWindowSize().y);
      const float contentHeight = std::max(static_cast<float>(visibleCount) * rowHeight, 1.0f);

      // One item owns every mouse button over the whole sheet; rows and keys are
      // hit-tested by hand from here on. It reaches past the last row so that
      // clicking the empty space below still box-selects and deselects.
      const float inputHeight = std::max(contentHeight, ImGui::GetWindowSize().y);
      ImGui::InvisibleButton(
          "##keys",
          ImVec2(std::max(width - scrollbarWidth, 1.0f), inputHeight),
          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
              ImGuiButtonFlags_MouseButtonMiddle);
      const ImGuiID keyOwner = ImGui::GetItemID();
      const bool keysHovered = ImGui::IsItemHovered();
      const bool keysActive = ImGui::IsItemActive();

      // Per-widget scratch that has to outlive a frame: a drag spans many
      // frames, and so does an open context menu.
      ImGuiStorage *storage = ImGui::GetStateStorage();
      const ImGuiID dragDirtyKey = ImGui::GetID("##dragdirty");
      const ImGuiID contextRowKey = ImGui::GetID("##ctxrow");
      const ImGuiID contextTimeKey = ImGui::GetID("##ctxtime");

      // Scope the keys we bind to this item so the 3D viewport keeps WASD and
      // the editor keeps its own shortcuts. Space is deliberately not bound —
      // the panel's transport buttons own play/pause.
      if (keysHovered || keysActive)
      {
        ImGui::SetKeyOwner(ImGuiKey_Delete, keyOwner, ImGuiInputFlags_LockThisFrame);
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, keyOwner, ImGuiInputFlags_LockThisFrame);
      }

      const ImVec2 mouse = io.MousePos;
      const bool shiftHeld = io.KeyShift;
      const bool toggleHeld = io.KeyCtrl || io.KeySuper;

      const auto visible_row_at = [&](float y) -> int
      {
        if (!(rowHeight > 0.0f))
        {
          return -1;
        }
        const int index = static_cast<int>(std::floor((y - contentOrigin.y) / rowHeight));
        if (index < 0 || index >= visibleCount)
        {
          return -1;
        }
        return index;
      };

      std::vector<float> scratch;
      // Survives the frame, so a Summary row that stands for tens of thousands
      // of keys is collapsed once per edit rather than once per frame.
      SummaryTimeCache &summaryCache = summary_cache_for(keyOwner);
      // Every membership test and every addition below goes through this, so
      // the selection stays a vector for everything that needs its order while
      // "is this key selected?" stops being a scan of it.
      SelectionLookup selectionIndex(state);
      const auto key_at = [&](int visibleIndex, float x, float &outTime) -> bool
      {
        if (visibleIndex < 0 || visibleIndex >= visibleCount)
        {
          return false;
        }
        const int rowIndex = visible[static_cast<std::size_t>(visibleIndex)];
        if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= rows.size())
        {
          return false;
        }

        row_key_times(clip, rows[static_cast<std::size_t>(rowIndex)], windowStart, windowEnd, summaryCache, scratch);
        float best = kKeyPickSlack;
        bool found = false;
        for (float time : scratch)
        {
          const float distance = std::fabs(timeline_time_to_x(state, geo.keyX, geo.keyWidth, time) - x);
          if (distance <= best)
          {
            best = distance;
            outTime = time;
            found = true;
          }
        }
        return found;
      };

      // ---- Press -------------------------------------------------------------

      if (keysHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      {
        const int visibleIndex = visible_row_at(mouse.y);
        const int rowIndex = visibleIndex >= 0 ? visible[static_cast<std::size_t>(visibleIndex)] : -1;

        if (mouse.x < geo.keyX)
        {
          if (rowIndex >= 0 && static_cast<std::size_t>(rowIndex) < rows.size())
          {
            const TimelineRow &row = rows[static_cast<std::size_t>(rowIndex)];
            const float indent = contentOrigin.x + 6.0f + static_cast<float>(row.depth) * 12.0f;
            if (row.kind == TimelineRow::Kind::BoneSummary && mouse.x <= indent + 14.0f)
            {
              state.toggleCollapsed(row.bone);
            }
            else
            {
              // Clicking a label selects the whole row: that is what feeds the
              // curve editor below.
              if (!shiftHeld && !toggleHeld)
              {
                selectionIndex.clear();
              }
              row_key_times(clip, row, windowStart, windowEnd, summaryCache, scratch);
              for (float time : scratch)
              {
                const TimelineKeyRef ref{rowIndex, time};
                if (!selectionIndex.contains(ref))
                {
                  selectionIndex.add(ref);
                }
              }
              result.selectionChanged = true;
            }
          }
        }
        else
        {
          float hitTime = 0.0f;
          if (visibleIndex >= 0 && rowIndex >= 0 && key_at(visibleIndex, mouse.x, hitTime))
          {
            const TimelineKeyRef ref{rowIndex, hitTime};
            if (toggleHeld)
            {
              const auto it = std::find_if(state.selection.begin(), state.selection.end(),
                                           [&ref](const TimelineKeyRef &candidate)
                                           {
                                             return candidate.row == ref.row &&
                                                    std::fabs(candidate.time - ref.time) <=
                                                        AnimationClipAsset::kKeyEpsilon;
                                           });
              if (it != state.selection.end())
              {
                state.selection.erase(it);
                selectionIndex.invalidate();
              }
              else
              {
                selectionIndex.add(ref);
              }
            }
            else if (shiftHeld)
            {
              // Extend across the row: every key between the nearest selected
              // key on this row and the one just clicked.
              float anchor = hitTime;
              bool hasAnchor = false;
              for (const TimelineKeyRef &selected : state.selection)
              {
                if (selected.row != rowIndex)
                {
                  continue;
                }
                if (!hasAnchor || std::fabs(selected.time - hitTime) < std::fabs(anchor - hitTime))
                {
                  anchor = selected.time;
                  hasAnchor = true;
                }
              }
              const float low = std::min(anchor, hitTime);
              const float high = std::max(anchor, hitTime);
              row_key_times(clip, rows[static_cast<std::size_t>(rowIndex)], windowStart, windowEnd, summaryCache, scratch);
              for (float time : scratch)
              {
                if (time < low - AnimationClipAsset::kKeyEpsilon ||
                    time > high + AnimationClipAsset::kKeyEpsilon)
                {
                  continue;
                }
                const TimelineKeyRef candidate{rowIndex, time};
                if (!selectionIndex.contains(candidate))
                {
                  selectionIndex.add(candidate);
                }
              }
            }
            else if (!selectionIndex.contains(ref))
            {
              selectionIndex.clear();
              selectionIndex.add(ref);
            }

            result.selectionChanged = true;
            state.draggingKeys = true;
            state.dragOriginTime = timeline_x_to_time(state, geo.keyX, geo.keyWidth, mouse.x);
            state.dragLastDelta = 0.0f;
          }
          else
          {
            if (!shiftHeld && !toggleHeld && !state.selection.empty())
            {
              selectionIndex.clear();
              result.selectionChanged = true;
            }
            state.boxSelecting = true;
            state.boxSelectStartX = mouse.x;
            state.boxSelectStartY = mouse.y;
          }
        }
      }

      // ---- Key drag ----------------------------------------------------------

      if (state.draggingKeys)
      {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || state.selection.empty())
        {
          state.draggingKeys = false;
          state.dragLastDelta = 0.0f;
          // One report for the whole drag: the panel turns each one into an
          // undo entry holding a full copy of the clip.
          if (storage->GetInt(dragDirtyKey, 0) != 0)
          {
            storage->SetInt(dragDirtyKey, 0);
            result.clipChanged = true;
            result.changeLabel = "move keys";
          }
        }
        else
        {
          float delta = timeline_x_to_time(state, geo.keyX, geo.keyWidth, mouse.x) - state.dragOriginTime;
          if (state.snapToFrames && clip.frameRate > 0.0f)
          {
            // Snap the offset, not each key: keys that were authored off the
            // frame grid keep their relative spacing.
            delta = std::round(delta * clip.frameRate) / clip.frameRate;
          }

          float lowest = state.selection.front().time;
          float highest = state.selection.front().time;
          for (const TimelineKeyRef &ref : state.selection)
          {
            lowest = std::min(lowest, ref.time);
            highest = std::max(highest, ref.time);
          }
          // `delta` is measured from where the drag started, so it has to be
          // bounded by where the selection started too. Every key has moved by
          // exactly `dragLastDelta` so far, which recovers that origin.
          // Bounding against the *current* extent instead would pin the offset
          // at the limit as soon as the pointer went past the end of the clip,
          // and then hurl every key back to its starting time the moment the
          // pointer came back inside.
          const float originLowest = lowest - state.dragLastDelta;
          const float originHighest = highest - state.dragLastDelta;
          // Keys authored past the duration must still be draggable, so the
          // ceiling never sits below the selection itself.
          const float upperBound =
              std::max(duration > 0.0f ? duration : originHighest, originHighest);
          delta = std::max(delta, -originLowest);
          delta = std::min(delta, upperBound - originHighest);

          const float step = delta - state.dragLastDelta;
          if (std::fabs(step) > 1.0e-6f)
          {
            // move_selection retimes the selection entries whether or not the
            // clip changed, so the table has to go either way.
            const bool moved = move_selection(clip, rows, state, summaryCache, step);
            selectionIndex.invalidate();
            if (moved)
            {
              storage->SetInt(dragDirtyKey, 1);
              result.selectionChanged = true;
            }
            state.dragLastDelta = delta;
          }
        }
      }

      // ---- Delete ------------------------------------------------------------

      if ((keysHovered || keysActive) && !state.selection.empty() &&
          ImGui::IsKeyPressed(ImGuiKey_Delete, ImGuiInputFlags_None, keyOwner))
      {
        if (delete_selection(clip, rows, state, summaryCache))
        {
          result.clipChanged = true;
          result.changeLabel = "delete keys";
        }
        selectionIndex.clear();
        result.selectionChanged = true;
      }

      // ---- View --------------------------------------------------------------

      if ((keysHovered || rulerHovered) && io.MouseWheel != 0.0f)
      {
        const float span = std::max(state.viewEnd - state.viewStart, kMinSpan);
        if (toggleHeld)
        {
          const float pivot = timeline_x_to_time(state, geo.keyX, geo.keyWidth, mouse.x);
          const float maxSpan = std::max(duration * 4.0f, 10.0f);
          const float scaled = std::clamp(span * std::pow(0.88f, io.MouseWheel), kMinSpan, maxSpan);
          state.viewStart = pivot - (pivot - state.viewStart) * (scaled / span);
          state.viewEnd = state.viewStart + scaled;
        }
        else if (shiftHeld)
        {
          const float shift = -io.MouseWheel * span * 0.12f;
          state.viewStart += shift;
          state.viewEnd += shift;
        }
        else
        {
          ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * rowHeight * 3.0f);
        }
        clamp_view(state, clip);
      }

      if (keysActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle) && io.MouseDelta.x != 0.0f &&
          geo.keyWidth > 0.0f)
      {
        const float span = state.viewEnd - state.viewStart;
        const float shift = -io.MouseDelta.x / geo.keyWidth * span;
        state.viewStart += shift;
        state.viewEnd += shift;
        clamp_view(state, clip);
      }

      // ---- Draw --------------------------------------------------------------

      refresh_window();

      const ImVec2 keyClipMin(geo.keyX, viewMin.y);
      const ImVec2 keyClipMax(geo.keyX + geo.keyWidth, viewMax.y);
      const ImVec2 labelClipMin(viewMin.x, viewMin.y);
      const ImVec2 labelClipMax(geo.keyX, viewMax.y);

      const auto draw_row = [&](int visibleIndex)
      {
        if (visibleIndex < 0 || visibleIndex >= visibleCount)
        {
          return;
        }
        const int rowIndex = visible[static_cast<std::size_t>(visibleIndex)];
        if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= rows.size())
        {
          return;
        }
        const TimelineRow &row = rows[static_cast<std::size_t>(rowIndex)];
        const float top = contentOrigin.y + static_cast<float>(visibleIndex) * rowHeight;
        const float centre = top + rowHeight * 0.5f;
        if (top > viewMax.y || top + rowHeight < viewMin.y)
        {
          return;
        }

        const ImVec2 rowMin(contentOrigin.x, top);
        const ImVec2 rowMax(geo.keyX + geo.keyWidth, top + rowHeight);
        drawList->AddRectFilled(rowMin, rowMax, (visibleIndex & 1) != 0 ? kColRowOdd : kColRowEven);
        if (row.kind == TimelineRow::Kind::Summary)
        {
          drawList->AddRectFilled(rowMin, rowMax, kColSummaryRow);
        }
        else if (row.kind == TimelineRow::Kind::Events)
        {
          drawList->AddRectFilled(rowMin, rowMax, kColEventRow);
        }

        // Label column.
        drawList->PushClipRect(labelClipMin, labelClipMax, true);
        float textX = contentOrigin.x + 6.0f + static_cast<float>(row.depth) * 12.0f;
        if (row.kind == TimelineRow::Kind::BoneSummary)
        {
          const bool collapsed = state.isCollapsed(row.bone);
          const float ax = textX + 2.0f;
          if (collapsed)
          {
            drawList->AddTriangleFilled(
                ImVec2(ax, centre - 4.5f), ImVec2(ax + 7.0f, centre), ImVec2(ax, centre + 4.5f), kColTextDim);
          }
          else
          {
            drawList->AddTriangleFilled(
                ImVec2(ax - 1.0f, centre - 2.5f), ImVec2(ax + 8.0f, centre - 2.5f),
                ImVec2(ax + 3.5f, centre + 4.5f), kColTextDim);
          }
          textX += 14.0f;
        }

        const bool unbound = row.joint < 0 &&
                             (row.kind == TimelineRow::Kind::BoneSummary || row.kind == TimelineRow::Kind::Channel);
        const ImU32 labelColour = unbound ? kColLabelUnbound
                                          : (row.kind == TimelineRow::Kind::Channel ? kColTextDim : kColText);
        drawList->AddText(ImVec2(textX, top + (rowHeight - fontSize) * 0.5f), labelColour, row.label.c_str());
        drawList->PopClipRect();

        // Key column.
        drawList->PushClipRect(keyClipMin, keyClipMax, true);
        row_key_times(clip, row, windowStart, windowEnd, summaryCache, scratch);
        const ImU32 baseColour = row.kind == TimelineRow::Kind::Events
                                     ? kColKeyEvent
                                     : (unbound ? kColKeyUnbound : kColKey);
        for (float time : scratch)
        {
          const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, time);
          if (x < keyClipMin.x - kKeyHalfExtent || x > keyClipMax.x + kKeyHalfExtent)
          {
            continue;
          }
          const bool isSelected = selectionIndex.contains(TimelineKeyRef{rowIndex, time});
          draw_key_diamond(drawList, x, centre, isSelected ? kColKeySelected : baseColour,
                           isSelected ? kColKeySelectedOutline : kColKeyOutline);
        }
        drawList->PopClipRect();
      };

      // Grid lines behind the rows, on the ruler's major ticks.
      drawList->PushClipRect(keyClipMin, keyClipMax, true);
      {
        const float tickStep = tick_step_for(pixels_per_second());
        const float firstTick = std::floor(state.viewStart / tickStep) * tickStep;
        int guard = 0;
        for (float t = firstTick; t <= state.viewEnd && guard < kMaxTicks; t += tickStep, ++guard)
        {
          const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, t);
          drawList->AddLine(ImVec2(x, viewMin.y), ImVec2(x, viewMax.y), kColGrid, 1.0f);
        }
      }
      const float endX = timeline_time_to_x(state, geo.keyX, geo.keyWidth, duration);
      if (endX < keyClipMax.x)
      {
        drawList->AddRectFilled(ImVec2(std::max(endX, keyClipMin.x), viewMin.y), keyClipMax, kColOutOfRange);
      }
      drawList->PopClipRect();

      if (visibleCount > kClipperThreshold)
      {
        // The invisible button already reserved the full height; rewind the
        // cursor so the clipper measures from the same origin.
        ImGui::SetCursorScreenPos(contentOrigin);
        ImGuiListClipper clipper;
        clipper.Begin(visibleCount, rowHeight);
        while (clipper.Step())
        {
          for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
          {
            draw_row(i);
          }
        }
      }
      else
      {
        for (int i = 0; i < visibleCount; ++i)
        {
          draw_row(i);
        }
      }
      ImGui::SetCursorScreenPos(ImVec2(contentOrigin.x, contentOrigin.y + contentHeight));

      drawList->AddLine(ImVec2(geo.keyX, viewMin.y), ImVec2(geo.keyX, viewMax.y), kColSeparator, 1.0f);

      // Play head through the rows.
      {
        drawList->PushClipRect(keyClipMin, keyClipMax, true);
        const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, state.time);
        drawList->AddLine(ImVec2(x, viewMin.y), ImVec2(x, viewMax.y), kColPlayhead, 1.5f);
        drawList->PopClipRect();
      }

      // ---- Box select --------------------------------------------------------

      if (state.boxSelecting)
      {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
          const ImVec2 boxMin(std::min(state.boxSelectStartX, mouse.x), std::min(state.boxSelectStartY, mouse.y));
          const ImVec2 boxMax(std::max(state.boxSelectStartX, mouse.x), std::max(state.boxSelectStartY, mouse.y));
          drawList->AddRectFilled(boxMin, boxMax, kColBoxSelectFill);
          drawList->AddRect(boxMin, boxMax, kColBoxSelectLine);
        }
        else
        {
          const float minX = std::min(state.boxSelectStartX, mouse.x);
          const float maxX = std::max(state.boxSelectStartX, mouse.x);
          const float minY = std::min(state.boxSelectStartY, mouse.y);
          const float maxY = std::max(state.boxSelectStartY, mouse.y);
          state.boxSelecting = false;

          // A press with no travel is a click that cleared the selection, not a
          // zero-sized marquee.
          if (maxX - minX > 3.0f || maxY - minY > 3.0f)
          {
            bool changed = false;
            for (int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex)
            {
              const float top = contentOrigin.y + static_cast<float>(visibleIndex) * rowHeight;
              if (top + rowHeight < minY || top > maxY)
              {
                continue;
              }
              const int rowIndex = visible[static_cast<std::size_t>(visibleIndex)];
              if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= rows.size())
              {
                continue;
              }
              row_key_times(clip, rows[static_cast<std::size_t>(rowIndex)], windowStart, windowEnd, summaryCache, scratch);
              for (float time : scratch)
              {
                const float x = timeline_time_to_x(state, geo.keyX, geo.keyWidth, time);
                if (x < minX - kKeyHalfExtent || x > maxX + kKeyHalfExtent)
                {
                  continue;
                }
                const TimelineKeyRef ref{rowIndex, time};
                if (!selectionIndex.contains(ref))
                {
                  selectionIndex.add(ref);
                  changed = true;
                }
              }
            }
            if (changed)
            {
              result.selectionChanged = true;
            }
          }
        }
      }

      // ---- Context menu ------------------------------------------------------

      // The popup outlives the frame the right-click happened on, so where it
      // was opened has to be remembered.
      if (keysHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      {
        const int visibleIndex = visible_row_at(mouse.y);
        storage->SetInt(contextRowKey, visibleIndex >= 0 ? visible[static_cast<std::size_t>(visibleIndex)] : -1);
        storage->SetFloat(
            contextTimeKey,
            std::max(timeline_snap(state, clip,
                                   timeline_x_to_time(state, geo.keyX, geo.keyWidth, mouse.x)),
                     0.0f));
      }

      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, popupPadding);
      if (ImGui::BeginPopupContextItem("##timeline_context"))
      {
        const int contextRow = storage->GetInt(contextRowKey, -1);
        const float contextTime = storage->GetFloat(contextTimeKey, 0.0f);
        const bool hasSelection = !state.selection.empty();

        ImGui::TextDisabled("%.3f s", static_cast<double>(contextTime));
        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_PLUS "  Insert key here"))
        {
          // Only the panel knows the pose to key, so this one goes back out.
          result.requestInsertKey = true;
          result.contextRow = contextRow;
          result.contextTime = contextTime;
        }
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete selected", nullptr, false, hasSelection))
        {
          if (delete_selection(clip, rows, state, summaryCache))
          {
            result.clipChanged = true;
            result.changeLabel = "delete keys";
          }
          selectionIndex.clear();
          result.selectionChanged = true;
        }
        if (ImGui::BeginMenu("Interpolation", hasSelection))
        {
          for (int mode = static_cast<int>(Interpolation::Step); mode <= static_cast<int>(Interpolation::Bezier);
               ++mode)
          {
            const Interpolation interpolation = static_cast<Interpolation>(mode);
            if (ImGui::MenuItem(interpolation_display_name(interpolation)))
            {
              if (apply_interpolation(clip, rows, state, summaryCache, interpolation))
              {
                result.clipChanged = true;
                result.changeLabel = "set interpolation";
              }
            }
          }
          ImGui::EndMenu();
        }
        ImGui::EndPopup();
      }
      ImGui::PopStyleVar();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    draw_ruler();

    // Keep the cursor below the widget so the panel can keep laying out.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
    return result;
  }

  // ---- Curve editor ----------------------------------------------------------

  void draw_animation_curves(
      const char *id,
      AnimationClipAsset &clip,
      const std::vector<TimelineRow> &rows,
      AnimationTimelineState &state,
      float height,
      AnimationTimelineResult &result)
  {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::PushID(id);

    // A point drag spans frames; like the dope sheet, it reports once when the
    // drag ends so the panel records a single undo entry for the gesture.
    ImGuiStorage *storage = ImGui::GetStateStorage();
    const ImGuiID pendingEditKey = ImGui::GetID("##curvedirty");
    bool anyHandleActive = false;
    const auto flush_pending_edit = [&]()
    {
      if (anyHandleActive)
      {
        return;
      }
      const int pending = storage->GetInt(pendingEditKey, 0);
      if (pending == 0)
      {
        return;
      }
      storage->SetInt(pendingEditKey, 0);
      result.clipChanged = true;
      result.changeLabel = pending == 2 ? "move keys" : "edit key value";
    };

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    const float regionHeight = std::max(height, 48.0f);
    const float fontSize = ImGui::GetFontSize();
    const float gutter = std::clamp(state.labelWidth, 40.0f, std::max(40.0f, width - 64.0f));
    const float plotX = origin.x + gutter;
    const float plotWidth = std::max(width - gutter, 16.0f);
    const ImVec2 regionMin = origin;
    const ImVec2 regionMax(origin.x + width, origin.y + regionHeight);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(regionMin, regionMax, kColCurveBg);
    drawList->AddRect(regionMin, regionMax, kColBorder);

    // Reserves the region and swallows scroll-wheel/drag over it. It has to
    // allow overlap: it covers the whole plot and is submitted before the key
    // handles, and ImGui gives an overlap to the item submitted first unless
    // that one opted out. Without this the handles never receive hover, so no
    // curve key can be grabbed and the editor is read-only.
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##curvearea", ImVec2(width, regionHeight));

    // Rows to plot: the channels the selection touches, with a bone summary
    // standing in for its own channels.
    std::vector<int> curveRows;
    const auto push_row = [&curveRows](int rowIndex)
    {
      if (rowIndex < 0 || static_cast<int>(curveRows.size()) >= kMaxCurveRows)
      {
        return;
      }
      if (std::find(curveRows.begin(), curveRows.end(), rowIndex) == curveRows.end())
      {
        curveRows.push_back(rowIndex);
      }
    };

    for (const TimelineKeyRef &ref : state.selection)
    {
      if (ref.row < 0 || static_cast<std::size_t>(ref.row) >= rows.size())
      {
        continue;
      }
      const TimelineRow &row = rows[static_cast<std::size_t>(ref.row)];
      if (row.kind == TimelineRow::Kind::Channel)
      {
        push_row(ref.row);
      }
      else if (row.kind == TimelineRow::Kind::BoneSummary)
      {
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
          if (rows[i].kind == TimelineRow::Kind::Channel && rows[i].bone == row.bone)
          {
            push_row(static_cast<int>(i));
          }
        }
      }
    }

    if (curveRows.empty())
    {
      drawList->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), kColTextDim,
                        "Select a channel row or its keys to edit curves.");
      flush_pending_edit();
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + regionHeight));
      ImGui::Dummy(ImVec2(0.0f, 0.0f));
      ImGui::PopID();
      return;
    }

    std::vector<CurveTrack> tracks;
    tracks.reserve(curveRows.size());
    float valueMin = 0.0f;
    float valueMax = 0.0f;
    bool haveRange = false;

    for (int rowIndex : curveRows)
    {
      if (rowIndex < 0 || static_cast<std::size_t>(rowIndex) >= rows.size())
      {
        continue;
      }
      CurveTrack track;
      track.row = rowIndex;
      sample_curve_track(clip, rows[static_cast<std::size_t>(rowIndex)], state.viewStart, state.viewEnd, track);
      if (track.samples.empty())
      {
        continue;
      }

      for (const CurveSample &sample : track.samples)
      {
        for (int component = 0; component < 3; ++component)
        {
          const float value = sample.value[component];
          if (!haveRange)
          {
            valueMin = value;
            valueMax = value;
            haveRange = true;
          }
          else
          {
            valueMin = std::min(valueMin, value);
            valueMax = std::max(valueMax, value);
          }
        }
      }
      tracks.push_back(std::move(track));
    }

    if (!haveRange || tracks.empty())
    {
      drawList->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), kColTextDim, "No keys on the selected rows.");
      flush_pending_edit();
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + regionHeight));
      ImGui::Dummy(ImVec2(0.0f, 0.0f));
      ImGui::PopID();
      return;
    }

    // Auto-fit with a margin so a flat curve is not glued to an edge.
    float valueSpan = valueMax - valueMin;
    if (!(valueSpan > 1.0e-5f))
    {
      valueMin -= 0.5f;
      valueMax += 0.5f;
      valueSpan = valueMax - valueMin;
    }
    else
    {
      const float pad = valueSpan * 0.08f;
      valueMin -= pad;
      valueMax += pad;
      valueSpan = valueMax - valueMin;
    }

    const float plotTop = origin.y + 4.0f;
    const float plotBottom = origin.y + regionHeight - 4.0f;
    const float plotHeight = std::max(plotBottom - plotTop, 1.0f);

    const auto value_to_y = [&](float value)
    { return plotBottom - ((value - valueMin) / valueSpan) * plotHeight; };
    const auto time_to_x = [&](float time)
    { return timeline_time_to_x(state, plotX, plotWidth, time); };

    // Value axis.
    {
      char label[32];
      const float rowsY[3] = {plotTop, (plotTop + plotBottom) * 0.5f, plotBottom};
      const float values[3] = {valueMax, (valueMin + valueMax) * 0.5f, valueMin};
      for (int i = 0; i < 3; ++i)
      {
        drawList->AddLine(ImVec2(plotX, rowsY[i]), ImVec2(plotX + plotWidth, rowsY[i]), kColAxis, 1.0f);
        ImFormatString(label, sizeof(label), "%.3g", static_cast<double>(values[i]));
        drawList->AddText(ImVec2(origin.x + 6.0f, rowsY[i] - fontSize * 0.5f), kColTextDim, label);
      }
      if (valueMin < 0.0f && valueMax > 0.0f)
      {
        const float zeroY = value_to_y(0.0f);
        drawList->AddLine(ImVec2(plotX, zeroY), ImVec2(plotX + plotWidth, zeroY), kColSeparator, 1.0f);
      }
    }

    const ImVec2 plotMin(plotX, origin.y + 1.0f);
    const ImVec2 plotMax(plotX + plotWidth, origin.y + regionHeight - 1.0f);
    drawList->PushClipRect(plotMin, plotMax, true);
    drawList->AddLine(ImVec2(time_to_x(state.time), plotMin.y), ImVec2(time_to_x(state.time), plotMax.y),
                      kColPlayhead, 1.5f);

    std::vector<ImVec2> points;
    for (const CurveTrack &track : tracks)
    {
      for (int component = 0; component < 3; ++component)
      {
        points.clear();
        points.reserve(track.samples.size());
        for (const CurveSample &sample : track.samples)
        {
          points.push_back(ImVec2(time_to_x(sample.time), value_to_y(sample.value[component])));
        }
        if (points.size() >= 2)
        {
          drawList->AddPolyline(points.data(), static_cast<int>(points.size()), component_colour(component),
                                ImDrawFlags_None, 1.6f);
        }
      }
    }
    drawList->PopClipRect();

    // Legend, in the gutter.
    {
      float legendY = plotTop;
      for (const CurveTrack &track : tracks)
      {
        if (track.row < 0 || static_cast<std::size_t>(track.row) >= rows.size())
        {
          continue;
        }
        if (legendY + fontSize > plotBottom)
        {
          break;
        }
        const TimelineRow &row = rows[static_cast<std::size_t>(track.row)];
        char label[160];
        ImFormatString(label, sizeof(label), "%s / %s", row.bone.c_str(), row.label.c_str());
        drawList->AddText(ImVec2(origin.x + 6.0f, legendY), kColTextDim, label);
        legendY += fontSize + 2.0f;
      }
    }

    // ---- Draggable key points -------------------------------------------------

    CurveEdit edit;
    int handleCount = 0;
    const float secondsPerPixel =
        plotWidth > 0.0f ? (state.viewEnd - state.viewStart) / plotWidth : 0.0f;
    const float valuePerPixel = valueSpan / plotHeight;

    for (const CurveTrack &track : tracks)
    {
      ImGui::PushID(track.row);
      for (int component = 0; component < 3; ++component)
      {
        ImGui::PushID(component);
        for (std::size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex)
        {
          const CurveSample &key = track.keys[keyIndex];
          const float px = time_to_x(key.time);
          const float py = value_to_y(key.value[component]);
          if (px < plotMin.x - 6.0f || px > plotMax.x + 6.0f || handleCount >= kMaxCurveHandles)
          {
            continue;
          }
          ++handleCount;

          drawList->AddCircleFilled(ImVec2(px, py), 3.0f, component_colour(component));
          drawList->AddCircle(ImVec2(px, py), 3.5f, kColKeyOutline, 0, 1.0f);

          ImGui::PushID(static_cast<int>(keyIndex));
          ImGui::SetCursorScreenPos(ImVec2(px - 5.0f, py - 5.0f));
          ImGui::InvisibleButton("##pt", ImVec2(10.0f, 10.0f));
          if (ImGui::IsItemActive())
          {
            anyHandleActive = true;
          }
          if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !edit.valid)
          {
            const ImVec2 delta = io.MouseDelta;
            // Which axis the gesture is on is decided by the whole drag, not by
            // this frame alone: a diagonal drag would otherwise alternate
            // between retiming and editing the value from frame to frame.
            const ImVec2 travel = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (std::fabs(travel.x) > std::fabs(travel.y))
            {
              edit.retime = true;
              edit.deltaTime = delta.x * secondsPerPixel;
            }
            else
            {
              edit.retime = false;
              edit.deltaValue = -delta.y * valuePerPixel;
            }
            edit.valid = std::fabs(delta.x) > 0.0f || std::fabs(delta.y) > 0.0f;
            edit.row = track.row;
            edit.key = static_cast<int>(keyIndex);
            edit.component = component;
          }
          ImGui::PopID();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }

    // ---- Apply ---------------------------------------------------------------

    if (edit.valid && edit.row >= 0 && static_cast<std::size_t>(edit.row) < rows.size())
    {
      const TimelineRow &row = rows[static_cast<std::size_t>(edit.row)];
      AnimationBoneTrack *track = clip.find_track(row.bone);
      if (track != nullptr && edit.key >= 0)
      {
        const std::size_t slot = static_cast<std::size_t>(edit.key);
        const float durationCeiling = clip.duration > 0.0f ? clip.duration : state.viewEnd;

        if (row.channel == TrackChannel::Rotation)
        {
          if (slot < track->rotations.size())
          {
            const AnimQuatKey key = track->rotations[slot];
            if (edit.retime)
            {
              const float target = std::clamp(timeline_snap(state, clip, key.time + edit.deltaTime),
                                              0.0f, std::max(durationCeiling, key.time));
              if (clip.move_key(row.bone, TrackChannel::Rotation, key.time, target) >= 0.0f)
              {
                storage->SetInt(pendingEditKey, 2);
              }
            }
            else
            {
              math::Vec3 euler = key.value.toEulerDegrees();
              set_vec3_component(euler, edit.component,
                                 vec3_component(euler, edit.component) + edit.deltaValue);
              clip.set_rotation_key(row.bone, key.time, math::Quat::fromEulerDegrees(euler),
                                    key.interpolation, key.ease);
              storage->SetInt(pendingEditKey, 1);
            }
          }
        }
        else
        {
          std::vector<AnimVec3Key> &keys =
              row.channel == TrackChannel::Scale ? track->scales : track->translations;
          if (slot < keys.size())
          {
            const AnimVec3Key key = keys[slot];
            if (edit.retime)
            {
              const float target = std::clamp(timeline_snap(state, clip, key.time + edit.deltaTime),
                                              0.0f, std::max(durationCeiling, key.time));
              if (clip.move_key(row.bone, row.channel, key.time, target) >= 0.0f)
              {
                storage->SetInt(pendingEditKey, 2);
              }
            }
            else
            {
              math::Vec3 value = key.value;
              set_vec3_component(value, edit.component,
                                 vec3_component(value, edit.component) + edit.deltaValue);
              if (row.channel == TrackChannel::Scale)
              {
                clip.set_scale_key(row.bone, key.time, value, key.interpolation, key.ease);
              }
              else
              {
                clip.set_translation_key(row.bone, key.time, value, key.interpolation, key.ease);
              }
              storage->SetInt(pendingEditKey, 1);
            }
          }
        }
      }
    }

    flush_pending_edit();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + regionHeight));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
  }
}
