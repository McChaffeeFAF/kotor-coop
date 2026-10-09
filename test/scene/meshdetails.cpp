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

// V04 mesh and light details: object lighting reaching the draw call,
// UV jitter, transparency-hint order, every lens flare, reattachable references.

#include <gtest/gtest.h>

#include "reone/graphics/animation.h"
#include "reone/graphics/material.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/graphics/texture.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/light.h"
#include "reone/scene/node/mesh.h"
#include "reone/scene/node/model.h"

#include "../fixtures/audio.h"
#include "../fixtures/graphics.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"

using namespace reone;
using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

using testing::_;
using testing::Return;

namespace {

class RecordingRenderPass : public IRenderPass {
public:
    struct Draw {
        bool objectLighting;
        int numObjectLights;
        glm::vec3 firstLightColor;
        float uvJitterAmplitude;
        float uvJitterSpeed;
    };

    void draw(Mesh &, Material &material, const glm::mat4 &, const glm::mat4 &) override {
        draws.push_back({material.objectLighting,
                         material.numObjectLights,
                         material.objectLights[0].color,
                         material.uvJitterAmplitude,
                         material.uvJitterSpeed});
    }
    void drawSkinned(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::mat4> &) override {}
    void drawDangly(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const std::vector<glm::vec4> &) override {}
    void drawSaber(Mesh &, Material &, const glm::mat4 &, const glm::mat4 &, const glm::vec4 &) override {}
    void drawBillboard(Texture &, const glm::vec4 &, const glm::mat4 &, const glm::mat4 &, std::optional<float>) override {}
    void drawParticles(Texture &, FaceCullMode, bool, const glm::ivec2 &, const std::vector<ParticleInstance> &) override {}
    void drawGrass(float, float, Texture &, std::optional<std::reference_wrapper<Texture>> &, const std::vector<GrassInstance> &) override {}
    void drawAABB(const std::vector<glm::vec4> &) override {}
    void drawImage(Texture &, const glm::ivec2 &, const glm::ivec2 &, glm::vec4, glm::mat3x4, ImageAlphaMode) override {}

    std::vector<Draw> draws;
};

struct DetailsFixture {
    GraphicsOptions graphicsOpt;
    MockRenderPipelineFactory pipelineFactory;
    TestGraphicsModule graphicsModule;
    TestAudioModule audioModule;
    TestResourceModule resourceModule;
    std::unique_ptr<SceneGraph> scene;
    std::shared_ptr<Texture> texture;
    std::vector<std::unique_ptr<Model>> models;

    DetailsFixture() {
        graphicsModule.init();
        audioModule.init();
        resourceModule.init();
        scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt,
                                             graphicsModule.services(),
                                             audioModule.services(),
                                             resourceModule.services());
        texture = std::make_shared<Texture>("tex", TextureType::TwoDim, Texture::Properties());
        EXPECT_CALL(resourceModule.textures(), get(_, _)).WillRepeatedly(Return(texture));
    }

    static std::shared_ptr<ModelNode> node(uint16_t number, const std::string &name, ModelNode *parent) {
        return std::make_shared<ModelNode>(number, name, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, parent);
    }

    std::shared_ptr<ModelNode::TriangleMesh> addMesh(ModelNode &root, uint16_t number, const std::string &name, int transparency = 0) {
        auto mesh = std::make_shared<ModelNode::TriangleMesh>();
        mesh->render = true;
        mesh->diffuseMap = "tex";
        mesh->transparency = transparency;
        Mesh::VertexLayout layout;
        layout.stride = 3 * sizeof(float);
        layout.offPosition = 0;
        mesh->mesh = std::make_shared<Mesh>(std::vector<float> {0.0f, 0.0f, 0.0f}, layout, std::vector<Mesh::Face>());
        auto meshNode = node(number, name, &root);
        meshNode->setMesh(mesh);
        root.addChild(meshNode);
        return mesh;
    }

    std::shared_ptr<ModelSceneNode> build(std::shared_ptr<ModelNode> root, ModelUsage usage) {
        models.push_back(std::make_unique<Model>(root->name(), 0, root, std::vector<std::shared_ptr<Animation>>(), "", 1.0f));
        auto model = scene->newModel(*models.back(), usage);
        model->init();
        model->setLocalTransform(glm::mat4(1.0f));
        return model;
    }
};

} // namespace

TEST(MeshLightDetails, VC_M_affectdynamic_creature_mesh_draws_with_its_own_lights_room_mesh_does_not) {
    DetailsFixture f;
    auto creatureRoot = DetailsFixture::node(0, "creature", nullptr);
    f.addMesh(*creatureRoot, 1, "body");
    auto creature = f.build(creatureRoot, ModelUsage::Creature);
    MaterialLight light;
    light.position = glm::vec3(1.0f, 0.0f, 1.0f);
    light.color = glm::vec3(0.5f, 0.25f, 0.125f);
    light.radius = 6.0f;
    creature->setObjectLights({light});

    auto roomRoot = DetailsFixture::node(0, "room", nullptr);
    f.addMesh(*roomRoot, 1, "floor");
    auto room = f.build(roomRoot, ModelUsage::Room);
    room->getNodeByName("floor")->setStatic(true);

    RecordingRenderPass pass;
    static_cast<MeshSceneNode *>(creature->getNodeByName("body"))->render(pass);
    static_cast<MeshSceneNode *>(room->getNodeByName("floor"))->render(pass);

    ASSERT_EQ(2ull, pass.draws.size());
    EXPECT_TRUE(pass.draws[0].objectLighting);
    EXPECT_EQ(1, pass.draws[0].numObjectLights);
    EXPECT_EQ(glm::vec3(0.5f, 0.25f, 0.125f), pass.draws[0].firstLightColor);
    EXPECT_FALSE(pass.draws[1].objectLighting);
}

