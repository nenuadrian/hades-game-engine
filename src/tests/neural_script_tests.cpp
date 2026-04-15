#include <gtest/gtest.h>

#include <memory>

#include "../engine/runtime/hades_neural_script.hpp"

namespace hades
{
  namespace
  {
    // Minimal neural script used for type-level tests only — never ticked.
    class DummyNeural : public NeuralScript
    {
    public:
      hne::SpaceSpec observationSpace() const override
      {
        hne::BoxSpace s;
        s.shape = {3};
        s.low = {-1.0f, -1.0f, -1.0f};
        s.high = {1.0f, 1.0f, 1.0f};
        return s;
      }

      hne::SpaceSpec actionSpace() const override
      {
        hne::DiscreteSpace d;
        d.n = 2;
        return d;
      }

      void readObservation(ScriptContext & /*ctx*/, hne::Tensor & /*out*/) override {}
      void applyAction(ScriptContext & /*ctx*/,
                       const hne::Action & /*action*/,
                       float /*deltaTime*/) override {}
    };
  }

  TEST(NeuralScriptTest, IsNeuralReturnsTrueOnDerived)
  {
    DummyNeural script;
    EXPECT_TRUE(script.isNeural());
  }

  TEST(NeuralScriptTest, DynamicCastFromBaseDetectsNeural)
  {
    std::unique_ptr<HadesScript> base = std::make_unique<DummyNeural>();
    auto *neural = dynamic_cast<NeuralScript *>(base.get());
    ASSERT_NE(neural, nullptr);
    EXPECT_TRUE(neural->isNeural());
  }

  TEST(NeuralScriptTest, DeclaresExpectedSpaces)
  {
    DummyNeural script;
    const auto obs = script.observationSpace();
    const auto *box = std::get_if<hne::BoxSpace>(&obs);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->shape.size(), 1u);
    EXPECT_EQ(box->shape[0], 3);

    const auto act = script.actionSpace();
    const auto *disc = std::get_if<hne::DiscreteSpace>(&act);
    ASSERT_NE(disc, nullptr);
    EXPECT_EQ(disc->n, 2);
  }

  TEST(NeuralScriptTest, DefaultRewardAndDoneHooks)
  {
    DummyNeural script;
    // Use dummy managers — these hooks don't touch them.
    EntityManager em;
    ComponentManager cm;
    ScriptContext ctx{Entity::INVALID, cm, em, 0.0f, 0.0f};
    EXPECT_FLOAT_EQ(script.computeReward(ctx, 0.016f), 0.0f);
    EXPECT_FALSE(script.isDone(ctx));
  }
}
