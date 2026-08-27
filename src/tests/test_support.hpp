#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace hades::test_support
{
  struct ScopedDirectoryCleanup
  {
    explicit ScopedDirectoryCleanup(std::filesystem::path directoryPath)
        : directory(std::move(directoryPath)) {}

    ~ScopedDirectoryCleanup()
    {
      std::error_code errorCode;
      std::filesystem::remove_all(directory, errorCode);
    }

    std::filesystem::path directory;
  };

  inline std::filesystem::path unique_test_directory(const char *prefix)
  {
    const auto uniqueSuffix = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / (std::string(prefix) + "-" + uniqueSuffix);
  }

  inline void write_text_file(const std::filesystem::path &path, const std::string &content)
  {
    std::ofstream out(path, std::ios::binary);
    out << content;
  }

  template <typename T>
  void append_bytes(std::vector<uint8_t> &buffer, const T &value)
  {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
  }

  // Build a minimal skinned glTF on disk: a triangle rigged to two joints
  // (Bone1 offset one unit up from Bone0) plus a 1-second animation that
  // rotates Bone1 by 90 degrees around Z.
  inline void write_skinned_gltf(const std::filesystem::path &directory)
  {
    std::vector<uint8_t> bin;

    // Positions (vec3 float ×3) at offset 0.
    const float positions[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const size_t positionsOffset = bin.size();
    for (const auto &p : positions)
      for (float f : p)
        append_bytes(bin, f);

    // Normals (vec3 float ×3).
    const size_t normalsOffset = bin.size();
    for (int i = 0; i < 3; ++i)
    {
      append_bytes(bin, 0.0f);
      append_bytes(bin, 0.0f);
      append_bytes(bin, 1.0f);
    }

    // JOINTS_0 (u16vec4 ×3): v0 → joint 0, v1 → joint 1, v2 → both.
    const size_t jointsOffset = bin.size();
    const uint16_t joints[3][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}};
    for (const auto &j : joints)
      for (uint16_t v : j)
        append_bytes(bin, v);

    // WEIGHTS_0 (vec4 float ×3).
    const size_t weightsOffset = bin.size();
    const float weights[3][4] = {{1, 0, 0, 0}, {1, 0, 0, 0}, {0.5f, 0.5f, 0, 0}};
    for (const auto &w : weights)
      for (float v : w)
        append_bytes(bin, v);

    // Indices (u16 ×3), padded to 4 bytes.
    const size_t indicesOffset = bin.size();
    for (uint16_t i : {uint16_t{0}, uint16_t{1}, uint16_t{2}})
      append_bytes(bin, i);
    append_bytes(bin, uint16_t{0}); // padding

    // Animation key times (float ×2).
    const size_t timesOffset = bin.size();
    append_bytes(bin, 0.0f);
    append_bytes(bin, 1.0f);

    // Animation rotations (quat float ×2): identity → 90° around Z.
    const size_t rotationsOffset = bin.size();
    const float halfSqrt2 = 0.70710678f;
    const float rotations[2][4] = {{0, 0, 0, 1}, {0, 0, halfSqrt2, halfSqrt2}};
    for (const auto &r : rotations)
      for (float v : r)
        append_bytes(bin, v);

    // Inverse bind matrices (mat4 float ×2, column-major):
    // identity for Bone0, translate(0,-1,0) for Bone1.
    const size_t ibmOffset = bin.size();
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float ibm1[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    for (float v : identity)
      append_bytes(bin, v);
    for (float v : ibm1)
      append_bytes(bin, v);

    {
      std::ofstream out(directory / "model.bin", std::ios::binary);
      out.write(reinterpret_cast<const char *>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }

    nlohmann::json gltf;
    gltf["asset"] = {{"version", "2.0"}};
    gltf["scene"] = 0;
    gltf["scenes"] = {{{"nodes", {0, 1}}}};
    gltf["nodes"] = {
        {{"name", "MeshNode"}, {"mesh", 0}, {"skin", 0}},
        {{"name", "Bone0"}, {"children", {2}}},
        {{"name", "Bone1"}, {"translation", {0.0, 1.0, 0.0}}}};
    gltf["meshes"] = {
        {{"primitives",
          {{{"attributes",
             {{"POSITION", 0}, {"NORMAL", 1}, {"JOINTS_0", 2}, {"WEIGHTS_0", 3}}},
            {"indices", 4}}}}}};
    gltf["skins"] = {
        {{"inverseBindMatrices", 7}, {"joints", {1, 2}}}};
    gltf["animations"] = {
        {{"name", "spin"},
         {"channels",
          {{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}}},
         {"samplers",
          {{{"input", 5}, {"output", 6}, {"interpolation", "LINEAR"}}}}}};

    const auto bufferView = [](size_t offset, size_t length) {
      return nlohmann::json{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
    };
    gltf["bufferViews"] = {
        bufferView(positionsOffset, 36),
        bufferView(normalsOffset, 36),
        bufferView(jointsOffset, 24),
        bufferView(weightsOffset, 48),
        bufferView(indicesOffset, 6),
        bufferView(timesOffset, 8),
        bufferView(rotationsOffset, 32),
        bufferView(ibmOffset, 128)};

    gltf["accessors"] = {
        {{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"},
         {"min", {0.0, 0.0, 0.0}}, {"max", {1.0, 1.0, 0.0}}},
        {{"bufferView", 1}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
        {{"bufferView", 2}, {"componentType", 5123}, {"count", 3}, {"type", "VEC4"}},
        {{"bufferView", 3}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}},
        {{"bufferView", 4}, {"componentType", 5123}, {"count", 3}, {"type", "SCALAR"}},
        {{"bufferView", 5}, {"componentType", 5126}, {"count", 2}, {"type", "SCALAR"},
         {"min", {0.0}}, {"max", {1.0}}},
        {{"bufferView", 6}, {"componentType", 5126}, {"count", 2}, {"type", "VEC4"}},
        {{"bufferView", 7}, {"componentType", 5126}, {"count", 2}, {"type", "MAT4"}}};

    gltf["buffers"] = {
        {{"uri", "model.bin"}, {"byteLength", bin.size()}}};

    write_text_file(directory / "model.gltf", gltf.dump(2));
  }
}
