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

#include "reone/game/gui/dialog.h"

#include <cmath>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/messagelog.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static const char kControlTagTopFrame[] = "TOP";
static const char kControlTagBottomFrame[] = "BOTTOM";
static const char kObjectTagOwner[] = "owner";

// Odyssey DLG participant animation ordinals occupy two namespaces.
//
// Ordinals at or above kDialogAnimationBase index dialoganimations.2da and name
// a semantic dialogue animation. K1 also uses valid positive 2DA rows directly.
// Recognized lower ordinal bands name a cutscene clip on the target model: the
// band selects the clip name suffix and whether the clip is held, while the
// offset within the band selects the clip number. Both namespaces are
// independent of AnimatedCut and of whether the participant is driven by a
// stunt model.
static constexpr int kDialogAnimationBase = 10000;

// Whether creatures can turn their heads to follow the speaker. Until they
// can, dialogue facing turns the body fully.
static constexpr bool kDialogHeadLookAt = false;

// The conversation bands are viewport-relative, not authored plate art:
// the subtitle sits in the top sixth and the reply list in the bottom sixth
// of whatever viewport the game is running at.
static constexpr int kBandDivisor = 6;
static constexpr int kCutAnimationBandSize = 200;

static const struct CutAnimationBand {
    int base;
    const char *suffix;
    bool looping;
} g_cutAnimationBands[] {
    {1000, "", false},
    {1200, "w", false},
    {1400, "l", true},
    {1600, "wl", true}};

static const std::unordered_map<std::string, AnimationType> g_animTypeByName {
    {"dead", AnimationType::LoopingDead},
    {"taunt", AnimationType::FireForgetTaunt},
    {"greeting", AnimationType::FireForgetGreeting},
    {"listen", AnimationType::LoopingListen},
    {"worship", AnimationType::LoopingWorship},
    {"salute", AnimationType::FireForgetSalute},
    {"bow", AnimationType::FireForgetBow},
    {"talk_normal", AnimationType::LoopingTalkNormal},
    {"talk_pleading", AnimationType::LoopingTalkPleading},
    {"talk_forceful", AnimationType::LoopingTalkForceful},
    {"talk_laughing", AnimationType::LoopingTalkLaughing},
    {"talk_sad", AnimationType::LoopingTalkSad},
    {"victory", AnimationType::FireForgetVictory1},
    {"scratch_head", AnimationType::FireForgetPauseScratchHead},
    {"drunk", AnimationType::LoopingPauseDrunk},
    {"inject", AnimationType::FireForgetInject},
    {"flirt", AnimationType::LoopingFlirt},
    {"use_computer_lp", AnimationType::LoopingUseComputer},
    {"horror", AnimationType::LoopingHorror},
    {"use_computer", AnimationType::FireForgetUseComputer},
    {"persuade", AnimationType::FireForgetPersuade},
    {"activate", AnimationType::FireForgetActivate},
    {"sleep", AnimationType::LoopingSleep},
    {"prone", AnimationType::LoopingProne},
    {"ready", AnimationType::LoopingReady},
    {"pause", AnimationType::LoopingPause},
    {"choked", AnimationType::LoopingChoke},
    {"talk_injured", AnimationType::LoopingTalkInjured},
    {"listen_injured", AnimationType::LoopingListenInjured},
    {"kneel_talk_angry", AnimationType::LoopingKneelTalkAngry},
    {"kneel_talk_sad", AnimationType::LoopingKneelTalkSad}};

void DialogGUI::preload(IGUI &gui) {
    GameGUI::preload(gui);
    // Conversation bands and reply boxes are viewport-relative rather than
    // authored plate art. Their dialog-specific font scale follows the
    // uniform limiting axis without inheriting the global text multiplier.
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
    gui.setTextScale(_game.options().graphics.guiDialogTextScale);
}

void DialogGUI::onGUILoaded() {
    bindControls();
    configureMessage();
    configureReplies();
    loadFrames();

    _controls.LB_REPLIES->setOnItemClick([this](const std::string &item) {
        int replyIdx = stoi(item);
        pickReply(replyIdx);
    });
}

void DialogGUI::selectReplyForCapture(int index) {
    _controls.LB_REPLIES->setSelectedItemIndex(index);
}

int DialogGUI::bandHeight() const {
    return _game.options().graphics.height / kBandDivisor;
}

Control::Extent DialogGUI::bandExtent(int top) const {
    return {0, top, _game.options().graphics.width, bandHeight()};
}

