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

#include <gtest/gtest.h>

#include "reone/scene/objectlights.h"

using namespace reone;
using namespace reone::scene;

namespace {

ObjectLightSource light(glm::vec3 position, float radius, int priority, int dynamicType, bool affectDynamic = false) {
    ObjectLightSource l;
    l.position = position;
    l.color = glm::vec3(1.0f);
    l.radius = radius;
    l.priority = priority;
    l.dynamicType = dynamicType;
    l.affectDynamic = affectDynamic;
    return l;
}

} // namespace

TEST(ObjectLights, VC_M_affectdynamic_light_with_neither_dynamic_flag_never_lights_an_object) {
    // K1 room lights are dynamic type 2 with affect-dynamic 0; a type-0 light
    // needs the affect-dynamic flag to reach a dynamic object.
    std::vector<ObjectLightSource> lights {
        light({1.0f, 0.0f, 0.0f}, 6.0f, 1, 0, false), // baked only
        light({2.0f, 0.0f, 0.0f}, 6.0f, 5, 0, true),  // affect-dynamic flag only: ranks after type 2
        light({3.0f, 0.0f, 0.0f}, 6.0f, 5, 2, false)  // dynamic type 2
    };
    auto selected = selectObjectLights(lights, glm::vec3(0.0f));
    ASSERT_EQ(2ull, selected.size());
    EXPECT_EQ(2, selected[0]); // same priority: dynamic type 2 ranks before type 0
    EXPECT_EQ(1, selected[1]);
}

TEST(ObjectLights, VC_M_priority_character_gets_three_lights_by_priority_then_type_then_distance) {
    std::vector<ObjectLightSource> lights {
        light({0.5f, 0.0f, 0.0f}, 6.0f, 5, 2), // 0: nearest, lowest priority
        light({4.0f, 0.0f, 0.0f}, 6.0f, 3, 2), // 1
        light({2.0f, 0.0f, 0.0f}, 6.0f, 4, 2), // 2
        light({3.0f, 0.0f, 0.0f}, 6.0f, 4, 1), // 3: same priority as 2, type 1 wins
        light({5.0f, 0.0f, 0.0f}, 6.0f, 4, 2), // 4: same as 2 but farther
        light({1.0f, 0.0f, 0.0f}, 6.0f, 1, 2, true)};
    lights[5].position = glm::vec3(30.0f, 0.0f, 0.0f); // 5: top priority, out of range
    auto selected = selectObjectLights(lights, glm::vec3(0.0f));
    ASSERT_EQ(3ull, selected.size());
    EXPECT_EQ(1, selected[0]);
    EXPECT_EQ(3, selected[1]);
    EXPECT_EQ(2, selected[2]);
}

TEST(ObjectLights, VC_M_priority_light_must_reach_the_object_centre) {
    std::vector<ObjectLightSource> lights {
        light({6.0f, 0.0f, 0.0f}, 6.0f, 5, 2),  // exactly at the radius
        light({6.01f, 0.0f, 0.0f}, 6.0f, 5, 2), // just outside
        light({0.0f, 0.0f, 0.0f}, 0.0f, 5, 2)}; // zero radius
    lights.push_back(light({1.0f, 0.0f, 0.0f}, 6.0f, 5, 2));
    lights.back().color = glm::vec3(0.0f); // black
    auto selected = selectObjectLights(lights, glm::vec3(0.0f));
    ASSERT_EQ(1ull, selected.size());
    EXPECT_EQ(0, selected[0]);
}

TEST(ObjectLights, VC_M_affectdynamic_attenuation_falls_to_about_a_fifth_at_the_radius) {
    EXPECT_FLOAT_EQ(1.0f, objectLightAttenuation(6.0f, 0.0f));
    EXPECT_NEAR(1.0f / 5.5f, objectLightAttenuation(6.0f, 6.0f), 1e-6f);
    EXPECT_NEAR(1.0f / 2.125f, objectLightAttenuation(8.0f, 4.0f), 1e-6f);
}

// Scene-level: lights on a room model reach a creature model through the
// scene graph, and only the dynamic-flagged ones do.

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/model.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;

namespace {

struct LitSceneFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::vector<std::unique_ptr<Model>> models;

