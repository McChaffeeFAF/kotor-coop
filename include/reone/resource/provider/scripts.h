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

#pragma once

#include "reone/script/program.h"

#include "../replacements.h"
#include "../resources.h"

namespace reone {

namespace resource {

class IScripts {
public:
    virtual ~IScripts() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<script::ScriptProgram> get(const std::string &key) = 0;
};

class Scripts : public IScripts {
public:
    Scripts(IResources &resources,
            IResourceReplacements &replacements) :
        _resources(resources),
        _replacements(replacements) {
    }

    void clear() override {
        _objects.clear();
        _k2Objects.clear();
    }

    /**
     * K2's compiled scripts, for the rows the K2 systems merge appends. A
     * name with the "k2:" prefix is read from this set only (never from K1),
     * and the program keeps the prefixed name so the runner picks K2's
     * routine numbering for it. Null disables the prefix: such names then
     * resolve to nothing.
     */
    void setK2Resources(IResources *resources) {
        _k2Resources = resources;
        _k2Objects.clear();
    }

    std::shared_ptr<script::ScriptProgram> get(const std::string &key) override {
        if (key.size() > 3 && key.compare(0, 3, "k2:") == 0) {
            return getK2(key.substr(3));
        }
        ResRef resRef(key);
        ResourceId id(resRef, ResType::Ncs);
        uint64_t revision = _replacements.revision(id);
        auto maybeObject = _objects.find(resRef);
        if (maybeObject != _objects.end() && maybeObject->second.revision == revision) {
            return maybeObject->second.program;
        }
        auto program = doGet(resRef.value());
        _objects[resRef] = CachedProgram {std::move(program), revision};
        return _objects.at(resRef).program;
    }

private:
    struct CachedProgram {
        std::shared_ptr<script::ScriptProgram> program;
        uint64_t revision;
    };

    IResources &_resources;
    IResourceReplacements &_replacements;
    IResources *_k2Resources {nullptr};

    std::unordered_map<ResRef, CachedProgram> _objects;
    std::unordered_map<ResRef, std::shared_ptr<script::ScriptProgram>> _k2Objects;

    std::shared_ptr<script::ScriptProgram> doGet(std::string resRef);
    std::shared_ptr<script::ScriptProgram> getK2(const std::string &name);
};

} // namespace resource

} // namespace reone
