/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Render interpolation between simulation ticks (design note render_interp).
// Visual census tag: VC_K_cadence.

#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/options.h"
#include "reone/scene/graphs.h"
#include "reone/scene/interp.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/modelnode.h"
#include "reone/system/fixedstep.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace {

constexpr float kTick = 1.0f / 30.0f;

struct InterpFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;

    std::shared_ptr<ModelNode> rootNode;
    std::vector<std::shared_ptr<Animation>> animations;
    std::unique_ptr<Model> model;

    InterpFixture() {
        interp::resetForTest();
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

        rootNode = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
        // "walk": root_node goes 3 m along x over 1 s. "other": it stands at y 5.
        auto walkRoot = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
        walkRoot->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f));
        walkRoot->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(3.0f, 0.0f, 0.0f));
        auto otherRoot = std::make_shared<ModelNode>(0, "root_node", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false, nullptr);
        otherRoot->vectorTracks()[ControllerTypes::position].add(0.0f, glm::vec3(0.0f, 5.0f, 0.0f));
        otherRoot->vectorTracks()[ControllerTypes::position].add(1.0f, glm::vec3(0.0f, 5.0f, 0.0f));
        animations.push_back(std::make_shared<Animation>("walk", 1.0f, 0.0f, "root_node", walkRoot, std::vector<Animation::Event>()));
        animations.push_back(std::make_shared<Animation>("other", 1.0f, 0.0f, "root_node", otherRoot, std::vector<Animation::Event>()));
        model = std::make_unique<Model>("interp_model", 0, rootNode, animations, "", 1.0f);
        model->init();
    }

    ~InterpFixture() {
        interp::resetForTest();
    }

    std::shared_ptr<ModelSceneNode> newModel() {
        auto node = scene->newModel(*model, ModelUsage::Creature);
        node->init();
        return node;
    }

    template <class F>
    void tick(F &&body) {
        interp::TickScope scope;
        body();
    }
};

float x(const SceneNode &node) {
    return node.localTransform()[3].x;
}

} // namespace

TEST(RenderInterp, VC_K_cadence_animated_node_at_alpha_half_is_the_midpoint_of_two_ticks) {
    InterpFixture f;
    auto model = f.newModel();
    model->playAnimation("walk", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    auto bone = model->getNodeByName("root_node");
    ASSERT_TRUE(bone);

    // Two 30 Hz ticks: the bone is at 0.1 m then at 0.2 m.
    f.tick([&] { model->update(kTick); });
    f.tick([&] { model->update(kTick); });
    ASSERT_NEAR(0.2f, x(*bone), 1e-5f);

    interp::apply(0.5f);
    EXPECT_TRUE(interp::applied());
    EXPECT_NEAR(0.15f, x(*bone), 1e-5f);
    EXPECT_NEAR(0.15f, bone->absoluteTransform()[3].x, 1e-5f);

    interp::restore();
    EXPECT_FALSE(interp::applied());
    EXPECT_NEAR(0.2f, x(*bone), 1e-5f);
    EXPECT_NEAR(0.2f, bone->absoluteTransform()[3].x, 1e-5f);
}

TEST(RenderInterp, VC_K_cadence_fixed_step_alpha_drives_even_steps_at_144_hz) {
    // A walking root drawn at 144 Hz from 30 Hz ticks: every frame advances
    // by the same distance (1/144 s of motion), not every fifth frame.
    InterpFixture f;
    auto model = f.newModel();
    FixedStepClock clock(30);
    float speed = 1.5f; // m/s
    float simX = 0.0f;
    std::vector<float> drawn;
    for (int frame = 0; frame < 144; ++frame) {
        int due = clock.advance(1.0 / 144.0);
        for (int i = 0; i < due; ++i) {
            f.tick([&] {
                simX += speed * kTick;
                model->setLocalTransform(glm::translate(glm::vec3(simX, 0.0f, 0.0f)));
            });
        }
        interp::apply(clock.alpha());
        drawn.push_back(model->absoluteTransform()[3].x);
        interp::restore();
        ASSERT_NEAR(simX, model->absoluteTransform()[3].x, 1e-5f);
    }
    // After the first tick, each frame moves 1.5 / 144 m.
    for (size_t i = 10; i < drawn.size(); ++i) {
        EXPECT_NEAR(speed / 144.0f, drawn[i] - drawn[i - 1], 2e-4f) << "frame " << i;
    }
}

TEST(RenderInterp, facing_is_slerped_and_children_follow) {
    InterpFixture f;
    auto model = f.newModel();
    auto bone = model->getNodeByName("root_node");
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f)) * glm::mat4_cast(glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)))); });

    interp::apply(0.5f);
    glm::mat4 drawn = model->absoluteTransform();
    EXPECT_NEAR(0.5f, drawn[3].x, 1e-5f);
    // 45 degrees: the x axis points half way between +x and +y.
    EXPECT_NEAR(std::cos(glm::quarter_pi<float>()), drawn[0].x, 1e-5f);
    EXPECT_NEAR(std::sin(glm::quarter_pi<float>()), drawn[0].y, 1e-5f);
    // The child is drawn with its parent.
    EXPECT_NEAR(0.5f, bone->absoluteTransform()[3].x, 1e-5f);
    interp::restore();
    EXPECT_NEAR(1.0f, model->absoluteTransform()[3].x, 1e-5f);
    EXPECT_NEAR(0.0f, model->absoluteTransform()[0].x, 1e-5f);
    EXPECT_NEAR(1.0f, bone->absoluteTransform()[3].x, 1e-5f);
}

