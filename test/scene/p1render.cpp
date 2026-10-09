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
#include "reone/scene/particleutil.h"
#include "reone/system/renderdiag.h"

using namespace reone;
using namespace reone::scene;

namespace {

ObjectLightSource makeLight(glm::vec3 position, float radius, int priority = 1, bool ambientOnly = false) {
    ObjectLightSource light;
    light.position = position;
    light.color = glm::vec3(1.0f);
    light.radius = radius;
    light.priority = priority;
    light.dynamicType = 2;
    light.ambientOnly = ambientOnly;
    return light;
}

} // namespace

// Character generation's cgbody_light: the ambient light's radius (1.2 m) does
// not reach the model's centre, yet on a GUI stage it still lights the model.
TEST(P1Render, gui_stage_uses_lights_out_of_reach) {
    std::vector<ObjectLightSource> lights {
        makeLight(glm::vec3(0.86f, 0.84f, 2.05f), 20.0f, 1),
        makeLight(glm::vec3(0.49f, -1.34f, -0.37f), 15.0f, 1),
        makeLight(glm::vec3(1.26f, 0.08f, 0.85f), 1.216f, 2, true)};
    glm::vec3 center(0.0f, 0.0f, 2.5f);

    auto inWorld = selectObjectLights(lights, center);
    auto onStage = selectObjectLights(lights, center, graphics::kMaxObjectLights, false);

    EXPECT_EQ(2u, inWorld.size());
    ASSERT_EQ(3u, onStage.size());
    EXPECT_EQ(2, onStage[2]);
}

TEST(P1Render, gui_stage_keeps_the_light_order_and_budget) {
    std::vector<ObjectLightSource> lights {
        makeLight(glm::vec3(100.0f, 0.0f, 0.0f), 1.0f, 5),
        makeLight(glm::vec3(50.0f, 0.0f, 0.0f), 1.0f, 3),
        makeLight(glm::vec3(10.0f, 0.0f, 0.0f), 1.0f, 1),
        makeLight(glm::vec3(20.0f, 0.0f, 0.0f), 1.0f, 2)};

    auto onStage = selectObjectLights(lights, glm::vec3(0.0f), 3, false);

    ASSERT_EQ(3u, onStage.size());
    EXPECT_EQ(2, onStage[0]);
    EXPECT_EQ(3, onStage[1]);
    EXPECT_EQ(1, onStage[2]);
}

// A blaster bolt (w_laserfire_r): 9 cm particle, blur length 0.01, moving at
// 16 m/s. The streak covers at least one frame of travel.
TEST(P1Render, short_blur_streaks_at_least_one_frame_of_travel) {
    auto basis = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f),
        glm::vec3(0.0f, 16.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.09f,
        0.01f,
        1.0f / 60.0f);

    EXPECT_NEAR(0.09f + 16.0f / 60.0f, 0.09f * basis.lengthScale, 1e-4f);
    EXPECT_NEAR(1.0f, basis.up.y, 1e-5f);
}

TEST(P1Render, long_blur_is_unchanged_by_the_frame_floor) {
    auto withFloor = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, 1.0f, 1.0f / 60.0f);
    auto without = particleutil::buildMotionBlurBasis(
        glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, 1.0f);

    EXPECT_NEAR(without.lengthScale, withFloor.lengthScale, 1e-6f);
}

TEST(P1Render, render_diagnostics_report_each_key_once_and_count_all) {
    renderdiag::reset();

    EXPECT_TRUE(renderdiag::report("texture", "fx_missing", "Texture not found: fx_missing"));
    EXPECT_FALSE(renderdiag::report("texture", "fx_missing", "Texture not found: fx_missing"));
    EXPECT_TRUE(renderdiag::report("texture", "fx_other", "Texture not found: fx_other"));

    EXPECT_EQ(3, renderdiag::count("texture"));
    EXPECT_EQ(2, renderdiag::distinct("texture"));
    EXPECT_TRUE(renderdiag::reported("texture", "fx_missing"));
    EXPECT_FALSE(renderdiag::reported("model", "fx_missing"));
    EXPECT_EQ("{\"texture\":{\"count\":3,\"distinct\":2,\"keys\":[\"fx_missing\",\"fx_other\"]}}", renderdiag::summaryJson());

    renderdiag::reset();
    EXPECT_EQ(0, renderdiag::count("texture"));
}
