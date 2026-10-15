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

#include <set>

#include "reone/audio/clip.h"
#include "reone/game/visualeffectpresenter.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/texture.h"
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

TEST(VisualEffects, a_programmed_beam_row_loads_the_beam_model_for_its_number) {
    TwoDA::Builder builder;
    builder.columns(kColumns);
    builder.row("2038", row({{"label", "VFX_BEAM_LIGHTNING_DARK_L"}, {"type_fd", "D"}, {"progfx_duration", "609"}}));
    builder.row("2040", row({{"label", "VFX_DUR_SHIELD_BLUE_01"}, {"type_fd", "D"}, {"progfx_duration", "1413"}}));
    auto table = builder.build();
    auto beam = effectModel("v_lightnx_dur", {"conjure01"});
    std::vector<std::string> asked;

    auto effects = VisualEffects::parse(
        *table,
        [&](const std::string &resRef) -> std::shared_ptr<Model> {
            asked.push_back(resRef);
            return resRef == "v_lightnx_dur" ? beam : nullptr;
        },
        [](const std::string &) { return nullptr; });

    EXPECT_EQ(609, effects.at(2038).progfxDuration);
    EXPECT_EQ(beam, effects.at(2038).beamModel);
    EXPECT_EQ(1413, effects.at(2040).progfxDuration);
    EXPECT_EQ(nullptr, effects.at(2040).beamModel) << "shields are not beams";
    EXPECT_EQ(std::vector<std::string> {"v_lightnx_dur"}, asked);
    // The fourteen beam numbers each have their own model.
    std::set<std::string> names;
    for (int progfx = 608; progfx <= 621; ++progfx) {
        names.insert(VisualEffectDesc::beamModelResRef(progfx));
    }
    EXPECT_EQ(14u, names.size());
    EXPECT_EQ(0u, names.count(""));
    EXPECT_EQ("", VisualEffectDesc::beamModelResRef(607));
    EXPECT_EQ("", VisualEffectDesc::beamModelResRef(1407));
}

