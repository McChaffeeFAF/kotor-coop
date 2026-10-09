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

#include "reone/resource/provider/gffs.h"

#include "reone/resource/format/gffreader.h"
#include "reone/resource/k2merge.h"
#include "reone/resource/resources.h"
#include "reone/system/stream/memoryinput.h"

namespace reone {

namespace resource {

// A K2 resource's localized strings are K2 strrefs: flag them so they
// resolve through K2's talk table (the K2 systems merge).
static void flagK2StrRefs(Gff &gff) {
    for (auto &field : gff.fields()) {
        if (field.type == Gff::FieldType::CExoLocString && field.intValue >= 0) {
            field.intValue = toK2StrRef(field.intValue);
        }
        for (auto &child : field.children) {
            if (child) {
                flagK2StrRefs(*child);
            }
        }
    }
}

std::shared_ptr<Gff> Gffs::get(const std::string &resRef, ResType type) {
    if (resRef.size() > 3 && resRef.compare(0, 3, "k2:") == 0) {
        if (!_k2Resources) {
            return nullptr;
        }
        ResourceId k2Id(resRef.substr(3), type);
        return _k2Cache.getOrAdd(k2Id, [this, &k2Id]() {
            auto res = _k2Resources->find(k2Id);
            if (!res) {
                return std::shared_ptr<Gff>();
            }
            MemoryInputStream stream(res->data);
            GffReader reader(stream);
            reader.load();
            auto root = reader.root();
            if (root) {
                flagK2StrRefs(*root);
            }
            return root;
        });
    }
    ResourceId resId(resRef, type);
    return _cache.getOrAdd(resId, [this, &resId]() {
        auto res = _resources.find(resId);
        if (!res) {
            return std::shared_ptr<Gff>();
        }
        MemoryInputStream stream(res->data);
        GffReader reader(stream);
        reader.load();
        return reader.root();
    });
}

} // namespace resource

} // namespace reone