Control::Extent DialogGUI::replySafeArea() const {
    int safeWidth = std::min(_game.options().graphics.width, _game.options().graphics.height * 4 / 3);
    int safeLeft = (_game.options().graphics.width - safeWidth) / 2;
    return {safeLeft, _game.options().graphics.height - bandHeight(), safeWidth, bandHeight()};
}

void DialogGUI::loadFrames() {
    addFrame(kControlTagTopFrame, 0);
    addFrame(kControlTagBottomFrame, _game.options().graphics.height - bandHeight());
}

void DialogGUI::addFrame(std::string tag, int top) {
    auto frame = _gui->newControl(ControlType::Panel, tag);
    frame->setExtent(bandExtent(top));
    frame->setBorderFill("blackfill");

    _gui->addControlToFront(std::move(frame), IGUI::ControlCoordinates::Screen);
}

void DialogGUI::configureMessage() {
    _controls.LBL_MESSAGE->setExtent(bandExtent(0));
    _controls.LBL_MESSAGE->setTextColor(_baseColor);
}

void DialogGUI::configureReplies() {
    // Reply prose is authored for a 4:3 dialogue safe area. Keep that area
    // centred on wider displays, but preserve the original left alignment
    // inside it so choices scan as a conventional vertical list.
    // The list's root remains full-width so its authored child coordinates
    // do not receive the safe-area offset twice. The row prototype below is
    // positioned in the 4:3 rectangle itself.
    _controls.LB_REPLIES->setExtent(bandExtent(_game.options().graphics.height - bandHeight()));
    // The authored list reserves a scroll-bar column against its left edge,
    // with the row prototype indented past it. Recreate that column at the
    // safe area's left edge: the list is moved into the band by the extent
    // override above, and no layout pass carries its scroll bar along, so
    // without this the bar would render at its raw authored coordinates in
    // the screen's top-left corner whenever the replies overflow the band.
    // The bar and the row indent share the dialogue text scale, not the
    // layout factor: the rows draw their prose at that scale, and the
    // authored proportion is a bar as wide as a row is tall.
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        auto safeArea = replySafeArea();
        scrollBar->setExtent({
            safeArea.left,
            safeArea.top,
            static_cast<int>(std::lround(scrollBar->authoredExtent().width * _controls.LBL_MESSAGE->scale())),
            safeArea.height});
    }
    _controls.LB_REPLIES->setProtoMatchContent(true);
    _controls.LB_REPLIES->protoItem().setTextFont(_controls.LBL_MESSAGE->text().font);
    _controls.LB_REPLIES->protoItem().setScale(_controls.LBL_MESSAGE->scale());
    _controls.LB_REPLIES->protoItem().setTextAlignment(Control::TextAlign::LeftCenter);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

// K2 behaviour: the automatic angle walks this table (1 close-up,
// 2 over the shoulder, 3 wide).
static constexpr int kAutoAngles[] {1, 3, 1, 3, 2, 2, 1, 3, 1, 2, 1, 2, 1, 3, 2, 3, 1, 1, 1};
static constexpr int kAutoAngleCount = static_cast<int>(sizeof(kAutoAngles) / sizeof(kAutoAngles[0]));

// A stable string hash, so that host and co-op client seed the table alike.
static uint32_t stableHash(const std::string &text) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

void DialogGUI::onStart() {
    _currentSpeaker = owner();
    _currentListener.reset();
    _previousSpeaker.reset();
    _previousListener.reset();
    _firstNodeShown = false;
    _pairSides = {};
    _closeUpRange = 0.0f;
    _haveDialogShot = false;
    _lastAngle = DialogCamera::Angle::CloseUp;
    _shotSpeaker.reset();
    _shotListener.reset();
    {
        auto ownerObject = owner();
        uint32_t seed = stableHash(_dialog->resRef + "/" + (ownerObject ? ownerObject->tag() : std::string()));
        _angleCycle.index = static_cast<int>(seed % kAutoAngleCount);
        _angleCycle.step = static_cast<int>(seed % 5) + 1;
        _angleCycle.up = (seed % 2) != 0;
    }
    _heldCutParticipants.clear();
    loadStuntParticipants();

    auto camera = _game.module()->area()->getCamera<AnimatedCamera>(CameraType::Animated);
    camera->setModel(_cameraModel);
}