namespace {

/** A beam model: an emitter-less root with one reattachable and one fixed reference. */
std::shared_ptr<Model> beamModel() {
    auto root = std::make_shared<ModelNode>(0, "v_beam", glm::vec3(0.0f), kIdentity, true, nullptr);
    auto far = std::make_shared<ModelNode>(1, "omenref01", glm::vec3(0.0f, 0.0f, 5.0f), kIdentity, true, root.get());
    auto farRef = std::make_shared<ModelNode::Reference>();
    farRef->reattachable = true;
    far->setReference(farRef);
    auto near = std::make_shared<ModelNode>(2, "omenref02", glm::vec3(0.0f, 0.0f, 1.0f), kIdentity, true, root.get());
    auto nearRef = std::make_shared<ModelNode::Reference>();
    nearRef->reattachable = false;
    near->setReference(nearRef);
    root->addChild(far);
    root->addChild(near);
    auto model = std::make_shared<Model>("v_beam", 0, root, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model->init();
    return model;
}

/** A caster-like model with a right hand hook. */
std::shared_ptr<Model> casterModel() {
    auto root = std::make_shared<ModelNode>(0, "caster", glm::vec3(0.0f), kIdentity, true, nullptr);
    auto hand = std::make_shared<ModelNode>(1, "rhand", glm::vec3(0.3f, 0.2f, 1.1f), kIdentity, true, root.get());
    root->addChild(hand);
    auto model = std::make_shared<Model>("caster", 0, root, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    model->init();
    return model;
}

} // namespace

TEST(VisualEffectPresenter, a_beam_leaves_the_source_hand_and_its_far_end_sits_on_the_target) {
    PresenterScene scene;
    auto casterModelData = casterModel();
    auto caster = scene.graph->newModel(*casterModelData, ModelUsage::Creature);
    caster->setLocalTransform(glm::translate(glm::vec3(10.0f, 15.0f, 0.0f)));
    scene.graph->addRoot(caster);
    scene.ctx.modelOf = [&](uint32_t id) -> ModelSceneNode * {
        return id == 7 ? scene.creature.get() : (id == 9 ? caster.get() : nullptr);
    };
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.beamModel = beamModel();

    auto request = scene.begin(desc);
    request->sourceId = 9;
    request->sourceBodyNode = static_cast<int>(BodyNode::Hand);
    scene.frame();
    ASSERT_EQ(1u, scene.presenter.numModels());
    for (int i = 0; i < 5; ++i) {
        scene.frame();
    }
    ASSERT_EQ(1u, scene.presenter.numModels()) << "lasts while the effect does";

    ModelSceneNode *beam = scene.presenter.models().front();
    ASSERT_TRUE(beam);
    glm::vec3 hand(10.3f, 15.2f, 1.1f);
    EXPECT_NEAR(0.0f, glm::distance(hand, glm::vec3(beam->absoluteTransform()[3])), 1e-4f) << "on the caster's right hand";
    glm::vec3 impact(10.0f, 20.0f, 1.2f); // the target's impact node
    EXPECT_NEAR(0.0f, glm::distance(impact, beam->getNodeByName("omenref01")->origin()), 1e-4f) << "far end on the target";
    EXPECT_GT(glm::distance(impact, beam->getNodeByName("omenref02")->origin()), 1.0f) << "a fixed reference stays on the beam";

    request->ended = true;
    scene.frame();
    EXPECT_EQ(0u, scene.presenter.numModels());
}

TEST(VisualEffectPresenter, a_beam_row_without_a_source_draws_no_beam) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.beamModel = beamModel();
    scene.begin(desc);
    scene.frame();
    EXPECT_EQ(0u, scene.presenter.numModels());
}

// Programmed body effects and cast-hand visuals
// (design note v14_programmed_effects).

TEST(VisualEffects, the_14xx_numbers_take_the_fx_tex_textures_with_13_left_out) {
    EXPECT_EQ("fx_tex_01", VisualEffectDesc::bodyLookFor(1401)->texture); // afflict
    EXPECT_EQ("fx_tex_04", VisualEffectDesc::bodyLookFor(1404)->texture); // hold
    EXPECT_EQ("fx_tex_05", VisualEffectDesc::bodyLookFor(1405)->texture); // force aura / shield
    EXPECT_EQ("fx_tex_12", VisualEffectDesc::bodyLookFor(1412)->texture);
    EXPECT_EQ("fx_tex_14", VisualEffectDesc::bodyLookFor(1413)->texture); // shield blue 01
    EXPECT_EQ("fx_tex_23", VisualEffectDesc::bodyLookFor(1422)->texture); // carbonite
    EXPECT_EQ("fx_tex_26", VisualEffectDesc::bodyLookFor(1425)->texture);
    EXPECT_EQ("fx_tex_stealth", VisualEffectDesc::bodyLookFor(1426)->texture);
    for (int progfx = 1401; progfx <= 1425; ++progfx) {
        EXPECT_NE("fx_tex_13", VisualEffectDesc::bodyLookFor(progfx)->texture) << progfx;
        EXPECT_EQ(1.0f, VisualEffectDesc::bodyLookFor(progfx)->opacity) << "an overlay, not a cloak: " << progfx;
    }
    for (int cloak : {1426, 2000, 2001, 1800}) {
        ASSERT_TRUE(VisualEffectDesc::bodyLookFor(cloak)) << cloak;
        EXPECT_LT(VisualEffectDesc::bodyLookFor(cloak)->opacity, 1.0f) << cloak;
    }
    EXPECT_EQ("fx_distortion", VisualEffectDesc::bodyLookFor(1800)->texture);
    EXPECT_FALSE(VisualEffectDesc::bodyLookFor(0));
    EXPECT_FALSE(VisualEffectDesc::bodyLookFor(1400));
    EXPECT_FALSE(VisualEffectDesc::bodyLookFor(1427));
    EXPECT_FALSE(VisualEffectDesc::bodyLookFor(609)) << "beams are drawn by their model";
}

TEST(VisualEffects, a_programmed_body_row_loads_its_texture) {
    TwoDA::Builder builder;
    builder.columns(kColumns);
    builder.row("2040", row({{"label", "VFX_DUR_SHIELD_BLUE_01"}, {"type_fd", "D"}, {"progfx_duration", "1413"}}));
    builder.row("1021", row({{"label", "VFX_PRO_LIGHTNING_L"}, {"type_fd", "F"}, {"progfx_impact", "1407"}}));
    builder.row("8001", row({{"label", "VFX_DUR_CLOAK_REMOVAL"}, {"type_fd", "F"}, {"progfx_impact", "1423"}, {"progfx_duration", "1800"}}));
    builder.row("1001", row({{"label", "VFX_PRO_FORCE_ARMOR"}, {"type_fd", "F"}, {"imp_impact_node", "v_farmor_imp"}}));
    auto table = builder.build();
    auto ramp = std::make_shared<Texture>("fx_tex_14", TextureType::TwoDim, Texture::Properties {});
    ramp->clear(32, 512, PixelFormat::RGB8);
    auto pattern = std::make_shared<Texture>("fx_tex_07", TextureType::TwoDim, Texture::Properties {});
    pattern->clear(128, 128, PixelFormat::RGB8);
    std::vector<std::string> asked;

    auto effects = VisualEffects::parse(
        *table,
        [](const std::string &) { return nullptr; },
        [](const std::string &) { return nullptr; },
        [&](const std::string &resRef) -> std::shared_ptr<Texture> {
            asked.push_back(resRef);
            return resRef == "fx_tex_14" ? ramp : (resRef == "fx_tex_07" ? pattern : nullptr);
        });

    ASSERT_TRUE(effects.at(2040).bodyLook);
    EXPECT_EQ(ramp, effects.at(2040).bodyTexture);
    ASSERT_TRUE(effects.at(1021).bodyLook);
    EXPECT_EQ(pattern, effects.at(1021).bodyTexture);
    ASSERT_TRUE(effects.at(8001).bodyLook);
    EXPECT_EQ("fx_tex_24", effects.at(8001).bodyLook->texture) << "a fire-and-forget row takes its impact number";
    EXPECT_FALSE(effects.at(1001).bodyLook) << "Force Armor is a model row";
    // Rows are read in label order (the map), so 1021, 2040, 8001.
    EXPECT_EQ(3u, asked.size());
}

TEST(VisualEffectPresenter, an_overlay_fades_in_holds_while_the_effect_lasts_and_fades_out) {
    // Lasting: in over 0.25 s, held, out over 0.4 s once ended.
    EXPECT_FLOAT_EQ(0.0f, VisualEffectPresenter::overlayStrength(true, 0.0f, -1.0f));
    EXPECT_FLOAT_EQ(0.5f, VisualEffectPresenter::overlayStrength(true, 0.125f, -1.0f));
    EXPECT_FLOAT_EQ(1.0f, VisualEffectPresenter::overlayStrength(true, 100.0f, -1.0f));
    EXPECT_FLOAT_EQ(0.5f, VisualEffectPresenter::overlayStrength(true, 100.0f, 0.2f));
    EXPECT_FALSE(VisualEffectPresenter::overlayDone(true, 100.0f, -1.0f));
    EXPECT_FALSE(VisualEffectPresenter::overlayDone(true, 100.0f, 0.2f));
    EXPECT_TRUE(VisualEffectPresenter::overlayDone(true, 100.0f, 0.4f));
    // Fire-and-forget: 1.5 s.
    EXPECT_FLOAT_EQ(1.0f, VisualEffectPresenter::overlayStrength(false, 0.5f, -1.0f));
    EXPECT_NEAR(0.5f, VisualEffectPresenter::overlayStrength(false, 1.2f, -1.0f), 1e-5f);
    EXPECT_FLOAT_EQ(0.0f, VisualEffectPresenter::overlayStrength(false, 1.5f, -1.0f));
    EXPECT_TRUE(VisualEffectPresenter::overlayDone(false, 1.5f, -1.0f));
}

TEST(VisualEffectPresenter, an_overlay_flipbook_steps_a_frame_per_one_over_fps) {
    // fx_tex_14: numx 1, numy 16, fps 16 - a one-second colour pulse.
    EXPECT_EQ(0, overlayCycleFrame(1, 16, 16.0f, 0.0f));
    EXPECT_EQ(0, overlayCycleFrame(1, 16, 16.0f, 0.06f));
    EXPECT_EQ(1, overlayCycleFrame(1, 16, 16.0f, 0.07f));
    EXPECT_EQ(15, overlayCycleFrame(1, 16, 16.0f, 0.99f));
    EXPECT_EQ(0, overlayCycleFrame(1, 16, 16.0f, 1.01f)) << "it loops";
    EXPECT_EQ(3, overlayCycleFrame(2, 2, 20.0f, 0.16f)) << "fx_tex_07: 2x2 at 20 fps";
    EXPECT_EQ(0, overlayCycleFrame(1, 1, 16.0f, 5.0f)) << "a still texture";
    EXPECT_EQ(0, overlayCycleFrame(2, 2, 0.0f, 5.0f)) << "no fps";
}

namespace {

std::shared_ptr<Texture> overlayTexture(const std::string &name, int w, int h) {
    auto texture = std::make_shared<Texture>(name, TextureType::TwoDim, Texture::Properties {});
    texture->clear(w, h, PixelFormat::RGB8);
    return texture;
}

} // namespace

TEST(VisualEffectPresenter, a_duration_body_effect_overlays_the_target_until_it_ends) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.bodyLook = VisualEffectDesc::BodyLook {"fx_tex_14", 1.0f};
    desc.bodyTexture = overlayTexture("fx_tex_14", 32, 512);

