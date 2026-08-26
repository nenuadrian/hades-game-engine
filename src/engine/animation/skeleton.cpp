#include "skeleton.hpp"

#include <cmath>

#include "../assets/model_asset.hpp"

namespace hades
{
  Skeleton Skeleton::from_model(const ModelAsset &asset)
  {
    Skeleton skeleton;

    const std::size_t count = asset.nodes.size();
    skeleton.joints_.resize(count);
    skeleton.children_.resize(count);

    for (std::size_t i = 0; i < count; ++i)
    {
      const ModelNode &node = asset.nodes[i];
      SkeletonJoint &joint = skeleton.joints_[i];
      joint.name = node.name;
      joint.parent = node.parent;
      if (!math::decomposeTRS(node.localTransform, joint.restTranslation, joint.restRotation, joint.restScale))
      {
        // decomposeTRS reports the failure but still writes the degenerate
        // scale it measured -- only the rotation is reset. A zero (or NaN)
        // component would survive into every pose built from this skeleton
        // and collapse this joint AND its whole subtree to a point the first
        // time buildModelMatrix ran on it, because a zero basis column wipes
        // out the children's offsets too. Neutralise only the axes that
        // carry no information; the ones that do are still authored data.
        // (The `!(x > eps)` form is deliberate: it also catches NaN.)
        if (!(std::fabs(joint.restScale.x) > 1e-8f))
        {
          joint.restScale.x = 1.0f;
        }
        if (!(std::fabs(joint.restScale.y) > 1e-8f))
        {
          joint.restScale.y = 1.0f;
        }
        if (!(std::fabs(joint.restScale.z) > 1e-8f))
        {
          joint.restScale.z = 1.0f;
        }

        // No residual is recovered for a degenerate local: dividing by the
        // basis the repair above just invented would fold the collapse we
        // are removing straight back into every pose.
        continue;
      }

      // decomposeTRS only reports a *degenerate* matrix, not a lossy one: a
      // sheared but invertible linear part (a COLLADA `<matrix>` node, or
      // `<scale>` composed before `<rotate>`) decomposes "successfully" and
      // silently loses the shear. Keep it as the residual between the
      // rebuilt TRS and what the file actually said, so local_to_global can
      // reproduce the node's own transform exactly -- and therefore the bind
      // pose the offset matrices were built against.
      //
      // Inverting T*R*S factor by factor rather than through
      // math::Mat4::inverse(). The cofactor inverse gives up and returns
      // IDENTITY when |det| < 1e-12, and det here is exactly sx*sy*sz, so a
      // node scaled uniformly by 1e-4 or less -- a model authored in
      // micrometres, or any rig carrying its unit conversion on a node --
      // would come back with `correction == node.localTransform`, which
      // local_to_global would then apply a SECOND time on top of the pose,
      // destroying that node and its whole subtree. There is no such cliff
      // here: decomposeTRS already refused every scale component below 1e-8,
      // so each reciprocal is finite and the three factors invert exactly.
      // buildModelMatrix composes T * R * S, so the inverse is S' * R' * T'.
      // (`restRotation` comes from Quat::fromMat4, which normalises, so the
      // quaternion inverse really is the rotation matrix's inverse.)
      const math::Mat4 inverseRebuilt =
          math::Mat4::scaleMatrix({1.0f / joint.restScale.x, 1.0f / joint.restScale.y, 1.0f / joint.restScale.z}) *
          joint.restRotation.inverse().toMat4() *
          math::Mat4::translate({-joint.restTranslation.x, -joint.restTranslation.y, -joint.restTranslation.z});
      const math::Mat4 correction = inverseRebuilt * node.localTransform;

      float deviation = 0.0f;
      const math::Mat4 identity = math::Mat4::identity();
      for (int column = 0; column < 4; ++column)
      {
        for (int row = 0; row < 4; ++row)
        {
          deviation = std::fmax(deviation, std::fabs(correction.m[column][row] - identity.m[column][row]));
        }
      }

      // A well-formed rig rebuilds to itself, so the residual is identity to
      // within float noise. Flagging that case keeps the per-frame cost of
      // this fix at zero for every FBX/glTF skeleton.
      //
      // Trusted only when it actually reconstructs what the file said:
      // `rebuilt * correction` has to come back to node.localTransform. An
      // ill-conditioned linear part (a rig mixing 1e4 and 1e-4 scale on one
      // node) can lose enough precision in the round trip that the residual
      // is noise, and applying noise unconditionally to every pose would be
      // worse than the lossy decomposition this is repairing.
      if (deviation > 1e-5f)
      {
        const math::Mat4 rebuilt =
            math::buildModelMatrix(joint.restTranslation, joint.restRotation, joint.restScale);
        const math::Mat4 reconstructed = rebuilt * correction;

        float residual = 0.0f;
        float magnitude = 1.0f;
        for (int column = 0; column < 4; ++column)
        {
          for (int row = 0; row < 4; ++row)
          {
            residual = std::fmax(residual,
                                 std::fabs(reconstructed.m[column][row] - node.localTransform.m[column][row]));
            magnitude = std::fmax(magnitude, std::fabs(node.localTransform.m[column][row]));
          }
        }

        if (residual <= 1e-4f * magnitude)
        {
          joint.restCorrection = correction;
          joint.hasCorrection = true;
        }
      }
    }

    // A node may own several palette entries (assimp splits a bone per mesh),
    // so flag from the palette side rather than searching per joint.
    for (const ModelBone &bone : asset.bones)
    {
      if (bone.nodeIndex >= 0 && static_cast<std::size_t>(bone.nodeIndex) < count)
      {
        skeleton.joints_[static_cast<std::size_t>(bone.nodeIndex)].skinned = true;
      }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
      const int parent = skeleton.joints_[i].parent;
      // `parent < i` is the same test local_to_global applies, and the two
      // must agree or the hierarchy the editor walks is not the hierarchy the
      // pose is composed from. It rejects a self reference, an out-of-range
      // parent and a forward reference in one go: nodes are flattened
      // parent-before-child, so anything else is a malformed asset and is
      // made a root here exactly as local_to_global makes it one. It also
      // guarantees every edge points from a lower index to a higher one, so
      // children_ is always a forest -- a naive recursive walk of it cannot
      // loop, and roots() plus children() always span every joint.
      if (parent >= 0 && static_cast<std::size_t>(parent) < i)
      {
        skeleton.children_[static_cast<std::size_t>(parent)].push_back(static_cast<int>(i));
      }
      else
      {
        skeleton.roots_.push_back(static_cast<int>(i));
      }

      // Exported rigs routinely carry duplicate node names (a "Bone" under
      // two different meshes). First occurrence wins so name lookups stay
      // stable across re-imports instead of depending on iteration luck.
      skeleton.byName_.emplace(skeleton.joints_[i].name, static_cast<int>(i));
    }

    return skeleton;
  }

