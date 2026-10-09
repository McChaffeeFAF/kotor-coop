/*
 * Copyright (c) 2026 The reone project contributors
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

// visualeffects.2da keyed by row label, and the presenter that draws a row
// (spec: design note v_vfx_emitters, sections 1-4).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "reone/audio/clip.h"
#include "reone/game/visualeffectpresenter.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/resource/2da.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::game;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

using testing::_;
using testing::Return;

namespace {

const std::vector<std::string> kColumns {
    "label", "type_fd", "orientwithground", "orientationoff", "imp_headcon_node", "imp_impact_node",
    "imp_root_s_node", "imp_root_m_node", "imp_root_l_node", "imp_root_h_node", "progfx_impact", "soundimpact",
    "progfx_duration", "soundduration", "progfx_cessation", "soundcessastion", "ces_headcon_node", "ces_impact_node",
    "ces_root_s_node", "ces_root_m_node", "ces_root_l_node", "ces_root_h_node", "shaketype", "shakedelay",
    "shakeduration", "lowviolence", "lowquality", "rumblepattern", "rumblecutoff"};

std::vector<std::string> row(std::map<std::string, std::string> cells) {
    std::vector<std::string> values;
    for (auto &column : kColumns) {
        auto it = cells.find(column);
        values.push_back(it != cells.end() ? it->second : "****");
    }
    return values;
}

const glm::quat kIdentity(1.0f, 0.0f, 0.0f, 0.0f);

/** A one-node model carrying the named animations, each `length` seconds. */
std::shared_ptr<Model> effectModel(const std::string &name, std::vector<std::string> anims, float length = 0.2f) {
    auto root = std::make_shared<ModelNode>(0, name, glm::vec3(0.0f), kIdentity, true, nullptr);
    std::vector<std::shared_ptr<Animation>> animations;
    for (auto &anim : anims) {
        animations.push_back(std::make_shared<Animation>(anim, length, 0.0f, "", nullptr, std::vector<Animation::Event>()));
    }
    auto model = std::make_shared<Model>(name, 0, root, animations, "", 1.0f);
    model->init();
    return model;
}

/** A creature-like model with impact and headconjure points. */
std::shared_ptr<Model> targetModel() {
    auto root = std::make_shared<ModelNode>(0, "creature", glm::vec3(0.0f), kIdentity, true, nullptr);
    auto impact = std::make_shared<ModelNode>(1, "impact", glm::vec3(0.0f, 0.0f, 1.2f), kIdentity, true, root.get());
    auto head = std::make_shared<ModelNode>(2, "headconjure", glm::vec3(0.0f, 0.1f, 1.8f), kIdentity, true, root.get());
    root->addChild(impact);
    root->addChild(head);
    auto model = std::make_shared<Model>("creature", 0, root, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model->init();
    return model;
}

struct PresenterScene {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> graph;
    MockAudioMixer mixer;
    std::shared_ptr<Model> creatureModel;
    std::shared_ptr<ModelSceneNode> creature;
    VisualEffectPresenter presenter;
    VisualEffectPresenter::Context ctx;

    PresenterScene() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        graph = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());
        creatureModel = targetModel();
        creature = graph->newModel(*creatureModel, ModelUsage::Creature);
        creature->setLocalTransform(glm::translate(glm::vec3(10.0f, 20.0f, 0.0f)) *
                                    glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
        graph->addRoot(creature);
        ON_CALL(mixer, play(_, _, _, _, _)).WillByDefault(Return(nullptr));
        ctx.graph = graph.get();
        ctx.mixer = &mixer;
        ctx.modelOf = [this](uint32_t id) -> ModelSceneNode * { return id == 7 ? creature.get() : nullptr; };
    }

    std::shared_ptr<VisualEffectPresenter::Request> begin(const VisualEffectDesc &desc, uint32_t target = 7) {
        auto request = std::make_shared<VisualEffectPresenter::Request>();
        request->desc = &desc;
        request->targetId = target;
        request->location = glm::vec3(1.0f, 2.0f, 3.0f);
        presenter.begin(request);
        return request;
    }

    /** One simulation tick (model animation) and one presentation frame. */
    void frame(float dt = 0.05f) {
        graph->updateModelRoots(dt);
        presenter.update(dt, ctx);
    }
};

} // namespace