    auto request = scene.begin(desc);
    for (int i = 0; i < 10; ++i) {
        scene.frame();
    }
    ASSERT_EQ(1u, scene.creature->bodyOverlays().size());
    const auto &overlay = scene.creature->bodyOverlays().front();
    EXPECT_EQ(desc.bodyTexture.get(), overlay.texture);
    EXPECT_FLOAT_EQ(1.0f, overlay.strength) << "faded in";
    EXPECT_GT(overlay.time, 0.4f);
    EXPECT_FLOAT_EQ(1.0f, scene.creature->bodyOpacity());
    EXPECT_EQ(1u, scene.presenter.numActive()) << "no model, still shown";
    EXPECT_EQ(0u, scene.presenter.numModels());

    request->ended = true;
    scene.frame();
    ASSERT_EQ(1u, scene.creature->bodyOverlays().size()) << "fading out";
    for (int i = 0; i < 10; ++i) {
        scene.frame();
    }
    EXPECT_TRUE(scene.creature->bodyOverlays().empty());
    EXPECT_EQ(0u, scene.presenter.numActive());
    EXPECT_TRUE(scene.presenter.overlayTargets().empty());
}

TEST(VisualEffectPresenter, a_cloak_makes_the_body_see_through_and_a_crackle_shows_briefly) {
    PresenterScene scene;
    VisualEffectDesc cloak;
    cloak.kind = VisualEffectDesc::Kind::Duration;
    cloak.bodyLook = VisualEffectDesc::BodyLook {"fx_tex_stealth", 0.35f};
    cloak.bodyTexture = overlayTexture("fx_tex_stealth", 512, 512);
    VisualEffectDesc crackle;
    crackle.kind = VisualEffectDesc::Kind::FireAndForget;
    crackle.bodyLook = VisualEffectDesc::BodyLook {"fx_tex_07", 1.0f};
    crackle.bodyTexture = overlayTexture("fx_tex_07", 128, 128);

    auto cloakRequest = scene.begin(cloak);
    auto crackleRequest = scene.begin(crackle);
    crackleRequest->ended = true; // fire-and-forget
    for (int i = 0; i < 10; ++i) {
        scene.frame();
    }
    EXPECT_EQ(2u, scene.creature->bodyOverlays().size());
    EXPECT_NEAR(0.35f, scene.creature->bodyOpacity(), 1e-4f);
    for (int i = 0; i < 30; ++i) {
        scene.frame(); // past 1.5 s
    }
    ASSERT_EQ(1u, scene.creature->bodyOverlays().size()) << "the crackle is over, the cloak is not";
    EXPECT_EQ(cloak.bodyTexture.get(), scene.creature->bodyOverlays().front().texture);
    cloakRequest->ended = true;
    for (int i = 0; i < 10; ++i) {
        scene.frame();
    }
    EXPECT_TRUE(scene.creature->bodyOverlays().empty());
    EXPECT_FLOAT_EQ(1.0f, scene.creature->bodyOpacity());
}