void DialogGUI::loadStuntParticipants() {
    if (!hasStuntPresentation()) {
        return;
    }

    _participantByTag.clear();

    for (auto &stunt : _dialog->stunts) {
        std::shared_ptr<Creature> creature(resolveParticipantCreature(stunt.participant));
        if (!creature) {
            warn("Dialog: participant creature not found by tag: " + stunt.participant);
            continue;
        }
        Participant participant;
        participant.creature = creature;

        std::shared_ptr<Model> model(_services.resource.models.get(stunt.stuntModel));
        if (!model) {
            warn("Dialog: stunt model not found: " + stunt.stuntModel);
            continue;
        }
        participant.model = model;

        if (_dialog->isAnimatedCutscene()) {
            creature->startStuntMode();
            creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || !_dialog->stunts.empty();
}

std::shared_ptr<Creature> DialogGUI::resolveParticipantCreature(const std::string &participant) const {
    if (participant == kObjectTagOwner) {
        return std::dynamic_pointer_cast<Creature>(owner());
    }
    if (boost::iequals(participant, kObjectTagPlayer)) {
        return _game.party().player();
    }
    return std::dynamic_pointer_cast<Creature>(_game.module()->area()->getObjectByTag(participant));
}

std::shared_ptr<Animation> DialogGUI::getStuntParticipantAnimation(
    const std::string &participant,
    int ordinal) const {
    auto cut = decodeCutAnimation(ordinal);
    if (!cut) {
        return nullptr;
    }
    auto maybeParticipant = _participantByTag.find(participant);
    return maybeParticipant != _participantByTag.end()
               ? maybeParticipant->second.model->getAnimation(cut->name)
               : nullptr;
}

void DialogGUI::hideSceneEquipment(const std::shared_ptr<Creature> &creature) {
    if (!creature) {
        return;
    }
    creature->setSceneHiddenEquipment(
        creature->sceneHidesWeapons() || _dialog->unequipItems,
        creature->sceneHidesHeadItem() || _dialog->unequipHItem);
    bool listed = std::any_of(_sceneUnequipped.begin(), _sceneUnequipped.end(), [&creature](const auto &ref) {
        return ref.resolve() == creature;
    });
    if (!listed) {
        _sceneUnequipped.push_back(creature);
    }
}

void DialogGUI::hideNodeParticipantsEquipment() {
    // K2 behaviour: on every node, the owner, speaker and listener and the
    // node's animated participants put weapons (and head items) away.
    if (!_dialog->unequipItems && !_dialog->unequipHItem) {
        return;
    }
    hideSceneEquipment(std::dynamic_pointer_cast<Creature>(owner()));
    hideSceneEquipment(std::dynamic_pointer_cast<Creature>(_currentSpeaker.resolve()));
    hideSceneEquipment(_game.party().player());
    for (auto &anim : _currentEntry->animations) {
        hideSceneEquipment(resolveParticipantCreature(anim.participant));
    }
}

void DialogGUI::restoreSceneEquipment() {
    // Shown again from whatever each creature has equipped now.
    for (auto &ref : _sceneUnequipped) {
        if (auto creature = ref.resolve()) {
            creature->setSceneHiddenEquipment(false, false);
        }
    }
    _sceneUnequipped.clear();
}

void DialogGUI::onLoadEntry() {
    restoreInactiveStuntParticipants();
    loadCurrentSpeaker();
    hideNodeParticipantsEquipment();
    updateParticipantAnimations();
    updateCamera();
    repositionMessage();

    _controls.LB_REPLIES->setVisible(false);
}

void DialogGUI::restoreInactiveStuntParticipants() {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    for (auto &entry : _participantByTag) {
        if (!entry.second.mixedStuntActive) {
            continue;
        }
        bool drivenThisEntry = false;
        for (auto &anim : _currentEntry->animations) {
            if (anim.participant == entry.first && getStuntParticipantAnimation(anim.participant, anim.animation)) {
                drivenThisEntry = true;
                break;
            }
        }
        if (!drivenThisEntry) {
            leaveMixedStunt(entry.second);
        }
    }
}

bool DialogGUI::enterMixedStunt(Participant &participant, const std::shared_ptr<Animation> &animation, bool looping) {
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return false;
    }
    if (!participant.mixedStuntActive && creature->isStuntMode()) {
        warn("Dialog: participant is already in stunt mode: " + creature->tag());
        return false;
    }

    AnimationProperties properties;
    properties.flags = AnimationFlags::propagate | (looping ? AnimationFlags::loop : 0);
    properties.scale = 1.0f;
    if (!creature->playExternalAnimation(animation, std::move(properties))) {
        return false;
    }

    if (!participant.mixedStuntActive) {
        participant.restorePosition = creature->position();
        participant.restoreFacing = creature->getFacing();
        if (auto node = creature->sceneNode()) {
            participant.restoreCulling = node->isCullingEnabled();
        }
        creature->startStuntMode();
        participant.mixedStuntActive = true;
    }
    return true;
}

void DialogGUI::leaveMixedStunt(Participant &participant) {
    if (!participant.mixedStuntActive) {
        return;
    }
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return;
    }
    creature->resumeStateDrivenAnimation();
    creature->setPosition(participant.restorePosition);
    creature->setFacing(participant.restoreFacing);
    creature->stopStuntMode();
    if (auto node = creature->sceneNode()) {
        node->setCullingEnabled(participant.restoreCulling);
    }
    participant.mixedStuntActive = false;
}