TEST(RenderInterp, alpha_one_draws_the_tick_as_simulated) {
    // Headless runs and a closed tick gate draw at alpha 1: captures unchanged.
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    interp::apply(1.0f);
    EXPECT_FALSE(interp::applied());
    EXPECT_FLOAT_EQ(1.0f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, a_new_tick_restores_first) {
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    interp::apply(0.25f);
    // Simulation reads its own value, never the blend.
    f.tick([&] {
        EXPECT_FLOAT_EQ(1.0f, model->absoluteTransform()[3].x);
        model->setLocalTransform(glm::translate(glm::vec3(2.0f, 0.0f, 0.0f)));
    });
    interp::apply(0.5f);
    EXPECT_NEAR(1.5f, model->absoluteTransform()[3].x, 1e-5f);
    interp::restore();
}

TEST(RenderInterp, a_node_the_last_tick_did_not_move_is_drawn_still) {
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    f.tick([] {});
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(1.0f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, explicit_snap_draws_the_tick_value) {
    InterpFixture f;
    auto model = f.newModel();
    auto bone = model->getNodeByName("root_node");
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] {
        model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f)));
        model->snapInterpolation(); // a jump
    });
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(1.0f, model->absoluteTransform()[3].x);
    EXPECT_FLOAT_EQ(1.0f, bone->absoluteTransform()[3].x);
    EXPECT_EQ(1, interp::stats().snapped);
    // The flag lasts one tick.
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(2.0f, 0.0f, 0.0f))); });
    interp::apply(0.5f);
    EXPECT_NEAR(1.5f, model->absoluteTransform()[3].x, 1e-5f);
    interp::restore();
}

