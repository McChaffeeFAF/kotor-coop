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

// Emitter behaviour from the corrected header flags and the animated
// controllers (spec: design note v_vfx_emitters, sections 6 and 7).

#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/emitter.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/particle.h"
#include "reone/system/randomutil.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::scene;
using namespace reone::audio;
using namespace reone::resource;

namespace {

const glm::quat kIdentity(1.0f, 0.0f, 0.0f, 0.0f);

struct Fixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> graph;

    std::shared_ptr<ModelNode> root;
    std::shared_ptr<ModelNode> emitterNode;
    std::shared_ptr<ModelNode::Emitter> emitter;
    std::vector<std::shared_ptr<Animation>> animations;
    std::shared_ptr<Model> model;
    std::shared_ptr<ModelSceneNode> sceneModel;

    Fixture(ModelNode::Emitter::UpdateMode mode = ModelNode::Emitter::UpdateMode::Fountain) {
        setCosmeticRandomSeed(7);
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        graph = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

        root = std::make_shared<ModelNode>(0, "root", glm::vec3(0.0f), kIdentity, true, nullptr);
        emitter = std::make_shared<ModelNode::Emitter>();
        emitter->updateMode = mode;
        emitter->renderMode = ModelNode::Emitter::RenderMode::Normal;
        emitterNode = std::make_shared<ModelNode>(1, "emitter", glm::vec3(0.0f), kIdentity, true, root.get());
        emitterNode->setEmitter(emitter);
        emitterNode->floatTracks()[ControllerTypes::birthrate].add(0.0f, 10.0f);
        emitterNode->floatTracks()[ControllerTypes::lifeExp].add(0.0f, 5.0f);
        emitterNode->floatTracks()[ControllerTypes::sizeStart].add(0.0f, 1.0f);
        emitterNode->floatTracks()[ControllerTypes::sizeEnd].add(0.0f, 1.0f);
        root->addChild(emitterNode);
    }

    void rest(ControllerType type, float value) {
        emitterNode->floatTracks().erase(type);
        emitterNode->floatTracks()[type].add(0.0f, value);
    }

    EmitterSceneNode &build() {
        model = std::make_shared<Model>("m", 0, root, animations, "", 1.0f);
        model->init();
        sceneModel = graph->newModel(*model, ModelUsage::GUI);
        graph->addRoot(sceneModel);
        return *static_cast<EmitterSceneNode *>(sceneModel->getNodeByName("emitter"));
    }

    std::vector<ParticleSceneNode *> particles(EmitterSceneNode &node) {
        std::vector<ParticleSceneNode *> result;
        for (auto &child : node.children()) {
            if (child->type() == SceneNodeType::Particle) {
                result.push_back(static_cast<ParticleSceneNode *>(child));
            }
        }
        return result;
    }
};

std::shared_ptr<ModelNode> animNode(uint16_t number, const std::string &name, ModelNode *parent = nullptr) {
    return std::make_shared<ModelNode>(number, name, glm::vec3(0.0f), kIdentity, true, parent);
}

} // namespace

TEST(EmitterAnimation, VC_M_anim_emitter_birthrate_follows_the_playing_animation) {
    // VC_E_ctl_birthrate: rest value 0 as in the K1 grenade and heal effects,
    // keyed up to 20 by the impact animation.
    Fixture f;
    f.rest(ControllerTypes::birthrate, 0.0f);
    auto animRoot = animNode(0, "root");
    auto animEmitter = animNode(1, "emitter", animRoot.get());
    animEmitter->floatTracks()[ControllerTypes::birthrate].add(0.0f, 20.0f);
    animEmitter->floatTracks()[ControllerTypes::birthrate].add(1.0f, 20.0f);
    animRoot->addChild(animEmitter);
    auto impact = std::make_shared<Animation>("impact", 1.0f, 0.0f, "", animRoot, std::vector<Animation::Event>());
    f.animations.push_back(impact);
    auto &emitter = f.build();

    f.graph->update(0.2f);
    EXPECT_TRUE(f.particles(emitter).empty()) << "the rest birthrate is 0";

    f.sceneModel->playAnimation("impact");
    for (int i = 0; i < 10; ++i) {
        f.graph->update(0.05f);
    }
    EXPECT_FLOAT_EQ(20.0f, emitter.birthrate());
    EXPECT_GT(f.particles(emitter).size(), 3u);
}