  int Skeleton::find(const std::string &name) const
  {
    const auto it = byName_.find(name);
    return it == byName_.end() ? -1 : it->second;
  }

  Pose Skeleton::rest_pose() const
  {
    Pose pose;
    pose.resize(joints_.size());
    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
      pose.translations[i] = joints_[i].restTranslation;
      pose.rotations[i] = joints_[i].restRotation;
      pose.scales[i] = joints_[i].restScale;
    }
    return pose;
  }

  void Skeleton::local_to_global(const Pose &pose, std::vector<math::Mat4> &outGlobals) const
  {
    outGlobals.resize(joints_.size());

    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
      const SkeletonJoint &joint = joints_[i];

      // Each channel is bounds-checked on its own: a pose that was only
      // partially filled (or built for a different skeleton) falls back to
      // the rest transform for whatever it is missing.
      const math::Vec3 &translation = i < pose.translations.size() ? pose.translations[i] : joint.restTranslation;
      const math::Quat &rotation = i < pose.rotations.size() ? pose.rotations[i] : joint.restRotation;
      const math::Vec3 &scale = i < pose.scales.size() ? pose.scales[i] : joint.restScale;

      math::Mat4 local = math::buildModelMatrix(translation, rotation, scale);
      if (joint.hasCorrection)
      {
        // The pose replaces the node's TRS, but the part TRS could not carry
        // is authored geometry that no channel keys, so it applies on top of
        // whatever the pose says -- unconditionally, because "is this joint
        // keyed" is not a question a Pose can answer.
        local = local * joint.restCorrection;
      }

      // Joints are stored parent-before-child, so the parent's global is
      // already final and one forward pass is enough. A forward or self
      // reference cannot be resolved in that pass, so it is treated as a
      // root rather than reading an uninitialised matrix.
      if (joint.parent >= 0 && static_cast<std::size_t>(joint.parent) < i)
      {
        outGlobals[i] = outGlobals[static_cast<std::size_t>(joint.parent)] * local;
      }
      else
      {
        outGlobals[i] = local;
      }
    }
  }

  void Skeleton::global_positions(const std::vector<math::Mat4> &globals, std::vector<math::Vec3> &outPositions) const
  {
    outPositions.resize(globals.size());
    for (std::size_t i = 0; i < globals.size(); ++i)
    {
      // Column-major: the translation lives in the fourth column.
      outPositions[i] = math::Vec3{globals[i].m[3][0], globals[i].m[3][1], globals[i].m[3][2]};
    }
  }

  void Skeleton::globals_to_palette(
      const ModelAsset &asset,
      const std::vector<math::Mat4> &globals,
      std::vector<math::Mat4> &outPalette)
  {
    // Joint index == node index is the whole point of the skeleton view, so
    // the model's own palette builder applies unchanged. Delegating keeps
    // the skinning convention in exactly one place.
    asset.paletteFromNodeGlobals(globals, outPalette);
  }

  void Skeleton::pose_to_palette(
      const ModelAsset &asset,
      const Pose &pose,
      std::vector<math::Mat4> &outPalette) const
  {
    std::vector<math::Mat4> globals;
    local_to_global(pose, globals);
    globals_to_palette(asset, globals, outPalette);
  }
}