TEST(MeshLightDetails, VC_M_uvjitter_jitter_reaches_the_draw_as_half_amplitude_and_speed) {
    DetailsFixture f;
    auto root = DetailsFixture::node(0, "forcefield", nullptr);
    auto jittery = f.addMesh(*root, 1, "field");
    jittery->uvAnimation.dir = glm::vec2(0.0f, 0.01f);
    jittery->uvAnimation.jitter = 0.4f;
    jittery->uvAnimation.jitterSpeed = 0.2f;
    auto still = f.addMesh(*root, 2, "frame");
    still->uvAnimation.jitter = 0.01f; // K1's common case: jitter without speed never moves
    auto model = f.build(root, ModelUsage::Placeable);

    RecordingRenderPass pass;
    static_cast<MeshSceneNode *>(model->getNodeByName("field"))->render(pass);
    static_cast<MeshSceneNode *>(model->getNodeByName("frame"))->render(pass);

    ASSERT_EQ(2ull, pass.draws.size());
    EXPECT_FLOAT_EQ(0.2f, pass.draws[0].uvJitterAmplitude);
    EXPECT_FLOAT_EQ(0.2f, pass.draws[0].uvJitterSpeed);
    EXPECT_FLOAT_EQ(0.0f, pass.draws[1].uvJitterAmplitude);
}

TEST(MeshLightDetails, VC_M_transhint_transparent_meshes_draw_in_ascending_hint_keeping_ties_in_order) {
    DetailsFixture f;
    auto root = DetailsFixture::node(0, "glass", nullptr);
    f.addMesh(*root, 1, "a", 7);
    f.addMesh(*root, 2, "b", 1);
    f.addMesh(*root, 3, "c", 7);
    f.addMesh(*root, 4, "d", 0);
    auto model = f.build(root, ModelUsage::Placeable);
    std::vector<MeshSceneNode *> meshes;
    for (auto name : {"a", "b", "c", "d"}) {
        meshes.push_back(static_cast<MeshSceneNode *>(model->getNodeByName(name)));
    }

    sortByTransparencyHint(meshes);

    std::vector<std::string> order;
    for (auto mesh : meshes) {
        order.push_back(mesh->modelNode().name());
    }
    EXPECT_EQ((std::vector<std::string> {"d", "b", "a", "c"}), order);
}

TEST(MeshLightDetails, VC_M_multiflare_flares_line_up_between_screen_centre_and_light) {
    glm::vec3 camera(0.0f, 0.0f, 0.0f);
    glm::vec3 forward(0.0f, 1.0f, 0.0f);
    glm::vec3 light(4.0f, 10.0f, 2.0f);
    // position 1 is the light, 0 the camera axis at the light's depth
    EXPECT_EQ(light, lensFlarePosition(light, camera, forward, 1.0f));
    EXPECT_EQ(glm::vec3(0.0f, 10.0f, 0.0f), lensFlarePosition(light, camera, forward, 0.0f));
    auto mid = lensFlarePosition(light, camera, forward, 0.5f);
    EXPECT_FLOAT_EQ(2.0f, mid.x);
    EXPECT_FLOAT_EQ(10.0f, mid.y);
    EXPECT_FLOAT_EQ(1.0f, mid.z);
}

TEST(MeshLightDetails, VC_M_reattach_only_a_reattachable_reference_moves_to_another_node) {
    DetailsFixture f;
    auto refRoot = DetailsFixture::node(0, "fx_ref", nullptr);
    auto refModel = std::make_shared<Model>("fx_ref", 0, refRoot, std::vector<std::shared_ptr<Animation>>(), "", 1.0f);
    EXPECT_CALL(f.resourceModule.models(), get("fx_ref")).WillRepeatedly(Return(refModel));

    auto root = DetailsFixture::node(0, "v_coldray_dur", nullptr);
    for (auto [number, name, reattachable] : std::vector<std::tuple<uint16_t, std::string, bool>> {
             {1, "omenref01", true}, {2, "omenref02", false}}) {
        auto refNode = DetailsFixture::node(number, name, root.get());
        auto reference = std::make_shared<ModelNode::Reference>();
        reference->modelName = "fx_ref";
        reference->reattachable = reattachable;
        refNode->setReference(reference);
        root->addChild(refNode);
    }
    auto effect = f.build(root, ModelUsage::Creature);
    auto target = f.build(DetailsFixture::node(0, "target", nullptr), ModelUsage::Creature);
    auto movable = effect->getAttachment("omenref01");
    auto fixed = effect->getAttachment("omenref02");
    ASSERT_TRUE(movable);
    ASSERT_TRUE(fixed);

    EXPECT_TRUE(effect->reattachReference("omenref01", *target));
    EXPECT_FALSE(effect->reattachReference("omenref02", *target));

    EXPECT_EQ(target.get(), movable->parent());
    EXPECT_EQ(effect->getNodeByName("omenref02"), fixed->parent());
}