std::shared_ptr<Object> DialogGUI::conversationPC() const {
    if (auto leader = _game.party().getLeader()) {
        return leader;
    }
    return _game.party().player();
}

std::shared_ptr<Object> DialogGUI::resolveListenerTag(const std::string &tag) const {
    if (tag.empty()) {
        return nullptr;
    }
    if (boost::iequals(tag, kObjectTagPlayer)) {
        return conversationPC();
    }
    if (boost::iequals(tag, kObjectTagOwner)) {
        return owner();
    }
    return findByTag(tag);
}

std::shared_ptr<Object> DialogGUI::findByTag(const std::string &tag) const {
    for (const auto &member : _game.party().members()) {
        if (member.creature && boost::iequals(member.creature->tag(), tag)) {
            return member.creature;
        }
    }
    return _game.module()->area()->getObjectByTag(tag);
}

void DialogGUI::setSpeakerAndListener(std::shared_ptr<Object> speaker, std::shared_ptr<Object> listener) {
    _previousSpeaker = _currentSpeaker;
    _previousListener = _currentListener;
    _currentSpeaker = std::move(speaker);
    _currentListener = std::move(listener);
}

void DialogGUI::loadCurrentSpeaker() {
    std::shared_ptr<Area> area(_game.module()->area());
    std::shared_ptr<Object> speaker;

    if (!_currentEntry->speaker.empty()) {
        speaker = findByTag(_currentEntry->speaker);
    }
    if (!speaker) {
        speaker = owner();
    }

    // Make previous speaker stop talking, if any
    auto previousSpeaker = _currentSpeaker.resolve();
    if (previousSpeaker && previousSpeaker != speaker) {
        auto speakerCreature =
            std::dynamic_pointer_cast<Creature>(previousSpeaker);
        if (speakerCreature) {
            speakerCreature->stopTalking();
        }
    }

    // Whom the speaker addresses: the node's Listener, else the player
    // character at first and while the same speaker goes on, else whoever
    // spoke last. Angle 5 goes back to the previous node's pair.
    std::shared_ptr<Object> listener(resolveListenerTag(_currentEntry->listener));
    if (!listener) {
        if (!_firstNodeShown || speaker == previousSpeaker) {
            listener = conversationPC();
        } else {
            listener = previousSpeaker;
        }
    }
    if (_currentEntry->cameraAngle == 5 && _firstNodeShown && _previousSpeaker.resolve()) {
        speaker = _previousSpeaker.resolve();
        listener = _previousListener.resolve();
    }
    _firstNodeShown = true;
    // The Messages screen's dialog log (design note k1hud_parity_2, follow-up).
    _game.messageLog().addDialog(speaker ? speaker->name() : std::string(),
                                 _game.substituteCustomTokens(_currentEntry->text));
    setSpeakerAndListener(speaker, listener);

    if (auto speakerCreature = std::dynamic_pointer_cast<Creature>(speaker)) {
        speakerCreature->startTalking(_lipAnimation);
    }
}