TEST(EmitterAnimation, controllers_return_to_rest_when_the_animation_stops_keying_them) {
    Fixture f;
    auto animRoot = animNode(0, "root");
    auto animEmitter = animNode(1, "emitter", animRoot.get());
    animEmitter->floatTracks()[ControllerTypes::drag].add(0.0f, 2.0f); // VC_E_ctl_drag
    animRoot->addChild(animEmitter);
    f.animations.push_back(std::make_shared<Animation>("keyed", 0.5f, 0.0f, "", animRoot, std::vector<Animation::Event>()));
    f.animations.push_back(std::make_shared<Animation>("plain", 0.5f, 0.0f, "", nullptr, std::vector<Animation::Event>()));
    f.rest(ControllerTypes::drag, 0.5f);
    auto &emitter = f.build();
    EXPECT_FLOAT_EQ(0.5f, emitter.drag());

    f.sceneModel->playAnimation("keyed");
    f.graph->update(0.1f);
    EXPECT_FLOAT_EQ(2.0f, emitter.drag());

    f.sceneModel->playAnimation("plain");
    f.graph->update(0.1f);
    EXPECT_FLOAT_EQ(0.5f, emitter.drag());
}

// VC_M_ctl_radius VC_M_ctl_multiplier: the geometry values are the starting point.
TEST(EmitterAnimation, VC_M_anim_lightradius_light_radius_and_multiplier_follow_the_animation) {
    Fixture f;
    auto lightNode = std::make_shared<ModelNode>(2, "light", glm::vec3(0.0f), kIdentity, true, f.root.get());
    lightNode->setLight(std::make_shared<ModelNode::Light>());
    lightNode->floatTracks()[ControllerTypes::radius].add(0.0f, 1.0f);
    lightNode->floatTracks()[ControllerTypes::multiplier].add(0.0f, 1.0f);
    f.root->addChild(lightNode);
    auto animRoot = animNode(0, "root");
    auto animLight = animNode(2, "light", animRoot.get());
    animLight->floatTracks()[ControllerTypes::radius].add(0.0f, 1.0f);
    animLight->floatTracks()[ControllerTypes::radius].add(1.0f, 9.0f);
    animLight->floatTracks()[ControllerTypes::multiplier].add(0.0f, 3.0f);
    // A light keyed with id 100 (vertical displacement) must not be taken
    // for mesh self-illumination.
    animLight->floatTracks()[ControllerTypes::verticalDisplacement].add(0.0f, 4.0f);
    animRoot->addChild(animLight);
    f.animations.push_back(std::make_shared<Animation>("pulse", 1.0f, 0.0f, "", animRoot, std::vector<Animation::Event>()));
    f.build();
    auto light = static_cast<LightSceneNode *>(f.sceneModel->getNodeByName("light"));
    ASSERT_TRUE(light);
    EXPECT_FLOAT_EQ(1.0f, light->radius());

    f.sceneModel->playAnimation("pulse");
    f.graph->update(0.5f);
    EXPECT_NEAR(5.0f, light->radius(), 1e-4f);
    EXPECT_FLOAT_EQ(3.0f, light->multiplier());
}

TEST(EmitterBehaviour, VC_E_update_explosion_bursts_birthrate_particles_on_detonate) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 8.0f);
    auto &emitter = f.build();

    f.graph->update(1.0f);
    EXPECT_TRUE(f.particles(emitter).empty()) << "explosions do not emit over time";

    f.sceneModel->signalEvent("detonate");
    EXPECT_EQ(8u, f.particles(emitter).size());
}

