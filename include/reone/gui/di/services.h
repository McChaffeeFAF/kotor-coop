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

namespace reone {

namespace resource {

struct ResourceServices;

}

namespace gui {

class IGUIs;

struct GUIServices {
    IGUIs &guis;

    /// GUIs read from the K2 GUI resource set, when one is configured.
    IGUIs *k2Guis {nullptr};
    /// The K2 GUI resource view the K2 GUIs are built over, when configured.
    resource::ResourceServices *k2Resources {nullptr};
    /// K2 models, world textures, 2DAs and music for the main-menu scene, when configured.
    resource::ResourceServices *k2SceneResources {nullptr};

    GUIServices(IGUIs &guis,
                IGUIs *k2Guis = nullptr,
                resource::ResourceServices *k2Resources = nullptr,
                resource::ResourceServices *k2SceneResources = nullptr) :
        guis(guis),
        k2Guis(k2Guis),
        k2Resources(k2Resources),
        k2SceneResources(k2SceneResources) {
    }
};

} // namespace gui

} // namespace reone