void DialogGUI::turnInDialog(Creature &creature, const Object &other) {
    const DialogLocks &locks = _game.dialogLocks();
    if (locks.isOrientationLocked(creature.id())) {
        return;
    }
    glm::vec2 toOther(glm::vec2(other.position()) - glm::vec2(creature.position()));
    if (glm::length(toOther) < 1e-4f) {
        return;
    }
    // A creature that follows with its head turns its body only as far as
    // its head cannot reach. K2 turns the body fully when the head cannot
    // look at the other; reone has no head look-at yet, so that is always
    // the case here (design note p2_cine).
    bool headFollows = kDialogHeadLookAt && !locks.isHeadFollowLocked(creature.id());
    float arc = 40.0f;
    if (headFollows) {
        std::shared_ptr<TwoDA> appearances(_services.resource.twoDas.get("appearance"));
        if (appearances && creature.appearance() >= 0 && creature.appearance() < appearances->getRowCount()) {
            headFollows = appearances->getInt(creature.appearance(), "headtrack", 1) != 0;
            float tableArc = appearances->getFloat(creature.appearance(), "head_arc_h", 0.0f);
            if (tableArc > 0.0f) {
                arc = tableArc;
            }
        }
    }
    if (!headFollows) {
        creature.face(other);
        return;
    }
    float facing = creature.getFacing();
    glm::vec2 heading(-glm::sin(facing), glm::cos(facing));
    glm::vec2 direction(glm::normalize(toOther));
    float angle = glm::degrees(std::acos(glm::clamp(glm::dot(heading, direction), -1.0f, 1.0f)));
    if (!std::isfinite(angle) || angle <= arc) {
        return;
    }
    float turn = glm::radians(angle - (arc + 1.0f));
    float cross = heading.x * direction.y - heading.y * direction.x;
    creature.setFacing(facing + (cross >= 0.0f ? turn : -turn));
}

void DialogGUI::applyFacing() {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    auto speaker = _currentSpeaker.resolve();
    auto listener = _currentListener.resolve();
    if (!speaker || !listener || speaker == listener) {
        return;
    }
    auto listenerCreature = std::dynamic_pointer_cast<Creature>(listener);
    if (listenerCreature && !listenerCreature->isStuntMode()) {
        turnInDialog(*listenerCreature, *speaker);
    }
    auto speakerCreature = std::dynamic_pointer_cast<Creature>(speaker);
    if (speakerCreature && !speakerCreature->isStuntMode()) {
        turnInDialog(*speakerCreature, *listener);
    }
}

int DialogGUI::nextAutoAngle() {
    int angle = kAutoAngles[_angleCycle.index];
    _angleCycle.index += _angleCycle.up ? _angleCycle.step : -_angleCycle.step;
    if (_angleCycle.index > kAutoAngleCount - 1) {
        _angleCycle.index = 0;
    }
    if (_angleCycle.index < 0) {
        _angleCycle.index = kAutoAngleCount - 1;
    }
    return angle;
}

int DialogGUI::sideForPair(const Object &speaker, const Object &listener) {
    for (auto &pair : _pairSides) {
        if (pair.side == 0) {
            break;
        }
        if (pair.speaker == speaker.id() && pair.listener == listener.id()) {
            return pair.side;
        }
        if (pair.speaker == listener.id() && pair.listener == speaker.id()) {
            return pair.side == 1 ? 2 : 1;
        }
    }
    auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
    int side = camera->chooseSide(getCameraAnchor(speaker), getCameraAnchor(listener));
    // The first free slot of four; with all four taken, the first is replaced.
    auto slot = std::find_if(_pairSides.begin(), _pairSides.end(), [](const auto &pair) { return pair.side == 0; });
    if (slot == _pairSides.end()) {
        slot = _pairSides.begin();
    }
    *slot = {speaker.id(), listener.id(), side};
    return side;
}

float DialogGUI::closeUpRangeFor(const resource::Dialog::EntryReply &node) {
    // The speaker's animation on this node, else the talk animation.
    static constexpr int kTalkAnimation = kDialogAnimationBase + 38;
    int ordinal = kTalkAnimation;
    auto speaker = _currentSpeaker.resolve();
    for (auto &anim : node.animations) {
        auto creature = resolveParticipantCreature(anim.participant);
        if (creature && speaker && creature->id() == speaker->id()) {
            ordinal = anim.animation;
            break;
        }
    }
    float range = 0.0f;
    int row = ordinal >= kDialogAnimationBase ? ordinal - kDialogAnimationBase : (!_game.isTSL() && ordinal > 0 ? ordinal : -1);
    std::shared_ptr<TwoDA> animations(_services.resource.twoDas.get("dialoganimations"));
    if (animations && row >= 0 && row < animations->getRowCount()) {
        range = animations->getFloat(row, "cu_pb_range", 0.0f);
    }
    if (std::abs(range) < 0.001f) {
        range = 0.0f;
    }
    // A range holds while successive close-ups ask for one.
    if (range == 0.0f) {
        _closeUpRange = 0.0f;
    }
    if (_closeUpRange <= 0.0f) {
        _closeUpRange = range;
    }
    return _closeUpRange;
}