TEST(EmitterBehaviour, detonated_particles_are_drawn_at_their_start_state_before_their_first_update) {
    // The impact models' detonate event fires during the model's animation
    // update, after the emitter has updated its particles for the frame. The
    // burst is drawn before its own first update: it must already have the
    // start size, colour and alpha, not a pooled particle's defaults (a white
    // 1 m square: the hit flash's white spot) or its last life's end state.
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 3.0f);
    f.rest(ControllerTypes::sizeStart, 0.014f);
    f.rest(ControllerTypes::sizeEnd, 0.5f);
    f.rest(ControllerTypes::alphaStart, 0.25f);
    f.rest(ControllerTypes::alphaEnd, 1.0f);
    f.emitterNode->vectorTracks()[ControllerTypes::colorStart].add(0.0f, glm::vec3(1.0f, 0.5f, 0.0f));
    f.emitterNode->vectorTracks()[ControllerTypes::colorEnd].add(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    auto &emitter = f.build();
    f.graph->update(0.0f);

    for (int life = 0; life < 2; ++life) {
        f.sceneModel->signalEvent("detonate");
        auto particles = f.particles(emitter);
        ASSERT_EQ(3u, particles.size());
        for (auto particle : particles) {
            EXPECT_NEAR(0.014f, particle->size().x, 1e-6f);
            EXPECT_NEAR(0.014f, particle->size().y, 1e-6f);
            EXPECT_NEAR(0.25f, particle->alpha(), 1e-6f);
            EXPECT_NEAR(1.0f, particle->color().r, 1e-6f);
            EXPECT_NEAR(0.5f, particle->color().g, 1e-6f);
            EXPECT_NEAR(0.0f, particle->color().b, 1e-6f);
        }
        // Age the burst out, so the second round reuses pooled particles
        // that ended at the end state.
        f.graph->update(4.9f);
        f.graph->update(0.2f);
        f.graph->update(0.0f);
        EXPECT_TRUE(f.particles(emitter).empty());
    }
}

TEST(EmitterBehaviour, fountains_ignore_detonate) {
    Fixture f;
    f.rest(ControllerTypes::birthrate, 0.0f);
    auto &emitter = f.build();
    f.sceneModel->signalEvent("detonate");
    EXPECT_TRUE(f.particles(emitter).empty());
}

TEST(EmitterBehaviour, mass_is_gravity_and_negative_mass_rises) {
    // VC_E_ctl_mass
    for (float mass : {1.0f, -1.0f}) {
        Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
        f.rest(ControllerTypes::birthrate, 1.0f);
        f.rest(ControllerTypes::mass, mass);
        auto &emitter = f.build();
        f.graph->update(0.0f);
        f.sceneModel->signalEvent("detonate");
        auto particle = f.particles(emitter).front();
        f.graph->update(0.5f);
        EXPECT_NEAR(mass * -9.81f * 0.5f, particle->velocity().z, 1e-3f);
    }
}

TEST(EmitterBehaviour, VC_E_ctl_drag_slows_particles) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::velocity, 4.0f);
    f.rest(ControllerTypes::drag, 1.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    EXPECT_NEAR(4.0f, glm::length(particle->velocity()), 1e-4f);
    f.graph->update(0.25f);
    EXPECT_NEAR(3.0f, glm::length(particle->velocity()), 1e-3f);
}

TEST(EmitterBehaviour, particles_stay_in_world_space_without_an_inherit_flag) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 1.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.1f);
    glm::vec3 before = particle->position();

    f.sceneModel->setLocalTransform(glm::translate(glm::vec3(5.0f, 0.0f, 0.0f)));
    f.graph->update(0.0f);
    EXPECT_NEAR(before.x, particle->position().x, 1e-4f);
}

TEST(EmitterBehaviour, VC_E_flag_inherit_particles_move_and_turn_with_the_emitter) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->inherit = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::velocity, 1.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.0f);
    glm::vec3 before = particle->position();

    // Turn the model a quarter about X: +Z velocity becomes -Y.
    f.sceneModel->setLocalTransform(glm::translate(glm::vec3(5.0f, 0.0f, 0.0f)) *
                                    glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f)));
    f.graph->update(0.0f);
    EXPECT_NEAR(before.x + 5.0f, particle->position().x, 1e-4f);
    EXPECT_NEAR(-1.0f, particle->velocity().y, 1e-4f);
}

TEST(EmitterBehaviour, VC_E_flag_inherit_local_and_inherit_part_carry_particles_without_turning) {
    for (int mode = 0; mode < 2; ++mode) {
        Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
        if (mode == 0) {
            f.emitter->inheritLocal = true; // VC_E_flag_inherit_local
        } else {
            f.emitter->inheritParticle = true; // VC_E_flag_inherit_part
        }
        f.rest(ControllerTypes::birthrate, 1.0f);
        f.rest(ControllerTypes::velocity, 1.0f);
        auto &emitter = f.build();
        f.graph->update(0.0f);
        f.sceneModel->signalEvent("detonate");
        auto particle = f.particles(emitter).front();
        f.graph->update(0.0f);
        glm::vec3 before = particle->position();

        f.sceneModel->setLocalTransform(glm::translate(glm::vec3(0.0f, 3.0f, 0.0f)) *
                                        glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f)));
        f.graph->update(0.0f);
        EXPECT_NEAR(before.y + 3.0f, particle->position().y, 1e-4f) << mode;
        EXPECT_NEAR(1.0f, particle->velocity().z, 1e-4f) << "velocity keeps its direction, mode " << mode;
    }
}