TEST(VisualEffects, cast_hand_visuals_come_from_spells_2da_and_skip_models_k1_lacks) {
    TwoDA::Builder builder;
    builder.columns({"label", "conjhandvisual", "casthandvisual"});
    builder.row("0", {"FORCE_POWER_MASTER_ALTER_XXX", "****", "****"});
    builder.row("1", {"FORCE_POWER_AFFLICTION", "****", "v_con_dark"});
    builder.row("2", {"FORCE_POWER_HEAL", "****", "v_con_light"});
    builder.row("3", {"FORCE_POWER_X", "v_fpush_cas", "****"});
    auto table = builder.build();
    auto dark = effectModel("v_con_dark", {"cast01"});
    auto push = effectModel("v_fpush_cas", {"dur"});

    auto hands = VisualEffects::parseCastHands(*table, [&](const std::string &resRef) -> std::shared_ptr<Model> {
        return resRef == "v_con_dark" ? dark : (resRef == "v_fpush_cas" ? push : nullptr);
    });

    ASSERT_EQ(2u, hands.size());
    const auto &affliction = hands.at(VisualEffectDesc::castHandId(1));
    EXPECT_EQ(dark, affliction.handModel);
    EXPECT_EQ(VisualEffectDesc::Kind::Duration, affliction.kind);
    EXPECT_EQ("CASTHAND_FORCE_POWER_AFFLICTION", affliction.label);
    EXPECT_EQ(push, hands.at(VisualEffectDesc::castHandId(3)).handModel) << "conjhandvisual when casthandvisual is empty";
    EXPECT_EQ(0u, hands.count(VisualEffectDesc::castHandId(2))) << "v_con_light is not shipped";
    EXPECT_GE(VisualEffectDesc::castHandId(0), VisualEffectDesc::kCastHandBase);
}