TEST(VisualEffects, VC_V_vfxid_rows_are_keyed_by_their_row_label_not_their_index) {
    TwoDA::Builder builder;
    builder.columns(kColumns);
    builder.row("1001", row({{"label", "VFX_IMP_HEALING_SMALL"}, {"type_fd", "F"}}));
    builder.row("3003", row({{"label", "VFX_FNF_GRENADE_FRAGMENTATION"}, {"type_fd", "F"}}));
    builder.row("junk", row({{"label", "NOT_AN_ID"}}));
    auto table = builder.build();

    auto effects = VisualEffects::parse(
        *table,
        [](const std::string &) { return nullptr; },
        [](const std::string &) { return nullptr; });

    ASSERT_EQ(2u, effects.size());
    EXPECT_EQ("VFX_IMP_HEALING_SMALL", effects.at(1001).label);
    EXPECT_EQ("VFX_FNF_GRENADE_FRAGMENTATION", effects.at(3003).label);
    EXPECT_EQ(0u, effects.count(0));
    EXPECT_EQ(0u, effects.count(1));
}

TEST(VisualEffects, every_column_k1_fills_is_read) {
    // VC_V_col_type_fd VC_V_col_orientwithground VC_V_col_orientationoff
    // VC_V_col_imp_headcon_node VC_V_col_imp_impact_node VC_V_col_imp_root_m_node
    // VC_V_col_soundimpact VC_V_col_soundduration VC_V_col_soundcessastion
    // VC_V_col_shaketype VC_V_col_shakeduration VC_V_col_rumblepattern VC_V_col_rumblecutoff
    TwoDA::Builder builder;
    builder.columns(kColumns);
    builder.row("2032", row({{"label", "VFX_DUR_SHIELD_RED_MARK_I"},
                             {"type_fd", "D"},
                             {"orientwithground", "1"},
                             {"orientationoff", "1"},
                             {"imp_headcon_node", "v_head"},
                             {"imp_impact_node", "v_chest"},
                             {"imp_root_m_node", "v_feet"},
                             {"progfx_impact", "1405"},
                             {"soundimpact", "s_on"},
                             {"progfx_duration", "1413"},
                             {"soundduration", "s_loop"},
                             {"soundcessastion", "s_off"},
                             {"shaketype", "2"},
                             {"shakeduration", "1"},
                             {"rumblepattern", "14"},
                             {"rumblecutoff", "5"}}));
    auto table = builder.build();
    std::map<std::string, std::shared_ptr<Model>> models {
        {"v_head", effectModel("v_head", {"impact"})},
        {"v_chest", effectModel("v_chest", {"impact"})},
        {"v_feet", effectModel("v_feet", {"impact"})}};
    std::map<std::string, std::shared_ptr<AudioClip>> sounds {
        {"s_on", std::make_shared<AudioClip>()},
        {"s_loop", std::make_shared<AudioClip>()},
        {"s_off", std::make_shared<AudioClip>()}};

    auto effects = VisualEffects::parse(
        *table,
        [&](const std::string &resRef) { return models.count(resRef) ? models.at(resRef) : nullptr; },
        [&](const std::string &resRef) { return sounds.count(resRef) ? sounds.at(resRef) : nullptr; });

    auto &desc = effects.at(2032);
    EXPECT_EQ(VisualEffectDesc::Kind::Duration, desc.kind);
    EXPECT_TRUE(desc.orientWithGround);
    EXPECT_TRUE(desc.orientationOff);
    EXPECT_EQ(models["v_head"], desc.impHeadConNode);
    EXPECT_EQ(models["v_chest"], desc.impImpactNode);
    EXPECT_EQ(models["v_feet"], desc.impRootMNode);
    EXPECT_EQ(models["v_feet"], desc.rootModel());
    EXPECT_EQ(sounds["s_on"], desc.soundImpact);
    EXPECT_EQ(sounds["s_loop"], desc.soundDuration);
    EXPECT_EQ(sounds["s_off"], desc.soundCessation);
    EXPECT_EQ(2, desc.shakeType);
    EXPECT_FLOAT_EQ(1.0f, desc.shakeDuration);
    EXPECT_EQ(14, desc.rumblePattern);
    EXPECT_FLOAT_EQ(5.0f, desc.rumbleCutoff);
}

TEST(VisualEffectPresenter, a_fire_and_forget_model_plays_its_impact_once_even_after_the_effect_ends) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::FireAndForget;
    auto model = effectModel("v_imp", {"impact"}, 0.2f);
    desc.impRootMNode = model;

    auto request = scene.begin(desc);
    request->ended = true; // an instant effect ends on the tick it starts
    scene.frame();
    EXPECT_EQ(1u, scene.presenter.numModels());
    scene.frame();
    EXPECT_EQ(1u, scene.presenter.numModels()) << "still playing its impact";
    for (int i = 0; i < 6; ++i) {
        scene.frame();
    }
    EXPECT_EQ(0u, scene.presenter.numModels());
    EXPECT_EQ(0u, scene.presenter.numActive());
}

