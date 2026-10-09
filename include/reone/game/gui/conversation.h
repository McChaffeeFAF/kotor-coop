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

#include "reone/audio/source.h"
#include "reone/graphics/lipanimation.h"
#include "reone/graphics/model.h"
#include "reone/resource/dialog.h"
#include "reone/system/timer.h"

#include "../gui.h"
#include "../globalfade.h"
#include "../object.h"
#include "../runtimeref.h"
#include "../types.h"

namespace reone {

namespace game {

class Conversation : public GameGUI {
    friend class TestConversation;

public:
    Conversation(Game &game, ServicesView &services) :
        GameGUI(game, services, PresentationFamily::Game) {
    }

    bool handle(const input::Event &event) override;
    /** Per-frame presentation only: GUI animation. */
    void update(float dt) override;

    /**
     * Fixed-step half: entry timing and the owner check. Runs once per
     * simulation tick while the conversation is the active screen, so when an
     * entry ends (and its scripts run) does not depend on the frame rate.
     */
    void updateSimulation(float dt);

    void start(const std::shared_ptr<resource::Dialog> &dialog, const std::shared_ptr<Object> &owner,
               GlobalFade::DialogTicket admission = {});
    void cleanupForModuleTransition();

    CameraType getCamera(int &cameraId) const;

    /**
     * The node whose camera fields frame the screen: the current entry, or
     * the first reply while the reply list is shown (design note p2_cine).
     */
    const resource::Dialog::EntryReply *cameraNode() const;
    bool isReplyListShown() const;
    /** Whether `node` is shown by the conversation's camera animation. */
    bool usesCameraAnimation(const resource::Dialog::EntryReply *node) const;

    /** Who speaks to whom and how the dialog camera frames it, for dumpstate. */
    struct PresentationReport {
        std::shared_ptr<Object> speaker;
        std::shared_ptr<Object> listener;
        bool dialogShot {false};
        int angle {0};
        int side {0};
        bool closeUpFallback {false};
        glm::vec3 eye {0.0f};
        glm::vec3 target {0.0f};
    };
    virtual PresentationReport presentationReport() const { return {}; }

    /**
     * SetDialogPlaceableCamera: show static camera `cameraId` until the next
     * entry loads, whatever the current entry asks for.
     */
    void setPlaceableCamera(int cameraId) { _placeableCamera = cameraId; }
    std::optional<int> placeableCamera() const { return _placeableCamera; }

    void pause();

    // Picks the reply shown at list row `row`, exactly as clicking it does.
    // Returns false when no such row is shown.
    bool pickReplyAtRow(int row);

    void resume();

    /**
     * Cut the conversation off before it reaches an end node: the dialog's
     * EndConverAbort script runs instead of EndConversation, and no creature
     * receives the end-of-dialogue event. No-op without a running dialog.
     */
    void abort();

    /** True from start() until the conversation has ended either way. */
    bool isRunning() const { return _dialog && !_ended; }

    // Co-op client: a replica of the host's conversation (design note coop_net).
    // It shows the host's entries, speaker camera, animations and replies, but
    // runs no condition, action, journal or XP: the host leads, so replies are
    // shown and never picked here.

    void startReplica(const std::shared_ptr<resource::Dialog> &dialog, const std::shared_ptr<Object> &owner);
    /** Show entry `entry` with the host's reply list; entryEnded shows the replies. */
    void showReplicaEntry(int entry, const std::vector<int> &replies, bool entryEnded);
    void endReplica();
    bool isReplica() const { return _replica; }
    /** The dialog entry shown, -1 for none. */
    int currentEntryIndex() const;

    /** How one conversation ended, for diagnostics (dumpstate). */
    struct EndRecord {
        std::string dialog;
        int entry {-1}; // the last entry shown, -1 when none was
        bool normal {false};
        uint64_t tick {0};
    };

    // The most recent ends, oldest first; bounded.
    const std::vector<EndRecord> &endHistory() const { return _endHistory; }

    struct AutoSkip {
        std::queue<std::optional<int>> replies;
        std::queue<bool> entries;
        bool enabled {false};