TEST(VisualEffectPresenter, a_cast_hand_visual_plays_cast01_on_the_right_hand_until_the_cast_ends) {
    PresenterScene scene;
    auto casterModelData = casterModel();
    auto caster = scene.graph->newModel(*casterModelData, ModelUsage::Creature);
    caster->setLocalTransform(glm::translate(glm::vec3(10.0f, 15.0f, 0.0f)));
    scene.graph->addRoot(caster);
    scene.ctx.modelOf = [&](uint32_t id) -> ModelSceneNode * { return id == 9 ? caster.get() : nullptr; };
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.handModel = effectModel("v_con_dark", {"cast01"}, 0.2f);

    auto request = scene.begin(desc, 9);
    for (int i = 0; i < 10; ++i) {
        scene.frame(); // longer than one play of cast01: it loops
    }
    ASSERT_EQ(1u, scene.presenter.numModels());
    ModelSceneNode *hand = scene.presenter.models().front();
    EXPECT_TRUE(hand->isAnimationPlaying("cast01"));
    glm::vec3 rhand(10.3f, 15.2f, 1.1f);
    EXPECT_NEAR(0.0f, glm::distance(rhand, glm::vec3(hand->absoluteTransform()[3])), 1e-4f);

    request->ended = true;
    scene.frame();
    EXPECT_EQ(0u, scene.presenter.numModels());
}

// Design note v15_vfx_leftovers

TEST(VisualEffects, the_speed_rows_and_the_programmed_model_rows_are_recognised) {
    TwoDA::Builder builder;
    builder.columns(kColumns);
    builder.row("1020", row({{"label", "VFX_IMP_SPEED_KNIGHT"}, {"type_fd", "F"}, {"progfx_impact", "1500"}}));
    builder.row("2004", row({{"label", "VFX_DUR_SPEED"}, {"type_fd", "D"}, {"imp_impact_node", "v_speed_dur"}, {"progfx_duration", "1601"}}));
    builder.row("4036", row({{"label", "VFX_COM_JEDI_FORCE_FIZZLE"}, {"type_fd", "F"}, {"progfx_impact", "1201"}}));
    builder.row("4038", row({{"label", "VFX_COM_SNIPER_SHOT"}, {"type_fd", "F"}, {"imp_impact_node", "v_sn_r_gen"}, {"progfx_impact", "1202"}}));
    builder.row("6000", row({{"label", "VFX_IMP_MIRV"}, {"type_fd", "F"}, {"progfx_impact", "1300"}}));
    builder.row("7000", row({{"label", "VFX_DUR_MEDAL"}, {"type_fd", "D"}, {"progfx_duration", "1700"}}));
    builder.row("7002", row({{"label", "VFX_DUR_MASK_02"}, {"type_fd", "D"}, {"progfx_duration", "1702"}}));
    auto table = builder.build();
    std::map<std::string, std::shared_ptr<Model>> models {
        {"v_fizzle_imp", effectModel("v_fizzle_imp", {"impact"})},
        {"v_medal_dur", effectModel("v_medal_dur", {})},
        {"v_revmask2_dur", effectModel("v_revmask2_dur", {})}};

    auto effects = VisualEffects::parse(
        *table,
        [&](const std::string &resRef) -> std::shared_ptr<Model> {
            auto it = models.find(resRef);
            return it != models.end() ? it->second : nullptr;
        },
        [](const std::string &) { return nullptr; });

    EXPECT_TRUE(effects.at(1020).speedTrail);
    EXPECT_TRUE(effects.at(2004).speedTrail);
    EXPECT_FALSE(effects.at(4036).speedTrail);
    EXPECT_EQ(models["v_fizzle_imp"], effects.at(4036).progfxModel);
    EXPECT_EQ(VisualEffectDesc::ProgfxPlace::AboveHead, effects.at(4036).progfxPlace);
    EXPECT_EQ(models["v_medal_dur"], effects.at(7000).progfxModel);
    EXPECT_EQ(VisualEffectDesc::ProgfxPlace::Chest, effects.at(7000).progfxPlace);
    EXPECT_EQ(models["v_revmask2_dur"], effects.at(7002).progfxModel);
    EXPECT_EQ(VisualEffectDesc::ProgfxPlace::Head, effects.at(7002).progfxPlace);
    EXPECT_EQ(nullptr, effects.at(4038).progfxModel) << "1202 is shared with the sniper shot: left undrawn";
    EXPECT_EQ(nullptr, effects.at(6000).progfxModel) << "MIRV has no model";
}

