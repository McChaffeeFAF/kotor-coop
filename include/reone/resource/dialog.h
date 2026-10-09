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

#include <array>

#include <glm/vec3.hpp>

#include "types.h"

namespace reone {

namespace resource {

struct Dialog {
    struct WaitFlags {
        static constexpr int waitAnimFinish = 1;
    };

    struct Stunt {
        std::string participant;
        std::string stuntModel;
    };

    struct EntryReplyLink {
        struct ConditionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        int index {0};
        std::string active;
        std::string active2;
        bool notActive {false};
        bool notActive2 {false};
        int logic {0};
        ConditionParams params;
        ConditionParams params2;
    };

    struct ParticipantAnimation {
        std::string participant;
        int animation;
    };

    struct EntryReply {
        struct ActionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        std::string speaker;
        std::string text;
        std::string voResRef;
        std::string script;
        std::string script2;
        std::string sound;
        std::string listener;
        std::string quest;
        uint32_t questEntry {0};
        int plotIndex {-1};
        float plotXPPercentage {0.0f};
        ActionParams actionParams;
        ActionParams actionParams2;
        int delay {0};
        int waitFlags {0};
        int cameraId {-1};
        int cameraAngle {0};
        int cameraAnimation {0};
        float camFieldOfView {0.0f};
        // Node fade: 0 none; 1 and 4 fade out to the colour, any other value
        // fades in from it. Delay and length are seconds.
        int fadeType {0};
        glm::vec3 fadeColor {0.0f};
        float fadeDelay {0.0f};
        float fadeLength {0.0f};
        // Row of videoeffects.2da shown while the node is up; -1 is none.
        int camVidEffect {-1};
        // Metres added to the dialog camera's height and to its look-at point.
        float camHeightOffset {0.0f};
        float tarHeightOffset {0.0f};
        std::vector<EntryReplyLink> replies;
        std::vector<EntryReplyLink> entries;
        std::vector<ParticipantAnimation> animations;
    };

    std::string resRef;
    bool skippable {false};
    std::string cameraModel;
    std::vector<EntryReplyLink> startEntries;
    std::vector<EntryReply> entries;
    std::vector<EntryReply> replies;
    std::string endScript;
    // Runs instead of endScript when the conversation is cut off rather than
    // reaching an end node (dlg EndConverAbort).
    std::string abortScript;
    int entryIndex {-1};
    bool animatedCutscene {false};
    std::vector<Stunt> stunts;
    ConversationType conversationType {ConversationType::Cinematic};
    // Participants put their weapons (UnequipItems) and head items
    // (UnequipHItem) away for the conversation and take them back after.
    bool unequipItems {false};
    bool unequipHItem {false};
    bool oldHitCheck {false};
    // Default delays for entries and replies whose own Delay is -1.
    uint32_t delayEntry {0};
    uint32_t delayReply {0};
    // Music track (a music resource) played while the conversation runs.
    std::string ambientTrack;
    ComputerType computerType {ComputerType::Normal};

    bool isSkippable() const { return skippable; }
    bool isAnimatedCutscene() const { return animatedCutscene; }

    const EntryReply &getEntry(int index) const { return entries[index]; }
    const EntryReply &getReply(int index) const { return replies[index]; }
};

} // namespace resource

} // namespace reone