TEST(VisualEffectPresenter, a_duration_model_loops_until_the_effect_ends_then_plays_cessation) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    auto model = effectModel("v_dur", {"duration", "cessation"}, 0.2f);
    desc.impImpactNode = model;

    auto request = scene.begin(desc);
    for (int i = 0; i < 20; ++i) {
        scene.frame();
    }
    ASSERT_EQ(1u, scene.presenter.numModels()) << "looping for as long as the effect lasts";

    request->ended = true;
    scene.frame();
    EXPECT_EQ(1u, scene.presenter.numModels()) << "cessation is playing";
    for (int i = 0; i < 6; ++i) {
        scene.frame();
    }
    EXPECT_EQ(0u, scene.presenter.numModels());
}

TEST(VisualEffectPresenter, a_duration_effect_that_ended_before_it_was_drawn_plays_once) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.impImpactNode = effectModel("v_dur", {"duration"}, 0.2f);
    scene.begin(desc)->ended = true;
    scene.frame();
    EXPECT_EQ(1u, scene.presenter.numModels());
    for (int i = 0; i < 6; ++i) {
        scene.frame();
    }
    EXPECT_EQ(0u, scene.presenter.numModels());
}

TEST(VisualEffectPresenter, models_sit_on_the_targets_attach_points_and_turn_with_its_facing) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.impHeadConNode = effectModel("v_head", {"duration"});
    desc.impImpactNode = effectModel("v_chest", {"duration"});
    desc.impRootMNode = effectModel("v_feet", {"duration"});
    scene.begin(desc);
    scene.frame();

    glm::mat4 h = VisualEffectPresenter::attachTransform(*scene.creature, VisualEffectPresenter::AttachPoint::HeadConjure, false);
    glm::mat4 c = VisualEffectPresenter::attachTransform(*scene.creature, VisualEffectPresenter::AttachPoint::Impact, false);
    glm::mat4 r = VisualEffectPresenter::attachTransform(*scene.creature, VisualEffectPresenter::AttachPoint::Root, false);
    // The creature stands at (10, 20) turned a quarter left: its local +Y
    // (the headconjure offset) points along world -X.
    EXPECT_NEAR(9.9f, h[3].x, 1e-4f);
    EXPECT_NEAR(1.8f, h[3].z, 1e-4f);
    EXPECT_NEAR(1.2f, c[3].z, 1e-4f);
    EXPECT_NEAR(0.0f, r[3].z, 1e-4f);
    EXPECT_NEAR(10.0f, r[3].x, 1e-4f);
    EXPECT_NEAR(1.0f, r[0].y, 1e-4f) << "turned with the facing";
    EXPECT_EQ(3u, scene.presenter.numModels());

    glm::mat4 world = VisualEffectPresenter::attachTransform(*scene.creature, VisualEffectPresenter::AttachPoint::Impact, true);
    EXPECT_NEAR(1.0f, world[0].x, 1e-5f) << "orientationoff keeps world axes";
}

TEST(VisualEffectPresenter, impact_duration_and_cessation_sounds_play_through_the_mixer) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.soundImpact = std::make_shared<AudioClip>();
    desc.soundDuration = std::make_shared<AudioClip>();
    desc.soundCessation = std::make_shared<AudioClip>();
    desc.impRootMNode = effectModel("v_dur", {"duration"});

    EXPECT_CALL(scene.mixer, play(desc.soundImpact, AudioType::Sound, _, false, _)).Times(1);
    EXPECT_CALL(scene.mixer, play(desc.soundDuration, AudioType::Sound, _, true, _)).Times(1);
    auto request = scene.begin(desc);
    scene.frame();
    testing::Mock::VerifyAndClearExpectations(&scene.mixer);
    ON_CALL(scene.mixer, play(_, _, _, _, _)).WillByDefault(Return(nullptr));

    EXPECT_CALL(scene.mixer, play(desc.soundCessation, AudioType::Sound, _, false, _)).Times(1);
    request->ended = true;
    scene.frame();
}

TEST(VisualEffectPresenter, a_shake_row_moves_the_drawn_camera_and_fades_out) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.shakeType = 2;
    desc.shakeDuration = 0.3f;
    scene.ctx.cameraPosition = glm::vec3(10.0f, 22.0f, 1.5f);
    scene.begin(desc);
    scene.frame(0.05f);
    EXPECT_GT(glm::length(scene.presenter.cameraShake()), 0.0f);
    EXPECT_LT(glm::length(scene.presenter.cameraShake()), 0.25f);
    for (int i = 0; i < 10; ++i) {
        scene.frame(0.05f);
    }
    EXPECT_EQ(glm::vec3(0.0f), scene.presenter.cameraShake());
}

TEST(VisualEffectPresenter, a_location_effect_plays_where_it_was_placed) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.impRootMNode = effectModel("v_fnf", {"impact"}, 1.0f);
    scene.begin(desc, 0);
    scene.frame();
    ASSERT_EQ(1u, scene.presenter.numModels());
    scene.presenter.clear();
    EXPECT_EQ(0u, scene.presenter.numModels());
}