void DialogGUI::updateCamera() {
    std::shared_ptr<Area> area(_game.module()->area());
    auto node = cameraNode();
    if (!node) {
        return;
    }
    if (usesCameraAnimation(node)) {
        // The shot comes from the animation, from its first frame; nobody turns.
        auto camera = area->getCamera<AnimatedCamera>(CameraType::Animated);
        camera->setFieldOfView(node->camFieldOfView != 0.0f ? node->camFieldOfView : kDefaultAnimCamFOV);
        camera->playAnimation(node->cameraAnimation);
        return;
    }
    applyFacing();
    int cameraId = 0;
    if (getCamera(cameraId) == CameraType::Dialog) {
        frameNode(*node);
    }
}

void DialogGUI::frameNode(const resource::Dialog::EntryReply &node) {
    // The automatic angle advances on every framed node, as it does in K2.
    int autoAngle = nextAutoAngle();
    int angle = node.cameraAngle;
    if (angle == 4 || angle == 5) {
        angle = _haveDialogShot ? static_cast<int>(DialogCamera::Angle::Hold) : autoAngle;
    } else if (angle < 1 || angle > 3) {
        angle = autoAngle;
    }
    auto shotAngle = static_cast<DialogCamera::Angle>(angle);

    auto speaker = _currentSpeaker.resolve();
    auto listener = _currentListener.resolve();
    if (!speaker) {
        return;
    }
    if (!listener) {
        listener = speaker;
    }
    auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);

    // Two wide shots in a row keep the first one's framing.
    if (_haveDialogShot && shotAngle == DialogCamera::Angle::Wide && _lastAngle == DialogCamera::Angle::Wide) {
        return;
    }
    DialogCamera::Shot shot;
    shot.angle = shotAngle;
    shot.side = sideForPair(*speaker, *listener);
    if (shotAngle == DialogCamera::Angle::CloseUp) {
        shot.closeUpRange = closeUpRangeFor(node);
    } else {
        _closeUpRange = 0.0f;
    }
    shot.cameraHeightOffset = node.camHeightOffset;
    shot.targetHeightOffset = node.tarHeightOffset;
    glm::vec3 speakerAnchor(getCameraAnchor(*speaker));
    glm::vec3 listenerAnchor(getCameraAnchor(*listener));
    // A shot the area blocks falls back to the close-up of the speaker.
    shot.closeUpFallback = camera->isBlocked(shot, speakerAnchor, listenerAnchor);

    _shotSpeaker = speaker;
    _shotListener = listener;
    camera->setEndpoints(speakerAnchor, listenerAnchor);
    camera->setShot(shot);
    _haveDialogShot = true;
    _lastAngle = shotAngle;
}

void DialogGUI::followShotPair() {
    if (!_haveDialogShot || _game.cameraType() != CameraType::Dialog) {
        return;
    }
    auto speaker = _shotSpeaker.resolve();
    auto listener = _shotListener.resolve();
    if (!speaker || !listener) {
        return;
    }
    auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
    camera->setEndpoints(getCameraAnchor(*speaker), getCameraAnchor(*listener));
}

Conversation::PresentationReport DialogGUI::presentationReport() const {
    PresentationReport report;
    report.speaker = _currentSpeaker.resolve();
    report.listener = _currentListener.resolve();
    int cameraId = 0;
    if (_dialog && cameraNode() && _haveDialogShot && _game.module() && _game.module()->area() &&
        getCamera(cameraId) == CameraType::Dialog) {
        auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
        report.dialogShot = true;
        report.angle = static_cast<int>(camera->shot().angle);
        report.side = camera->shot().side;
        report.closeUpFallback = camera->shot().closeUpFallback;
        report.eye = camera->framing().eye;
        report.target = camera->framing().target;
    }
    return report;
}

glm::vec3 DialogGUI::getTalkPosition(const Object &object) const {
    auto node = object.sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return object.position();
    }

    auto model = std::static_pointer_cast<ModelSceneNode>(node);
    std::shared_ptr<ModelNode> talkDummy(model->model().getNodeByNameRecursive("talkdummy"));
    if (!talkDummy)
        return model->getWorldCenterOfAABB();

    return (model->absoluteTransform() * talkDummy->absoluteTransform())[3];
}

