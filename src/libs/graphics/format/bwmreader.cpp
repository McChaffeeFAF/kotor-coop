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

#include "reone/graphics/format/bwmreader.h"

#include "reone/graphics/walkmesh.h"
#include "reone/system/checkutil.h"

namespace reone {

namespace graphics {

void BwmReader::load() {
    checkEqual("BWM signature", _bwm.readString(8), std::string("BWM V1.0", 8));

    _type = static_cast<WalkmeshType>(_bwm.readUint32());

    std::vector<float> relUsePosition1(_bwm.readFloatArray(3));
    std::vector<float> relUsePosition2(_bwm.readFloatArray(3));
    std::vector<float> absUsePosition1(_bwm.readFloatArray(3));
    std::vector<float> absUsePosition2(_bwm.readFloatArray(3));

    std::vector<float> position(_bwm.readFloatArray(3));
    _position = glm::make_vec3(&position[0]);

    // The header, use points included, is kept even for a walkmesh without
    // faces (upstream #356).
    _walkmesh = std::make_shared<Walkmesh>();
    _walkmesh->_area = _type == WalkmeshType::WOK;
    _walkmesh->relativeUsePositions = {glm::make_vec3(relUsePosition1.data()), glm::make_vec3(relUsePosition2.data())};
    _walkmesh->absoluteUsePositions = {glm::make_vec3(absUsePosition1.data()), glm::make_vec3(absUsePosition2.data())};
    _walkmesh->position = _position;
    for (const auto *use : {&relUsePosition1, &relUsePosition2}) {
        glm::vec3 point(glm::make_vec3(use->data()));
        if (point != glm::vec3(0.0f)) {
            _walkmesh->usePositions.push_back(point);
        }
    }

    _numVertices = _bwm.readUint32();
    if (_numVertices == 0) {
        return;
    }

    _offVertices = _bwm.readUint32();
    _numFaces = _bwm.readUint32();
    _offFaces = _bwm.readUint32();
    _offMaterials = _bwm.readUint32();
    _offNormals = _bwm.readUint32();
    _offPlanarDistances = _bwm.readUint32();

    if (_type == WalkmeshType::WOK) {
        _numAabb = _bwm.readUint32();
        _offAabb = _bwm.readUint32();

        _bwm.skipBytes(4); // unknown

        _numAdjacencies = _bwm.readUint32();
        _offAdjacencies = _bwm.readUint32();
        _numEdges = _bwm.readUint32();
        _offsetEdges = _bwm.readUint32();
        _numPerimeters = _bwm.readUint32();
        _offPerimeters = _bwm.readUint32();
    }

    loadVertices();
    loadFaces();
    loadMaterials();
    loadNormals();

    _walkmesh->verify();

    if (_type == WalkmeshType::WOK) {
        loadAABB();
    }
}

void BwmReader::loadVertices() {
    _bwm.seek(_offVertices);
    auto &array = _walkmesh->vertices;
    _bwm.checkCount(_numVertices);
    array.reserve(_numVertices);
    // A door or placeable walkmesh places its vertices by its position offset
    // (upstream #356): K1's open-door walkmeshes are authored at the hinge
    // origin and offset to the jamb (dor_lhr011: -2.79 m). An area
    // walkmesh's vertices are already where they lie.
    const glm::vec3 offset = _type == WalkmeshType::PWK_DWK ? _position : glm::vec3(0.0f);
    for (uint32_t i = 0; i < _numVertices; ++i) {
        float x = _bwm.readFloat() + offset.x;
        float y = _bwm.readFloat() + offset.y;
        float z = _bwm.readFloat() + offset.z;
        array.emplace_back(x, y, z);
    }
}

void BwmReader::loadFaces() {
    _bwm.seek(_offFaces);
    auto &array = _walkmesh->faces;
    _bwm.checkCount(_numFaces);
    array.reserve(_numFaces);
    for (uint32_t i = 0; i < _numFaces; ++i) {
        uint32_t v0 = _bwm.readUint32();
        uint32_t v1 = _bwm.readUint32();
        uint32_t v2 = _bwm.readUint32();
        Walkmesh::FaceVertices face = {{v0, v1, v2}};
        array.emplace_back(face);
    }
}

void BwmReader::loadMaterials() {
    _bwm.seek(_offMaterials);
    auto &array = _walkmesh->materials;
    _bwm.checkCount(_numFaces);
    array.reserve(_numFaces);
    for (uint32_t i = 0; i < _numFaces; ++i) {
        array.emplace_back(_bwm.readUint32());
    }
}

void BwmReader::loadNormals() {
    _bwm.seek(_offNormals);
    auto &array = _walkmesh->normals;
    _bwm.checkCount(_numFaces);
    array.reserve(_numFaces);
    for (uint32_t i = 0; i < _numFaces; ++i) {
        float x = _bwm.readFloat();
        float y = _bwm.readFloat();
        float z = _bwm.readFloat();
        array.emplace_back(x, y, z);
    }
}

void BwmReader::loadAABB() {
    _bwm.seek(_offAabb);

    _bwm.checkCount(_numAabb);
    std::vector<std::shared_ptr<Walkmesh::AABB>> aabbs;
    aabbs.resize(_numAabb);

    std::vector<std::pair<uint32_t, uint32_t>> aabbChildren;
    aabbChildren.resize(_numAabb);

    for (uint32_t i = 0; i < _numAabb; ++i) {
        std::vector<float> bounds(_bwm.readFloatArray(6));
        int faceIdx = _bwm.readInt32();
        _bwm.skipBytes(4); // unknown
        uint32_t mostSignificantPlane = _bwm.readUint32();
        uint32_t childIdx1 = _bwm.readUint32();
        uint32_t childIdx2 = _bwm.readUint32();

        aabbs[i] = std::make_shared<Walkmesh::AABB>();
        aabbs[i]->value = AABB(glm::make_vec3(&bounds[0]), glm::make_vec3(&bounds[3]));
        if (faceIdx < -1 || (faceIdx >= 0 && static_cast<uint32_t>(faceIdx) >= _numFaces)) {
            throw ValidationException("BWM AABB face index out of range");
        }
        aabbs[i]->faceIdx = faceIdx;

        aabbChildren[i] = std::make_pair(childIdx1, childIdx2);
    }

    // The nodes must form a tree below the first one. Linking a cycle, or a node
    // reachable twice, would leak (shared_ptr cycle) and send every later walk
    // of the tree into an endless loop, so check the indices before linking.
    std::vector<bool> reached(_numAabb, false);
    if (!aabbs.empty()) {
        std::vector<uint32_t> pending {0};
        reached[0] = true;
        while (!pending.empty()) {
            uint32_t i = pending.back();
            pending.pop_back();
            if (aabbs[i]->faceIdx != -1) {
                continue;
            }
            for (uint32_t child : {aabbChildren[i].first, aabbChildren[i].second}) {
                if (child >= _numAabb || reached[child]) {
                    throw ValidationException("BWM AABB nodes do not form a tree");
                }
                reached[child] = true;
                pending.push_back(child);
            }
        }
    }

    for (uint32_t i = 0; i < _numAabb; ++i) {
        // Nodes outside the tree stay unlinked: among themselves they could form
        // a cycle the walk above never saw.
        if (!reached[i] || aabbs[i]->faceIdx != -1) {
            continue;
        }
        uint32_t childIdx1 = aabbChildren[i].first;
        uint32_t childIdx2 = aabbChildren[i].second;
        if (childIdx1 >= _numAabb || childIdx2 >= _numAabb) {
            throw ValidationException("BWM AABB child index out of range");
        }
        aabbs[i]->left = aabbs[childIdx1];
        aabbs[i]->right = aabbs[childIdx2];
    }

    if (!aabbs.empty()) {
        _walkmesh->_rootAabb = aabbs[0];
    }
}

} // namespace graphics

} // namespace reone