        std::optional<int> trySkipReply();
        std::optional<bool> trySkipEntry();
    };

    void setAutoSkip(AutoSkip *skip) { _autoSkip = skip; }

    /**
     * Read-only snapshot of the running conversation for diagnostics. Reply
     * positions match the rows of the reply list, and therefore the indices
     * that the selectdialogoption console command accepts.
     */
    struct InspectionState {
        std::shared_ptr<Object> owner;
        std::shared_ptr<resource::Dialog> dialog;
        const resource::Dialog::EntryReply *entry {nullptr};
        std::vector<const resource::Dialog::EntryReply *> replies;
        bool autoPickFirstReply {false};
        bool entryEnded {false};
        bool paused {false};
    };

    InspectionState inspectionState() const {
        return {_owner.resolve(), _dialog, _currentEntry, _replies, _autoPickFirstReply, _entryEnded, _paused};
    }

protected:
    std::shared_ptr<Object> owner() const { return _owner.resolve(); }
    std::shared_ptr<resource::Dialog> _dialog;
    RuntimeObjectRef<Object> _owner;
    std::shared_ptr<graphics::Model> _cameraModel;
    std::shared_ptr<graphics::LipAnimation> _lipAnimation;
    const resource::Dialog::EntryReply *_currentEntry {nullptr};
    std::optional<int> _placeableCamera;
    bool _entryEnded {false};
    bool _paused {false};
    bool _replica {false};

    virtual void loadEntry(int index, bool start = false);

    void pickReply(int index);

    // Complete the active entry's presentation using the same path as expiry.
    void endCurrentEntry();

    virtual void setReplyLines(std::vector<std::string> lines) = 0;

    // How a one-liner presents its entry, alongside setMessage/setReplyLines.
    virtual void setBarkText(std::string text, float duration);

    virtual void onStart();
    virtual void onFinish();
    virtual void onLoadEntry();
    virtual void onEntryEnded();

private:
    std::shared_ptr<audio::AudioSource> _currentVoice;
    std::shared_ptr<audio::AudioSource> _ambientTrack;
    Timer _endEntryTimer;
    // Simulation time the current voice-over has been playing. Compared with
    // the clip length instead of asking the mixer, which runs on wall time.
    float _voiceElapsed {0.0f};
    float _entryDuration {0.0f};
    std::vector<const resource::Dialog::EntryReply *> _replies;
    bool _autoPickFirstReply {false};
    AutoSkip *_autoSkip {nullptr};
    GlobalFade::DialogTicket _fadeDialog;
    uint64_t _generation {0};
    bool _ended {true};
    std::vector<EndRecord> _endHistory;
    // Set while a one-liner resolves its terminal reply: its normal end
    // reaches only the owner, not every creature in the area.
    bool _oneLinerEnding {false};

    bool isCurrent(uint64_t generation) const;

    void loadConversationBackground();
    void loadCameraModel();
    void loadStartEntry();
    void loadVoiceOver();
    void scheduleEndOfEntry();
    void loadReplies();

    bool isSkippableEntry() const;
    bool isNonPresentationalEntry() const;

    void refreshReplies();

    void finish();
    void end(bool normal);
    void signalEndDialogue(const std::shared_ptr<Object> &owner);

    int indexOfFirstActive(const std::vector<resource::Dialog::EntryReplyLink> &links);

    bool isLinkActive(const resource::Dialog::EntryReplyLink &link);
    bool evaluateCondition(const std::string &scriptResRef, const resource::Dialog::EntryReplyLink::ConditionParams &params);
    void runScript(const std::string &scriptResRef, const resource::Dialog::EntryReply::ActionParams &params);
    void runScripts(const resource::Dialog::EntryReply &node);
    void applyStatusSummaryEntries(const resource::Dialog::EntryReply &node);
    void applyNodeFade(const resource::Dialog::EntryReply &node);

    virtual void setMessage(std::string message) = 0;

    // Event handlers

    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);

    // END Event handlers
};

} // namespace game

} // namespace reone