glm::vec3 DialogGUI::getCameraAnchor(const Object &object) const {
    auto node = object.sceneNode();
    if (node && node->type() == SceneNodeType::Model) {
        auto model = std::static_pointer_cast<ModelSceneNode>(node);
        if (auto hook = model->model().getNodeByNameRecursive("camerahook")) {
            return (model->absoluteTransform() * hook->absoluteTransform())[3];
        }
    }
    return getTalkPosition(object);
}

void DialogGUI::updateParticipantAnimations() {
    // Each authored animation is resolved on its own. The ordinal decides which
    // animation is meant, the participant decides which model plays it, and a
    // single entry may drive stunt-bound participants and ordinary area
    // creatures side by side.
    for (auto &anim : _currentEntry->animations) {
        if (auto cut = decodeCutAnimation(anim.animation)) {
            applyCutAnimation(anim.participant, *cut);
        } else {
            applyDialogAnimation(anim.participant, anim.animation);
        }
    }
}

void DialogGUI::applyCutAnimation(const std::string &participant, const CutAnimation &cut) {
    auto maybeParticipant = _participantByTag.find(participant);
    if (maybeParticipant != _participantByTag.end()) {
        Participant &stunt = maybeParticipant->second;
        if (auto animation = stunt.model->getAnimation(cut.name)) {
            if (_dialog->isAnimatedCutscene()) {
                AnimationProperties properties;
                properties.flags = AnimationFlags::propagate | (cut.looping ? AnimationFlags::loop : 0);
                properties.scale = 1.0f;
                if (auto creature = stunt.creature.resolve()) {
                    creature->playExternalAnimation(
                        animation, std::move(properties));
                }
            } else {
                enterMixedStunt(stunt, animation, cut.looping);
            }
            return;
        }
        // The stunt model is the authored source for this participant, so a
        // missing clip is a data problem rather than a reason to silently
        // animate from somewhere else. Staged participants also sit at the
        // stunt origin, where an in-place clip would play in the wrong place.
        warn("Dialog: stunt model has no animation: " + cut.name);
        return;
    }

    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    auto node = creature->sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return;
    }
    // Cut clips authored without the world-space suffix live on the creature's
    // own model, so they play in place rather than through stunt staging.
    auto animation = std::static_pointer_cast<ModelSceneNode>(node)->model().getAnimation(cut.name);
    if (!animation) {
        return;
    }
    AnimationProperties properties;
    if (cut.looping) {
        properties.flags |= AnimationFlags::loop;
    }
    // Authored cutscene clips stay under dialogue ownership: a one-shot clip
    // holds its final frame instead of falling back to the state-driven idle,
    // because the authored sequence may leave entries without an AnimList
    // before the next clip takes over.
    if (creature->playExternalAnimation(animation, std::move(properties))) {
        holdCutParticipant(creature);
    }
}

void DialogGUI::applyDialogAnimation(const std::string &participant, int ordinal) {
    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return;
    }
    AnimationType animType = getDialogAnimationType(ordinal);
    if (animType != AnimationType::Invalid) {
        creature->playAnimation(animType);
    }
}

std::optional<DialogGUI::CutAnimation> DialogGUI::decodeCutAnimation(int ordinal) {
    for (auto &band : g_cutAnimationBands) {
        int offset = ordinal - band.base;
        if (offset < 0 || offset >= kCutAnimationBandSize) {
            continue;
        }
        CutAnimation cut;
        cut.name = str(boost::format("cut%03d%s") % (offset + 1) % band.suffix);
        cut.looping = band.looping;
        return cut;
    }
    return std::nullopt;
}

AnimationType DialogGUI::getDialogAnimationType(int ordinal) const {
    int index;
    if (ordinal >= kDialogAnimationBase) {
        index = ordinal - kDialogAnimationBase;
    } else if (ordinal > 0 && !_game.isTSL()) {
        index = ordinal;
    } else {
        // Cut-band ordinals never reach here. K2 lower ordinals and the zero
        // sentinel belong to no ordinary-animation namespace reone recognises.
        warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        return AnimationType::Invalid;
    }
    std::shared_ptr<TwoDA> animations(_services.resource.twoDas.get("dialoganimations"));

    if (index >= animations->getRowCount()) {
        if (ordinal < kDialogAnimationBase) {
            warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        } else {
            warn("Dialog: animation index out of bounds: " + std::to_string(index));
        }
        return AnimationType::Invalid;
    }

    std::string name(boost::to_lower_copy(animations->getString(index, "name")));
    auto maybeAnimType = g_animTypeByName.find(name);

    return maybeAnimType != g_animTypeByName.end() ? maybeAnimType->second : AnimationType::Invalid;
}

