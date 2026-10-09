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

/**
 * The K2 presentation resource set: which source answers a GUI lookup when a
 * K1 session presents the K2 HUD, and what the set can never answer.
 */

#include <gtest/gtest.h>

#include "reone/resource/presentationresources.h"
#include "reone/resource/replacementresources.h"
#include "reone/resource/replacements.h"

using namespace reone;
using namespace reone::resource;

namespace {

class MemoryContainer : public IResourceContainer {
public:
    MemoryContainer &add(const std::string &resRef, ResType type, const std::string &value) {
        ResourceId id(resRef, type);
        _data[id] = ByteBuffer(value.begin(), value.end());
        _ids.insert(id);
        return *this;
    }

    std::optional<ByteBuffer> findResourceData(const ResourceId &id) override {
        auto it = _data.find(id);
        if (it == _data.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    const std::unordered_set<ResourceId> &resourceIds() const override { return _ids; }

private:
    std::unordered_map<ResourceId, ByteBuffer> _data;
    std::unordered_set<ResourceId> _ids;
};

std::unique_ptr<IResourceContainer> filtered(std::unique_ptr<MemoryContainer> inner,
                                             std::set<ResType> types = presentationResourceTypes()) {
    return std::make_unique<TypeFilteredResourceContainer>(std::move(inner), std::move(types));
}

std::string text(const std::optional<Resource> &resource) {
    if (!resource) {
        return "<none>";
    }
    return std::string(resource->data.begin(), resource->data.end());
}

class PresentationResourcesTest : public testing::Test {
protected:
    // The main (K1) sources.
    Resources k1;
    std::unique_ptr<LayeredPresentationResources> k2;

    void SetUp() override {
        auto k1Pack = std::make_unique<MemoryContainer>();
        k1Pack->add("lbl_mibox00", ResType::Tpc, "k1:lbl_mibox00")
            .add("po_pcarth", ResType::Tpc, "k1:po_pcarth")
            .add("mipc28x6", ResType::Gui, "k1:mipc28x6")
            .add("baseitems", ResType::TwoDA, "k1:baseitems")
            .add("iw_blstrpstl", ResType::Tpc, "k1:iw_blstrpstl");
        k1.add(std::move(k1Pack), ResourceOwner::Global, ResourceSourceBucket::EncapsulatedClass2);

        // The K2 set, mounted as mountK2PresentationSources does: KEY with
        // GUI entries only, the GUI pack, then override.
        auto set = std::make_unique<Resources>();
        auto key = std::make_unique<MemoryContainer>();
        key->add("mipc28x6_p", ResType::Gui, "k2key:mipc28x6_p")
            .add("equip_p", ResType::Gui, "k2key:equip_p")
            .add("baseitems", ResType::TwoDA, "k2key:baseitems")
            .add("keyonly", ResType::Tpc, "k2key:keyonly");
        set->add(filtered(std::move(key), {ResType::Gui}), ResourceOwner::Global, ResourceSourceBucket::KeyBif);

        auto pack = std::make_unique<MemoryContainer>();
        pack->add("lbl_mibox00", ResType::Tpc, "k2pack:lbl_mibox00")
            .add("i_swtchwpn1_p", ResType::Tpc, "k2pack:i_swtchwpn1_p")
            .add("uibit_brdr_man1p", ResType::Tpc, "k2pack:uibit_brdr_man1p")
            .add("iw_blstrpstl", ResType::Tpc, "k2pack:iw_blstrpstl");
        set->add(filtered(std::move(pack)), ResourceOwner::Global, ResourceSourceBucket::EncapsulatedClass2);

        auto override = std::make_unique<MemoryContainer>();
        override->add("equip_p", ResType::Gui, "k2override:equip_p")
            .add("uibit_brdr_man1p", ResType::Tga, "k2override:uibit_brdr_man1p")
            .add("lbl_mibox00", ResType::Tpc, "k2override:lbl_mibox00")
            .add("appearance", ResType::TwoDA, "k2override:appearance");
        set->add(filtered(std::move(override)), ResourceOwner::Global, ResourceSourceBucket::LooseDirectory);

        k2 = std::make_unique<LayeredPresentationResources>(std::move(set), k1);
    }
};

} // namespace

TEST_F(PresentationResourcesTest, k2_art_wins_over_k1_art_of_the_same_name) {
    // The override outranks the pack, and both outrank K1.
    EXPECT_EQ("k2override:lbl_mibox00", text(k2->find(ResourceId("lbl_mibox00", ResType::Tpc))));
    EXPECT_EQ("k2pack:i_swtchwpn1_p", text(k2->find(ResourceId("i_swtchwpn1_p", ResType::Tpc))));
}

TEST_F(PresentationResourcesTest, aspyr_override_gui_wins_over_the_key_copy) {
    EXPECT_EQ("k2override:equip_p", text(k2->find(ResourceId("equip_p", ResType::Gui))));
    EXPECT_EQ("k2key:mipc28x6_p", text(k2->find(ResourceId("mipc28x6_p", ResType::Gui))));
}

TEST_F(PresentationResourcesTest, misses_fall_back_to_k1) {
    EXPECT_EQ("k1:po_pcarth", text(k2->find(ResourceId("po_pcarth", ResType::Tpc))));
    EXPECT_EQ("k1:mipc28x6", text(k2->find(ResourceId("mipc28x6", ResType::Gui))));
    EXPECT_EQ("<none>", text(k2->find(ResourceId("nowhere", ResType::Tpc))));
    EXPECT_THROW(k2->get(ResourceId("nowhere", ResType::Tpc)), std::exception);
}

TEST_F(PresentationResourcesTest, non_presentation_types_never_come_from_k2) {
    // K2 data never reaches a K1 session through the presentation set, even
    // when the set's KEY or override holds a resource of that name.
    EXPECT_EQ("k1:baseitems", text(k2->find(ResourceId("baseitems", ResType::TwoDA))));
    EXPECT_EQ("<none>", text(k2->find(ResourceId("appearance", ResType::TwoDA))));
    // The KEY serves GUI files only.
    EXPECT_EQ("<none>", text(k2->find(ResourceId("keyonly", ResType::Tpc))));
}

TEST_F(PresentationResourcesTest, the_main_sources_never_see_k2) {
    EXPECT_EQ("k1:lbl_mibox00", text(k1.find(ResourceId("lbl_mibox00", ResType::Tpc))));
    EXPECT_EQ("k1:iw_blstrpstl", text(k1.find(ResourceId("iw_blstrpstl", ResType::Tpc))));
    EXPECT_EQ("<none>", text(k1.find(ResourceId("mipc28x6_p", ResType::Gui))));
}

TEST_F(PresentationResourcesTest, retiring_the_set_leaves_k1_intact) {
    k2->clear();
    EXPECT_EQ("k1:lbl_mibox00", text(k2->find(ResourceId("lbl_mibox00", ResType::Tpc))));
    EXPECT_EQ("<none>", text(k2->find(ResourceId("i_swtchwpn1_p", ResType::Tpc))));
    EXPECT_EQ("k1:po_pcarth", text(k1.find(ResourceId("po_pcarth", ResType::Tpc))));
}

TEST_F(PresentationResourcesTest, filtered_container_lists_only_allowed_ids) {
    auto inner = std::make_unique<MemoryContainer>();
    inner->add("a", ResType::Gui, "a").add("b", ResType::TwoDA, "b");
    TypeFilteredResourceContainer container(std::move(inner), {ResType::Gui});
    EXPECT_EQ(1u, container.resourceIds().size());
    EXPECT_EQ(1u, container.resourceIds().count(ResourceId("a", ResType::Gui)));
    EXPECT_FALSE(container.findResourceData(ResourceId("b", ResType::TwoDA)));
}

TEST_F(PresentationResourcesTest, replacements_apply_to_the_set) {
    ResourceReplacements replacements;
    auto set = std::make_unique<Resources>();
    auto pack = std::make_unique<MemoryContainer>();
    pack->add("lbl_mibox00", ResType::Tpc, "k2pack:lbl_mibox00");
    set->add(filtered(std::move(pack)), ResourceOwner::Global, ResourceSourceBucket::EncapsulatedClass2);
    LayeredPresentationResources layered(
        std::make_unique<ReplacementResources>(std::move(set), replacements), k1);
    std::string value = "harness:lbl_mibox00";
    replacements.replaceResource(ResourceId("lbl_mibox00", ResType::Tpc), ByteBuffer(value.begin(), value.end()));
    EXPECT_EQ("harness:lbl_mibox00", text(layered.find(ResourceId("lbl_mibox00", ResType::Tpc))));
}

TEST(PresentationResources, mount_rejects_a_directory_that_is_not_a_k2_install) {
    auto dir = std::filesystem::temp_directory_path() / "reone_not_k2_install";
    std::filesystem::create_directories(dir);
    Resources set;
    EXPECT_FALSE(isK2Install(dir));
    EXPECT_FALSE(isK2Install(""));
    EXPECT_THROW(mountK2PresentationSources(set, dir), std::runtime_error);
    std::filesystem::remove_all(dir);
}
