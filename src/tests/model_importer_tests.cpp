#include <gtest/gtest.h>

#include <string>

#include "test_support.hpp"

#include "../engine/assets/model_importer.hpp"

namespace hades
{
  TEST(ModelImporterTest, ImportObjCollectsMeshAndMaterialMetadata)
  {
    std::string errorMessage;
    const auto model = ModelImporter::importFromFile(test_support::backpack_model_path(), &errorMessage);

    ASSERT_TRUE(model.has_value()) << errorMessage;
    EXPECT_EQ(model->formatHint, "obj");
    EXPECT_FALSE(model->meshes.empty());
    EXPECT_FALSE(model->materials.empty());
    EXPECT_GT(model->totalVertexCount, 0U);
    EXPECT_GT(model->totalFaceCount, 0U);
    EXPECT_FALSE(model->sourcePath.empty());
    EXPECT_FALSE(model->meshes.front().vertices.empty());
    EXPECT_FALSE(model->meshes.front().triangles.empty());
    EXPECT_EQ(model->meshes.front().vertexCount, model->meshes.front().vertices.size());
    EXPECT_EQ(model->meshes.front().faceCount, model->meshes.front().triangles.size());
  }
}