void DialogGUI::repositionMessage() {
    Control::Text text(_controls.LBL_MESSAGE->text());
    int top;

    if (_entryEnded) {
        text.align = Control::TextAlign::CenterBottom;
        top = 0;
    } else {
        text.align = Control::TextAlign::CenterTop;
        top = _controls.LB_REPLIES->extent().top;
    }

    _controls.LBL_MESSAGE->setText(std::move(text));
    _controls.LBL_MESSAGE->setExtentTop(top);
}

void DialogGUI::onFinish() {
    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }
    releaseHeldCutParticipants();
    restoreSceneEquipment();

    // Make current speaker stop talking, if any
    auto speakerCreature =
        std::dynamic_pointer_cast<Creature>(_currentSpeaker.resolve());
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
}

void DialogGUI::holdCutParticipant(const std::shared_ptr<Creature> &creature) {
    auto maybeHeld = std::find_if(
        _heldCutParticipants.begin(), _heldCutParticipants.end(),
        [&creature](const auto &held) {
            return held.resolve() == creature;
        });
    if (maybeHeld == _heldCutParticipants.end()) {
        _heldCutParticipants.push_back(creature);
    }
}

void DialogGUI::releaseHeldCutParticipants() {
    for (auto &reference : _heldCutParticipants) {
        if (auto creature = reference.resolve()) {
            creature->resumeStateDrivenAnimation();
        }
    }
    _heldCutParticipants.clear();
}

void DialogGUI::releaseStuntParticipants() {
    if (!_dialog->isAnimatedCutscene()) {
        for (auto &participant : _participantByTag) {
            leaveMixedStunt(participant.second);
        }
        _participantByTag.clear();
        return;
    }
    for (auto &participant : _participantByTag) {
        auto creature = participant.second.creature.resolve();
        if (!creature) continue;
        creature->resumeStateDrivenAnimation();
        creature->stopStuntMode();
        creature->setIsInConversation(false);
    }
    _participantByTag.clear();
}

void DialogGUI::onEntryEnded() {
    _controls.LB_REPLIES->setVisible(true);

    // The reply list is the player character speaking to the entry's speaker,
    // framed by the first reply's camera fields.
    if (isReplyListShown()) {
        auto entrySpeaker = _currentSpeaker.resolve();
        if (!entrySpeaker) {
            entrySpeaker = owner();
        }
        setSpeakerAndListener(conversationPC(), entrySpeaker);
        updateCamera();
    }
    repositionMessage();
}

void DialogGUI::setMessage(std::string message) {
    _controls.LBL_MESSAGE->setTextMessage(message);
}

void DialogGUI::setReplyLines(std::vector<std::string> lines) {
    _controls.LB_REPLIES->clearItems();

    for (size_t i = 0; i < lines.size(); ++i) {
        ListBox::Item item;
        item.tag = std::to_string(i);
        item.text = lines[i];
        _controls.LB_REPLIES->addItem(std::move(item));
    }
    // Replies start at the top-left of the centred 4:3 safe area within the
    // bottom band, indented past the scroll-bar column by their authored
    // offset so an overflowing list shows its bar beside the prose, not
    // under it. K1 authors the rows flush against the bar, which reads as
    // touching; hold them clear of it by the gap TSL authors, which leaves
    // TSL's own indent unchanged. The list root stays full-width so the
    // offset is applied exactly once to its row prototype.
    static constexpr int kScrollBarTextGap = 8;
    auto extent = _controls.LB_REPLIES->protoItem().extent();
    const auto &band = _controls.LB_REPLIES->extent();
    auto safeArea = replySafeArea();
    float textScale = _controls.LBL_MESSAGE->scale();
    int indent = static_cast<int>(std::lround(
        (_controls.LB_REPLIES->protoItem().authoredExtent().left -
         _controls.LB_REPLIES->authoredExtent().left) *
        textScale));
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        indent = std::max(
            indent,
            scrollBar->extent().width + static_cast<int>(std::lround(kScrollBarTextGap * textScale)));
    }
    extent.left = safeArea.left + indent;
    extent.width = safeArea.width - indent;
    extent.top = band.top;
    _controls.LB_REPLIES->protoItem().setExtent(std::move(extent));
}

void DialogGUI::update(float dt) {
    Conversation::update(dt);

    // The dialog camera follows the pair it frames.
    followShotPair();
}

} // namespace game

} // namespace reone