TEST(VisualEffectPresenter, speed_afterimages_trail_a_moving_target_and_a_standing_one_pulses) {
    auto standing = VisualEffectPresenter::speedGhosts(glm::vec3(0.0f), 1.0f, 0.1f);
    ASSERT_EQ(1u, standing.size());
    EXPECT_TRUE(standing[0].ghost);
    EXPECT_EQ(glm::vec3(0.0f), standing[0].offset);
    EXPECT_GT(standing[0].inflate, 0.01f);
    EXPECT_GT(standing[0].strength, 0.0f);

    auto running = VisualEffectPresenter::speedGhosts(glm::vec3(4.0f, 0.0f, 0.0f), 1.0f, 0.1f);
    ASSERT_EQ(3u, running.size()) << "no pulse at full speed";
    for (size_t i = 0; i < running.size(); ++i) {
        EXPECT_TRUE(running[i].ghost);
        EXPECT_NEAR(-0.2f * static_cast<float>(i + 1), running[i].offset.x, 1e-5f) << "behind it";
        if (i > 0) {
            EXPECT_LT(running[i].strength, running[i - 1].strength) << "fainter further back";
        }
    }
    EXPECT_TRUE(VisualEffectPresenter::speedGhosts(glm::vec3(4.0f, 0.0f, 0.0f), 0.0f, 0.1f).empty());
}

TEST(VisualEffectPresenter, a_speed_effect_gives_the_moving_target_afterimages_until_it_ends) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.speedTrail = true;

    auto request = scene.begin(desc);
    glm::vec3 position(10.0f, 20.0f, 0.0f);
    for (int i = 0; i < 20; ++i) {
        position.y += 0.25f; // 5 m/s at 0.05 s frames
        scene.creature->setLocalTransform(glm::translate(position));
        scene.frame();
    }
    const auto &overlays = scene.creature->bodyOverlays();
    ASSERT_EQ(3u, overlays.size());
    EXPECT_TRUE(overlays[0].ghost);
    EXPECT_LT(overlays[0].offset.y, -0.1f) << "trailing behind the motion";
    EXPECT_NEAR(0.0f, overlays[0].offset.x, 1e-3f);
    EXPECT_FLOAT_EQ(1.0f, scene.creature->bodyOpacity()) << "the body itself is untouched";

    request->ended = true;
    for (int i = 0; i < 10; ++i) {
        scene.frame();
    }
    EXPECT_TRUE(scene.creature->bodyOverlays().empty());
    EXPECT_EQ(0u, scene.presenter.numActive());
}

TEST(VisualEffectPresenter, the_medal_is_pinned_to_the_front_of_the_chest) {
    PresenterScene scene;
    VisualEffectDesc desc;
    desc.kind = VisualEffectDesc::Kind::Duration;
    desc.progfxModel = effectModel("v_medal_dur", {});
    desc.progfxPlace = VisualEffectDesc::ProgfxPlace::Chest;

    scene.begin(desc);
    scene.frame();
    auto models = scene.presenter.models();
    ASSERT_EQ(1u, models.size());
    const glm::mat4 &transform = models[0]->localTransform();
    // The creature stands at (10, 20) turned a quarter left (facing -X), its
    // impact node 1.2 m up: in front is -X, its left is -Y.
    glm::vec3 position(transform[3]);
    EXPECT_NEAR(10.0f - 0.14f, position.x, 1e-4f);
    EXPECT_NEAR(20.0f - 0.08f, position.y, 1e-4f);
    EXPECT_NEAR(1.3f, position.z, 1e-4f);
    glm::vec3 back(transform[1]);
    EXPECT_NEAR(1.0f, back.x, 1e-4f) << "its face (-Y) looks where the creature does";
    glm::vec3 up(transform[2]);
    EXPECT_NEAR(1.0f, up.z, 1e-4f);
}