TEST(EmitterBehaviour, VC_E_flag_inherit_vel_adds_the_emitter_velocity_at_birth) {
    Fixture f;
    f.emitter->inheritVelocity = true;
    f.rest(ControllerTypes::birthrate, 0.0f);
    auto &emitter = f.build();
    f.graph->update(0.1f);
    f.sceneModel->setLocalTransform(glm::translate(glm::vec3(1.0f, 0.0f, 0.0f)));
    f.rest(ControllerTypes::birthrate, 1.0f);
    // Moved 1 m in 0.5 s: 2 m/s along X.
    emitter.applyAnimatedControllers({{static_cast<uint16_t>(ControllerTypes::birthrate), 10.0f}}, {});
    f.graph->update(0.5f);
    auto ps = f.particles(emitter);
    ASSERT_FALSE(ps.empty());
    EXPECT_NEAR(2.0f, ps.front()->velocity().x, 1e-3f);
}

TEST(EmitterBehaviour, VC_E_flag_random_starts_particles_on_random_frames) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->random = true;
    f.rest(ControllerTypes::birthrate, 40.0f);
    f.rest(ControllerTypes::frameStart, 0.0f);
    f.rest(ControllerTypes::frameEnd, 15.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    f.graph->update(0.0f);
    std::set<int> frames;
    for (auto particle : f.particles(emitter)) {
        EXPECT_GE(particle->frame(), 0);
        EXPECT_LE(particle->frame(), 15);
        frames.insert(particle->frame());
    }
    EXPECT_GT(frames.size(), 4u);

    Fixture plain(ModelNode::Emitter::UpdateMode::Explosion);
    plain.rest(ControllerTypes::birthrate, 10.0f);
    plain.rest(ControllerTypes::frameEnd, 15.0f);
    auto &plainEmitter = plain.build();
    plain.graph->update(0.0f);
    plain.sceneModel->signalEvent("detonate");
    plain.graph->update(0.0f);
    for (auto particle : plain.particles(plainEmitter)) {
        EXPECT_EQ(0, particle->frame());
    }
}

TEST(EmitterBehaviour, VC_E_flag_bounce_rebounds_off_the_ground_scaled_by_bounce_co) {
    // VC_E_ctl_bounce_co
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->bounce = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::mass, 1.0f);
    f.rest(ControllerTypes::bounceCo, 0.5f);
    auto &emitter = f.build();
    emitter.setGroundHeight(-0.1f);
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    float lastDown = 0.0f;
    bool bounced = false;
    for (int i = 0; i < 20 && !bounced; ++i) {
        lastDown = particle->velocity().z;
        f.graph->update(0.05f);
        if (particle->velocity().z > 0.0f) {
            bounced = true;
        }
    }
    ASSERT_TRUE(bounced);
    EXPECT_GE(particle->position().z, -0.1f - 1e-5f);
    EXPECT_NEAR(-0.5f * (lastDown - 9.81f * 0.05f), particle->velocity().z, 1e-3f);
}

TEST(EmitterBehaviour, VC_E_flag_splat_sticks_a_landing_particle_flat) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->bounce = true;
    f.emitter->splat = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::mass, 1.0f);
    f.rest(ControllerTypes::bounceCo, 0.5f);
    auto &emitter = f.build();
    emitter.setGroundHeight(-0.1f);
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    for (int i = 0; i < 20; ++i) {
        f.graph->update(0.05f);
    }
    EXPECT_TRUE(particle->isStuck());
    EXPECT_NEAR(-0.1f, particle->position().z, 1e-5f);
    EXPECT_EQ(glm::vec3(0.0f), particle->velocity());
}