TEST(RenderInterp, a_warp_farther_than_the_snap_distance_is_not_blended) {
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(interp::kSnapDistance + 1.0f, 0.0f, 0.0f))); });
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(interp::kSnapDistance + 1.0f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, a_node_created_this_tick_is_not_blended_from_identity) {
    InterpFixture f;
    std::shared_ptr<ModelSceneNode> model;
    f.tick([&] {
        model = f.newModel();
        model->setLocalTransform(glm::translate(glm::vec3(2.0f, 0.0f, 0.0f)));
    });
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(2.0f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, an_animation_change_snaps_the_bones_but_not_the_root) {
    InterpFixture f;
    auto model = f.newModel();
    auto bone = model->getNodeByName("root_node");
    model->playAnimation("walk", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    f.tick([&] {
        model->setLocalTransform(glm::translate(glm::vec3(0.0f)));
        model->update(kTick);
    });
    f.tick([&] {
        model->setLocalTransform(glm::translate(glm::vec3(0.0f, 0.0f, 1.0f)));
        model->playAnimation("other", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
        model->update(kTick);
    });
    interp::apply(0.5f);
    // The root still moves smoothly...
    EXPECT_NEAR(0.5f, model->absoluteTransform()[3].z, 1e-5f);
    // ...but the bone is drawn in the new clip's pose, not half way to it.
    EXPECT_NEAR(5.0f, bone->localTransform()[3].y, 1e-5f);
    EXPECT_NEAR(0.0f, bone->localTransform()[3].x, 1e-5f);
    interp::restore();
}

TEST(RenderInterp, an_animation_restart_snaps_the_bones) {
    InterpFixture f;
    auto model = f.newModel();
    auto bone = model->getNodeByName("root_node");
    model->playAnimation("walk", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    for (int i = 0; i < 10; ++i) {
        f.tick([&] { model->update(kTick); });
    }
    f.tick([&] {
        ASSERT_TRUE(model->restartAnimation("walk"));
        model->update(kTick);
    });
    interp::apply(0.5f);
    EXPECT_NEAR(0.1f, x(*bone), 1e-5f);
    interp::restore();
}

TEST(RenderInterp, a_presentation_write_takes_the_node_out_of_the_blend) {
    // Co-op clients place replicas themselves each frame (N1): the scene
    // must not blend those a second time.
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    model->setLocalTransform(glm::translate(glm::vec3(0.7f, 0.0f, 0.0f)));
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(0.7f, model->absoluteTransform()[3].x);
    interp::restore();
    EXPECT_FLOAT_EQ(0.7f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, a_node_destroyed_after_its_tick_is_forgotten) {
    InterpFixture f;
    // Owned here only (the graph keeps the nodes it makes).
    auto model = std::make_shared<ModelSceneNode>(*f.model, ModelUsage::Creature, *f.scene,
                                                  f.graphicsModule.services(), f.audioModule.services(), f.resourceModule.services());
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    model.reset();
    interp::apply(0.5f);
    interp::restore();
    EXPECT_EQ(0, interp::stats().blended);
}

TEST(RenderInterp, snap_all_drops_the_tick) {
    InterpFixture f;
    auto model = f.newModel();
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { model->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    interp::snapAll();
    interp::apply(0.5f);
    EXPECT_FLOAT_EQ(1.0f, model->absoluteTransform()[3].x);
}

TEST(RenderInterp, blend_keeps_scale_and_handles_mirroring) {
    glm::mat4 a = glm::scale(glm::vec3(1.0f));
    glm::mat4 b = glm::scale(glm::vec3(3.0f));
    glm::mat4 m = interp::blend(a, b, 0.5f);
    EXPECT_NEAR(2.0f, m[0].x, 1e-5f);
    EXPECT_NEAR(2.0f, m[1].y, 1e-5f);
    glm::mat4 mirrored = glm::scale(glm::vec3(-1.0f, 1.0f, 1.0f));
    glm::mat4 same = interp::blend(mirrored, mirrored, 0.5f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(mirrored[c][r], same[c][r], 1e-5f);
        }
    }
}

TEST(RenderInterp, blending_is_only_the_recompute_inside_apply) {
    // A node placed by presentation code while other nodes are drawn blended
    // (a visual effect following its target) holds a simulated transform:
    // emitters must not take that move for a drawn-only one.
    InterpFixture f;
    auto walker = f.newModel();
    auto effect = f.newModel();
    f.tick([&] { walker->setLocalTransform(glm::translate(glm::vec3(0.0f))); });
    f.tick([&] { walker->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f))); });
    EXPECT_FALSE(interp::blending());

    interp::apply(0.5f);
    ASSERT_TRUE(interp::applied());
    EXPECT_FALSE(interp::blending());
    effect->setLocalTransform(glm::translate(glm::vec3(5.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(interp::blending());
    EXPECT_NEAR(5.0f, effect->absoluteTransform()[3].x, 1e-5f);
    interp::restore();
    EXPECT_FALSE(interp::blending());
    EXPECT_NEAR(5.0f, effect->absoluteTransform()[3].x, 1e-5f);
}
