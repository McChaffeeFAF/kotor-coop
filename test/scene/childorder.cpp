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

#include "reone/graphics/modelnode.h"
#include "reone/graphics/options.h"
#include "reone/scene/childlist.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/dummy.h"

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

template <class T>
std::vector<T *> listed(const ChildList<T> &list) {
    return std::vector<T *>(list.begin(), list.end());
}

} // namespace

TEST(ChildList, iterates_in_insertion_order_whatever_the_addresses) {
    int values[6] {};
    ChildList<int> list;
    // Highest address first: an unordered_set would usually not keep this.
    for (int i = 5; i >= 0; --i) {
        EXPECT_TRUE(list.insert(&values[i]).second);
    }
    EXPECT_EQ((std::vector<int *> {&values[5], &values[4], &values[3], &values[2], &values[1], &values[0]}), listed(list));
    EXPECT_EQ(6u, list.size());
}

TEST(ChildList, reinsert_keeps_place_and_erase_keeps_the_rest_in_order) {
    int a = 0, b = 0, c = 0, d = 0;
    ChildList<int> list;
    list.insert(&c);
    list.insert(&a);
    list.insert(&d);
    list.insert(&b);

    EXPECT_FALSE(list.insert(&a).second);
    EXPECT_EQ((std::vector<int *> {&c, &a, &d, &b}), listed(list));

    EXPECT_EQ(1u, list.erase(&a));
    EXPECT_EQ(0u, list.erase(&a));
    EXPECT_EQ((std::vector<int *> {&c, &d, &b}), listed(list));
    EXPECT_EQ(0u, list.count(&a));
    EXPECT_EQ(list.end(), list.find(&a));

    // Erase while iterating, as the emitter's particle recycling does.
    for (auto it = list.begin(); it != list.end();) {
        it = (*it == &d) ? list.erase(it) : std::next(it);
    }
    EXPECT_EQ((std::vector<int *> {&c, &b}), listed(list));

    // A removed child comes back at the end.
    list.insert(&a);
    EXPECT_EQ((std::vector<int *> {&c, &b, &a}), listed(list));
    EXPECT_EQ(&c, list.front());
    EXPECT_EQ(&a, list.back());

    list.clear();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(0u, list.count(&c));
}

TEST(SceneNode, children_update_and_iterate_in_the_order_they_were_added) {
    auto graphicsOpt = GraphicsOptions();
    auto pipelineFactory = MockRenderPipelineFactory();
    auto graphicsModule = TestGraphicsModule();
    graphicsModule.init();
    auto audioModule = TestAudioModule();
    audioModule.init();
    auto resourceModule = TestResourceModule();
    resourceModule.init();
    auto scene = std::make_unique<SceneGraph>("test", pipelineFactory, graphicsOpt, graphicsModule.services(), audioModule.services(), resourceModule.services());

    auto modelNode = std::make_shared<ModelNode>(0, "dummy", glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true, nullptr);
    auto parent = scene->newDummy(*modelNode);
    std::vector<std::shared_ptr<DummySceneNode>> nodes;
    for (int i = 0; i < 32; ++i) {
        nodes.push_back(scene->newDummy(*modelNode));
    }
    // Add them sorted by descending address, then by a scrambled order:
    // children() must list them as added either way.
    std::vector<SceneNode *> order;
    for (auto &node : nodes) {
        order.push_back(node.get());
    }
    std::sort(order.begin(), order.end(), std::greater<SceneNode *>());
    std::swap(order[3], order[17]);
    std::swap(order[0], order[30]);
    for (auto *node : order) {
        parent->addChild(*node);
    }
    EXPECT_EQ(order, std::vector<SceneNode *>(parent->children().begin(), parent->children().end()));

    parent->removeChild(*order[5]);
    order.erase(order.begin() + 5);
    EXPECT_EQ(order, std::vector<SceneNode *>(parent->children().begin(), parent->children().end()));
    parent->removeAllChildren();
    EXPECT_TRUE(parent->children().empty());
}