TEST(EmitterBehaviour, VC_E_flag_affected_by_wind_drifts_with_the_area_wind) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->affectedByWind = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    auto &emitter = f.build();
    f.graph->setWindPower(2.0f);
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.5f);
    glm::vec2 drift(particle->position());
    EXPECT_GT(glm::length(drift), 0.5f);

    Fixture calm(ModelNode::Emitter::UpdateMode::Explosion);
    calm.emitter->affectedByWind = true;
    calm.rest(ControllerTypes::birthrate, 1.0f);
    auto &calmEmitter = calm.build();
    calm.graph->update(0.0f);
    calm.sceneModel->signalEvent("detonate");
    auto still = calm.particles(calmEmitter).front();
    calm.graph->update(0.5f);
    EXPECT_LT(glm::length(glm::vec2(still->position())), 1e-4f);
}

TEST(EmitterBehaviour, VC_E_p2p_gravity_mode_pulls_particles_toward_the_target_dummy) {
    // VC_E_ctl_grav
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->p2p = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::grav, 10.0f);
    auto target = std::make_shared<ModelNode>(2, "target", glm::vec3(4.0f, 0.0f, 0.0f), kIdentity, true, f.emitterNode.get());
    f.emitterNode->addChild(target);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.2f);
    EXPECT_GT(particle->velocity().x, 1.0f);
}

TEST(EmitterBehaviour, p2p_sel_follows_a_curve_that_ends_on_the_target) {
    // VC_E_ctl_p2p_bezier2 VC_E_ctl_p2p_bezier3
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.emitter->p2p = true;
    f.emitter->p2pSel = true;
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::lifeExp, 1.0f);
    f.rest(ControllerTypes::p2pBezier2, 1.0f);
    f.rest(ControllerTypes::p2pBezier3, 1.0f);
    auto target = std::make_shared<ModelNode>(2, "target", glm::vec3(4.0f, 0.0f, 0.0f), kIdentity, true, f.emitterNode.get());
    f.emitterNode->addChild(target);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.5f);
    EXPECT_GT(particle->position().z, 0.1f) << "bent up by the control distances";
    f.graph->update(0.49f);
    EXPECT_NEAR(4.0f, particle->position().x, 0.05f);
}

TEST(EmitterBehaviour, VC_E_spawntype_trail_spreads_births_along_the_path_moved) {
    Fixture f;
    f.emitter->spawnType = ModelNode::Emitter::SpawnType::Trail;
    f.rest(ControllerTypes::birthrate, 0.0f);
    auto &emitter = f.build();
    f.graph->update(0.1f);
    emitter.applyAnimatedControllers({{static_cast<uint16_t>(ControllerTypes::birthrate), 40.0f}}, {});
    f.sceneModel->setLocalTransform(glm::translate(glm::vec3(10.0f, 0.0f, 0.0f)));
    f.graph->update(0.1f);
    auto ps = f.particles(emitter);
    ASSERT_GT(ps.size(), 2u);
    float minX = 1e9f, maxX = -1e9f;
    for (auto p : ps) {
        minX = std::min(minX, p->position().x);
        maxX = std::max(maxX, p->position().x);
    }
    EXPECT_LT(minX, 6.0f);
    EXPECT_GT(maxX, 9.0f);
}

TEST(EmitterBehaviour, VC_E_ctl_particleRot_spins_particles) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::particleRot, 2.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.5f);
    EXPECT_NEAR(1.0f, particle->rotation(), 1e-4f);
}

TEST(EmitterBehaviour, VC_E_ctl_sizeStart_y_VC_E_ctl_sizeMid_y_VC_E_ctl_sizeEnd_y_set_the_particle_height) {
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::sizeMid, 1.0f);
    f.rest(ControllerTypes::sizeStartY, 3.0f);
    f.rest(ControllerTypes::sizeMidY, 3.0f);
    f.rest(ControllerTypes::sizeEndY, 3.0f);
    auto &emitter = f.build();
    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    f.graph->update(0.1f);
    EXPECT_FLOAT_EQ(1.0f, particle->size().x);
    EXPECT_FLOAT_EQ(3.0f, particle->size().y);
}