    LitSceneFixture() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());
    }

    std::shared_ptr<ModelNode> root(const std::string &name) {
        return std::make_shared<ModelNode>(0, name, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    }

    void addLight(ModelNode &parent, uint16_t number, const std::string &name, glm::vec3 position,
                  int priority, int dynamicType, bool affectDynamic) {
        auto node = std::make_shared<ModelNode>(number, name, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, &parent);
        auto light = std::make_shared<ModelNode::Light>();
        light->priority = priority;
        light->dynamicType = dynamicType;
        light->affectDynamic = affectDynamic;
        node->setLight(light);
        parent.addChild(node);
    }

    std::shared_ptr<ModelSceneNode> build(std::shared_ptr<ModelNode> rootNode, ModelUsage usage) {
        models.push_back(std::make_unique<Model>(rootNode->name(), 0, rootNode,
                                                 std::vector<std::shared_ptr<Animation>>(), "", 1.0f));
        auto node = scene->newModel(*models.back(), usage);
        node->init();
        node->setLocalTransform(glm::mat4(1.0f));
        return node;
    }
};

} // namespace

TEST(ObjectLights, VC_M_affectdynamic_VC_M_priority_character_is_lit_only_by_dynamic_room_lights_nearest_best_three) {
    // given: a room with six lights around a creature at the origin
    LitSceneFixture f;
    auto roomRoot = f.root("room");
    f.addLight(*roomRoot, 1, "baked", {1.0f, 0.0f, 0.0f}, 1, 0, false);      // lightmap only: never
    f.addLight(*roomRoot, 2, "dyn_near", {0.0f, 1.0f, 0.0f}, 5, 2, false);   // priority 5: fourth
    f.addLight(*roomRoot, 3, "aff_only", {0.0f, 2.0f, 0.0f}, 4, 0, true);    // affect-dynamic flag only: ranks after type 2
    f.addLight(*roomRoot, 4, "dyn_p3", {3.0f, 0.0f, 0.0f}, 3, 2, false);     // best priority
    f.addLight(*roomRoot, 5, "dyn_p4", {0.0f, 4.0f, 0.0f}, 4, 2, false);     // type 2 beats aff_only at the same priority
    f.addLight(*roomRoot, 6, "far", {0.0f, 0.0f, 40.0f}, 1, 2, false);       // out of reach
    auto room = f.build(roomRoot, ModelUsage::Room);
    for (auto name : {"baked", "dyn_near", "aff_only", "dyn_p3", "dyn_p4", "far"}) {
        auto &light = *static_cast<LightSceneNode *>(room->getNodeByName(name));
        light.setColor(glm::vec3(1.0f));
        light.setRadius(6.0f);
        light.setMultiplier(1.0f);
    }
    f.scene->addRoot(room);
    auto creature = f.build(f.root("creature"), ModelUsage::Creature);
    f.scene->addRoot(creature);

    // when
    f.scene->updateObjectLighting(0.0f);

    // then
    ASSERT_TRUE(creature->hasObjectLights());
    const auto &lights = creature->objectLights();
    ASSERT_EQ(3ull, lights.size());
    EXPECT_EQ(glm::vec3(3.0f, 0.0f, 0.0f), lights[0].position);
    EXPECT_EQ(glm::vec3(0.0f, 4.0f, 0.0f), lights[1].position);
    EXPECT_EQ(glm::vec3(0.0f, 2.0f, 0.0f), lights[2].position);
    EXPECT_FALSE(room->hasObjectLights()); // rooms keep the scene light list and their lightmaps
}

TEST(ObjectLights, VC_M_affectdynamic_attached_model_is_lit_by_its_carrier) {
    LitSceneFixture f;
    auto bodyRoot = f.root("body");
    auto hook = std::make_shared<ModelNode>(1, "headhook", glm::vec3(0.0f, 0.0f, 1.5f),
                                            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, bodyRoot.get());
    bodyRoot->addChild(hook);
    auto body = f.build(bodyRoot, ModelUsage::Creature);
    auto head = f.build(f.root("head"), ModelUsage::Creature);
    body->attach("headhook", *head);
    EXPECT_EQ(body.get(), &head->lightingRoot());
    EXPECT_EQ(body.get(), &body->lightingRoot());
}