TEST(EmitterBehaviour, geometry_controller_values_reach_the_simulation) {
    // VC_E_ctl_lifeExp VC_E_ctl_frameStart VC_E_ctl_frameEnd VC_E_ctl_velocity
    // VC_E_ctl_colorStart VC_E_ctl_colorMid VC_E_ctl_colorEnd VC_E_ctl_alphaStart
    // VC_E_ctl_alphaMid VC_E_ctl_alphaEnd VC_E_ctl_percentStart VC_E_ctl_percentMid
    // VC_E_ctl_percentEnd VC_E_ctl_sizeStart VC_E_ctl_sizeMid VC_E_ctl_sizeEnd
    Fixture f(ModelNode::Emitter::UpdateMode::Explosion);
    f.rest(ControllerTypes::birthrate, 1.0f);
    f.rest(ControllerTypes::lifeExp, 2.0f);
    f.rest(ControllerTypes::frameStart, 2.0f);
    f.rest(ControllerTypes::frameEnd, 6.0f);
    f.rest(ControllerTypes::velocity, 3.0f);
    f.rest(ControllerTypes::sizeStart, 1.0f);
    f.rest(ControllerTypes::sizeMid, 2.0f);
    f.rest(ControllerTypes::sizeEnd, 3.0f);
    f.rest(ControllerTypes::alphaStart, 1.0f);
    f.rest(ControllerTypes::alphaMid, 0.5f);
    f.rest(ControllerTypes::alphaEnd, 0.0f);
    f.rest(ControllerTypes::percentStart, 0.0f);
    f.rest(ControllerTypes::percentMid, 0.5f);
    f.rest(ControllerTypes::percentEnd, 1.0f);
    f.emitterNode->vectorTracks()[ControllerTypes::colorStart].add(0.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    f.emitterNode->vectorTracks()[ControllerTypes::colorMid].add(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    f.emitterNode->vectorTracks()[ControllerTypes::colorEnd].add(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    auto &emitter = f.build();
    EXPECT_FLOAT_EQ(2.0f, emitter.lifeExpectancy());
    EXPECT_EQ(2, emitter.frameStart());
    EXPECT_EQ(6, emitter.frameEnd());

    f.graph->update(0.0f);
    f.sceneModel->signalEvent("detonate");
    auto particle = f.particles(emitter).front();
    EXPECT_NEAR(3.0f, glm::length(particle->velocity()), 1e-4f);
    f.graph->update(1.0f); // halfway through its life
    EXPECT_FLOAT_EQ(2.0f, particle->size().x);
    EXPECT_FLOAT_EQ(0.5f, particle->alpha());
    EXPECT_LT(glm::length(particle->color() - glm::vec3(0.0f, 1.0f, 0.0f)), 1e-5f);
    f.graph->update(1.0f);
    f.graph->update(0.0f);
    EXPECT_TRUE(f.particles(emitter).empty()) << "expired at its life expectancy";
}

TEST(EmitterAnimation, a_lightning_segment_is_as_wide_as_the_particle_size_not_the_lightning_scale) {
    // v_coldray_dur / v_stunray_dur: lightningscale 0, size 0.1. The bolt
    // runs from the emitter to the reference node under it.
    Fixture f(ModelNode::Emitter::UpdateMode::Lightning);
    f.emitter->renderMode = ModelNode::Emitter::RenderMode::Linked;
    f.rest(ControllerTypes::sizeStart, 0.1f);
    f.rest(ControllerTypes::lightingScale, 0.0f);
    f.rest(ControllerTypes::lightingSubDiv, 2.0f);
    f.emitterNode->vectorTracks()[ControllerTypes::colorStart].add(0.0f, glm::vec3(0.2f, 0.4f, 1.0f));
    f.emitterNode->floatTracks()[ControllerTypes::alphaStart].add(0.0f, 0.5f);
    auto ref = std::make_shared<ModelNode>(2, "omenref01", glm::vec3(0.0f, 0.0f, 4.0f), kIdentity, true, f.emitterNode.get());
    ref->setReference(std::make_shared<ModelNode::Reference>());
    f.emitterNode->addChild(ref);
    auto &emitter = f.build();

    f.graph->update(0.05f);
    auto segments = f.particles(emitter);
    ASSERT_EQ(3u, segments.size()) << "subdiv 2: three segments";
    float length = 0.0f;
    for (auto *segment : segments) {
        EXPECT_FLOAT_EQ(0.1f, segment->size().x);
        EXPECT_NEAR(0.4f, segment->color().y, 1e-5f);
        EXPECT_FLOAT_EQ(0.5f, segment->alpha());
        length += segment->size().y;
    }
    EXPECT_NEAR(4.0f, length, 1e-3f);
}
