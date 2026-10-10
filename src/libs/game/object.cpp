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

#include "reone/game/object.h"

#include "reone/game/effect/linkeffects.h"

#include <exception>
#include <sstream>
#include <typeinfo>

#include "reone/game/action/startconversation.h"
#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/item.h"
#include "reone/game/script/savedsituation.h"
#include "reone/resource/provider/scripts.h"
#include "reone/game/room.h"
#include "reone/resource/gff.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kKeepPathDuration = 1000.0f;
static constexpr float kDefaultMaxObjectDistance = 2.0f;
static constexpr float kMaxConversationDistance = 4.0f;
static constexpr float kDistanceWalk = 4.0f;

void Object::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    // FIXME: not all of these properties are shared by all object subclasses.
    gff.readResRef(_blueprintResRef, "TemplateResRef");
    gff.readResRef(_conversation, "Conversation");
    gff.readResRef(_onHeartbeat, "ScriptHeartbeat");
    gff.readResRef(_onUserDefined, "ScriptUserDefine");
    gff.readBool(_minOneHP, "Min1HP");
    gff.readBool(_plot, "Plot");
    gff.readBool(_commandable, "Commandable");
    gff.readBool(_interruptable, "Interruptable");
    gff.readShort(_hitPoints, "HitPoints");
    gff.readShort(_maxHitPoints, "MaxHitPoints");
    gff.readShort(_currentHitPoints, "CurrentHitPoints");

    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "Z");
    gff.readFloat(_position[2], "ZPosition");

    {
        float cosine, sine;
        if (gff.readFloat(cosine, "XOrientation") && gff.readFloat(sine, "YOrientation")) {
            _orientation = yawQuat(-portable::atan2f(cosine, sine));
        }

        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = yawQuat(bearing);
        }
    }

    if (_type != ObjectType::Placeable && _type != ObjectType::Store &&
        _type != ObjectType::Item && dynamic_cast<Creature *>(this) == nullptr) {
        deserializeOwnedItems(
            gff, identityContext, SaveRecordOriginKind::ContainedItem);
    }
    deserializeRuntimeState(gff, identityContext);
}

std::vector<std::shared_ptr<Object>> Object::ownedRuntimeObjects() const {
    return {_items.begin(), _items.end()};
}

std::shared_ptr<Item> Object::appendOwnedItemCandidate(
    std::vector<std::shared_ptr<Item>> &items,
    const std::shared_ptr<Item> &item,
    bool preserveSerializedIdentities) {
    if (!item) {
        throw ValidationException("Cannot stage a null owned Item");
    }
    for (const auto &existing : items) {
        if (!existing || existing.get() == item.get()) {
            continue;
        }
        if (preserveSerializedIdentities &&
            (existing->serializedObjectIdentity() ||
             item->serializedObjectIdentity())) {
            continue;
        }
        if (!existing->isStackCompatibleWith(*item)) {
            continue;
        }
        if (existing->mergeStackFrom(*item)) {
            item->setOwner(0);
            _game.discardStagedRuntimeObjects({item});
            return existing;
        }
    }
    items.push_back(item);
    return item;
}

void Object::deserializeOwnedItems(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SaveRecordOriginKind originKind,
    bool forceDropable,
    std::string originOwner) {
    if (!identityContext.isSerializedState()) {
        if (!gff.has("ItemList")) return;
        std::vector<std::shared_ptr<Object>> obsolete(
            _items.begin(), _items.end());
        std::vector<std::shared_ptr<Item>> replacement;
        ItemAttributes replacementAttributes;
        auto creature = dynamic_cast<Creature *>(this);
        _game.replaceRuntimeObjectGraph(
            obsolete,
            [&]() {
                for (const auto &itemGff : gff.getList("ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    if (forceDropable) item->setDropable(true);
                    item->setOwner(_id);
                    appendOwnedItemCandidate(
                        replacement, item, true);
                }
                if (creature) {
                    for (const auto &item : replacement) {
                        replacementAttributes.addItem(item, _services.game);
                    }
                }
            },
            [&]() noexcept {
                _items = std::move(replacement);
                if (creature) {
                    creature->itemAttributes() =
                        std::move(replacementAttributes);
                }
            });
        return;
    }

    std::vector<std::shared_ptr<Object>> obsolete(_items.begin(), _items.end());
    std::vector<std::shared_ptr<Item>> replacement;
    ItemAttributes replacementAttributes;
    auto creature = dynamic_cast<Creature *>(this);
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            for (const auto &itemGff : gff.getList("ItemList")) {
                auto item = _game.newOwnedItem(*itemGff, identityContext);
                item->captureSaveRecord(
                    *itemGff,
                    identityContext,
                    {originKind,
                     originOwner.empty() ? std::to_string(_id) : originOwner});
                if (forceDropable) {
                    item->setDropable(true);
                }
                item->setOwner(_id);
                appendOwnedItemCandidate(
                    replacement, item, true);
            }
            if (creature) {
                for (const auto &item : replacement) {
                    replacementAttributes.addItem(item, _services.game);
                }
            }
        },
        [&]() noexcept {
            _items = std::move(replacement);
            if (creature) {
                creature->itemAttributes() =
                    std::move(replacementAttributes);
            }
        });
}

void Object::update(float dt) {
    if (!isRuntimeLive()) {
        return;
    }
    updateActions(dt);
    updateEffects(dt);
    if (!_dead && (canExecuteActions() || currentActionKeepsConversationTime())) {
        executeActions(dt);
    }
    if (_sceneNode && _sceneNode->type() == SceneNodeType::Model) {
        std::static_pointer_cast<ModelSceneNode>(_sceneNode)->setPickable(isSelectable());
    }
}

bool Object::getLocalBoolean(int index) const {
    auto it = _localBooleans.find(index);
    return it != _localBooleans.end() ? it->second : false;
}

int Object::getLocalNumber(int index) const {
    auto it = _localNumbers.find(index);
    return it != _localNumbers.end() ? it->second : 0;
}

void Object::setLocalBoolean(int index, bool value) {
    _localBooleans[index] = value;
}

void Object::setLocalNumber(int index, int value) {
    _localNumbers[index] = value;
}
void Object::deserializeRuntimeState(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {

    _localBooleans.clear();
    _localNumbers.clear();
    if (auto variables = gff.findStruct("SWVarTable")) {
        const auto bits = variables->getList("BitArray");
        for (size_t word = 0; word < std::min<size_t>(bits.size(), 5); ++word) {
            uint32_t value = bits[word]->getUint("Variable");
            for (int bit = 0; bit < 32; ++bit) {
                if ((value & (1u << bit)) != 0) {
                    _localBooleans[static_cast<int>(word * 32 + bit)] = true;
                }
            }
        }

        const auto bytes = variables->getList("ByteArray");
        for (size_t index = 0; index < std::min<size_t>(bytes.size(), 32); ++index) {
            uint8_t value = 0;
            if (bytes[index]->readByte(value, "Variable") && value != 0) {
                _localNumbers[static_cast<int>(index)] = value;
            }
        }
    }

    _savedEffects.clear();
    _savedActionQueue = SavedActionQueue {};
    _savedRuntimeIdentityContext = identityContext;
    _savedEffectReferencesBound.clear();
    _savedActionReferencesBound.clear();
    _savedRuntimeParsed = gff.has("EffectList") || gff.has("ActionList");
    _savedRuntimePublished = false;
    _loadedSaveActionSlots.clear();
    if (_savedRuntimeParsed) {
        for (const auto &effect : gff.getList("EffectList")) {
            _savedEffects.push_back(EffectInstance::fromGff(
                *effect, identityContext));
        }
        _savedActionQueue = SavedActionQueue::fromGff(
            gff, identityContext);
        _effects.clear();
        _actions.clear();
        _delayed.clear();
        _executingAction.reset();
    }

    _savedReferenceIds.clear();
    _savedReferences.clear();
    _lastDamager.reset();
    _savedLastDamagerId.reset();
    uint32_t lastDamagerId = 0;
    if (gff.readDword(lastDamagerId, "LastDamager")) {
        _savedLastDamagerId = lastDamagerId;
    }
    static const std::array<std::string_view, 9> referenceFields {
        "AreaId",
        "CreatorId",
        "LastAttacker",
        "LastHostileActor",
        "LastPerceived",
        "MasterID",
        "OwnerId",
        "ReoneEncounter",
        "TargetId",
    };
    for (auto field : referenceFields) {
        uint32_t id = 0;
        if (gff.readDword(id, field)) {
            _savedReferenceIds.emplace(std::string(field), id);
        }
    }

    size_t perceptionIndex = 0;
    for (const auto &perception : gff.getList("PerceptionList")) {
        uint32_t id = 0;
        if (perception->readDword(id, "ObjectId")) {
            _savedReferenceIds.emplace(
                "Perception/" + std::to_string(perceptionIndex), id);
        }
        ++perceptionIndex;
    }
}

void Object::bindSavedRuntimeState() {
    if (!_savedRuntimeParsed) {
        return;
    }
    for (auto &effect : _savedEffects) {
        _savedEffectReferencesBound.push_back(
            _game.bindEffectCreator(effect));
    }
    for (auto &action : _savedActionQueue.actions) {
        _savedActionReferencesBound.push_back(
            action.bindObjectReferences(_game));
    }
}

void Object::publishSavedRuntimeState() {
    if (!_savedRuntimeParsed || _savedRuntimePublished) {
        return;
    }
    SavedScriptSituationImporter importer(
        _game, _services.resource.scripts);

    for (size_t index = 0; index < _savedEffects.size(); ++index) {
        const auto &savedEffect = _savedEffects[index];
        if (index >= _savedEffectReferencesBound.size() ||
            !_savedEffectReferencesBound[index]) {
            continue;
        }
        EffectInstance effect(savedEffect);
        if (effect.durationType() == DurationType::Temporary) {
            auto remaining = _game.remainingEffectDuration(effect);
            if (!remaining || *remaining <= 0.0f) {
                continue;
            }
            effect.remainingDuration = *remaining;
        }
        if (effect.hasStableId()) {
            _game.importEffectId(effect.id);
        } else {
            effect.id = _game.allocateEffectId();
        }
        restoreEffect(std::move(effect));
    }

    for (size_t index = 0; index < _savedActionQueue.actions.size(); ++index) {
        const auto &savedAction = _savedActionQueue.actions[index];
        if (index >= _savedActionReferencesBound.size() ||
            !_savedActionReferencesBound[index]) {
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, {}, true});
            continue;
        }
        auto action = savedAction.toRuntimeAction(_game, &importer);
        if (action) {
            action->attachSavedAction(savedAction);
            addAction(action);
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, action, false});
        } else if (savedAction.executionSupport() ==
                   SavedExecutionSupport::RepresentableButUnsupported) {
            _loadedSaveActionSlots.push_back(
                LoadedSaveActionSlot {savedAction, {}, true});
        }
    }
    _savedRuntimePublished = true;
}

void Object::captureSaveRecord(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SaveRecordOrigin origin) {
    std::optional<SerializedObjectIdentity> identity;
    uint32_t id = 0;
    if (identityContext.isSerializedState() &&
        gff.readDword(id, "ObjectId")) {
        identity = SerializedObjectIdentity {identityContext, id};
    }
    _saveRecordProvenance = SaveRecordProvenance {
        SaveGffShadow::capture(gff), std::move(origin), std::move(identity)};
}

void Object::assignSerializedObjectIdentity(
    const SerializedObjectIdentity &identity) {
    if (_saveRecordProvenance) {
        _saveRecordProvenance->identity = identity;
    } else {
        _saveRecordProvenance = SaveRecordProvenance {
            SaveGffShadow {}, SaveRecordOrigin {}, identity};
    }
}

std::vector<EffectInstance> Object::saveEffectSnapshot() const {
    // Later orchestration calls this at a stable synchronous frame boundary.
    std::vector<EffectInstance> result;
    for (const EffectInstance &effect : _effects) {
        // Equipped effects are derived from the authoritative equipment edge.
        // Persisting them independently would duplicate them on reconstruction.
        if (effect.durationType() != DurationType::Equipped) {
            result.push_back(effect);
        }
    }
    return result;
}

EffectInstance *Object::findLinkedChildInstance(EffectId linkId, const Effect &child) {
    auto it = _linkChildInstances.find(linkId);
    if (it == _linkChildInstances.end()) {
        return nullptr;
    }
    for (EffectInstance &instance : it->second) {
        if (instance.effect.get() == &child) {
            return &instance;
        }
    }
    return nullptr;
}

EffectInstance *Object::findEffectInstance(const Effect &effect) {
    auto it = std::find_if(
        _effects.begin(),
        _effects.end(),
        [&effect](const EffectInstance &instance) {
            return instance.effect.get() == &effect;
        });
    return it != _effects.end() ? &*it : nullptr;
}

std::vector<SavedActionRecord> Object::saveActionSnapshot(bool dropUnsaveable) const {
    // Loaded slots retain original order. Executed/cancelled actions cease to
    // be live as soon as they leave (or complete within) the runtime queue.
    std::vector<SavedActionRecord> result;
    std::set<const Action *> represented;
    auto unsupportedAction = [this](const Action &action, size_t queueIndex) {
        std::ostringstream message;
        message << "live queued action has no save-facing representation"
                << ": ownerId=" << id()
                << " ownerType=" << static_cast<int>(type())
                << " ownerTag=\"" << tag() << '"'
                << " ownerBlueprint=\"" << blueprintResRef() << '"'
                << " queueIndex=" << queueIndex
                << " actionType=" << static_cast<int>(action.type())
                << " runtimeClass=" << typeid(action).name()
                << " provenance="
                << (action.originalSavedAction() ? "loaded" : "runtime-created");
        if (action.originalSavedAction()) {
            message << " savedActionId=" << action.originalSavedAction()->actionId
                    << " groupActionId="
                    << action.originalSavedAction()->groupActionId;
        }
        return ValidationException(message.str());
    };
    auto saveAction = [this](const Action &action, size_t queueIndex) {
        try {
            return action.saveFacingState();
        } catch (const std::exception &ex) {
            std::ostringstream message;
            message << ex.what()
                    << "; ownerId=" << id()
                    << " ownerType=" << static_cast<int>(type())
                    << " ownerClass=" << typeid(*this).name()
                    << " ownerTag=\"" << tag() << '\"'
                    << " ownerBlueprint=\"" << blueprintResRef() << '\"'
                    << " queueIndex=" << queueIndex
                    << " actionType=" << static_cast<int>(action.type())
                    << " runtimeClass=" << typeid(action).name()
                    << " actionProvenance="
                    << (action.originalSavedAction() ? "present" : "absent");
            if (action.originalSavedAction()) {
                message << " savedActionId=" << action.originalSavedAction()->actionId
                        << " groupActionId=" << action.originalSavedAction()->groupActionId;
            }
            throw ValidationException(message.str());
        }
    };
    for (const auto &slot : _loadedSaveActionSlots) {
        if (slot.unsupportedPending) {
            result.push_back(slot.original);
            continue;
        }
        auto action = slot.runtimeAction.lock();
        bool queued = action &&
                      std::find(_actions.begin(), _actions.end(), action) != _actions.end();
        if (!queued || action->isCompleted() || action->isCancelled()) {
            continue;
        }
        represented.insert(action.get());
        auto position = std::find(_actions.begin(), _actions.end(), action);
        auto queueIndex = static_cast<size_t>(
            std::distance(_actions.begin(), position));
        if (action->isEphemeral()) {
            continue;
        }
        if (auto saved = saveAction(*action, queueIndex)) {
            result.push_back(std::move(*saved));
        } else if (!dropUnsaveable) {
            throw unsupportedAction(*action, queueIndex);
        }
    }
    for (size_t index = 0; index < _actions.size(); ++index) {
        const auto &action = _actions[index];
        if (represented.count(action.get()) != 0 ||
            action->isCompleted() || action->isCancelled() || action->isEphemeral()) {
            continue;
        }
        if (auto saved = saveAction(*action, index)) {
            result.push_back(std::move(*saved));
        } else if (!dropUnsaveable) {
            throw unsupportedAction(*action, index);
        } else {
            debug(unsupportedAction(*action, index).what() + std::string(" (dropped at a module transition)"));
        }
    }
    return result;
}

void Object::retireAreaRuntimeState(
    const std::set<const Object *> &retainedObjects) {
    // The authoritative source snapshot was captured before this boundary.
    // Discard rather than cancel: cancellation callbacks are live gameplay and
    // must not mutate the already-frozen outgoing world.
    for (auto &action : _actions) {
        // Conversation cancellation only retires its presentation admission.
        // Other actions are discarded without invoking gameplay callbacks.
        if (auto conversation = dyn_cast<StartConversationAction>(action)) {
            conversation->cancel(action, *this);
        }
        if (action) action->markCancelled();
    }
    _actions.clear();
    _delayed.clear();
    _executingAction.reset();
    _loadedSaveActionSlots.clear();
    _savedActionQueue = SavedActionQueue {};

    for (auto &effect : _effects) {
        if (effect.effect) {
            effect.effect->retireAreaRuntime(retainedObjects);
        }
        effect.retireAreaRuntimeBindings(retainedObjects);
    }
    _savedEffects.clear();
    _savedRuntimeParsed = false;
    _savedRuntimePublished = false;

    // Rebase object-local bindings exactly as effects are rebased. Master/owner
    // relations between retained session objects remain meaningful; every
    // outgoing Area binding retires. A2 separately owns the saved-graph
    // namespace, translation, and generation.
    std::map<std::string, uint32_t> retainedReferenceIds;
    std::map<std::string, RuntimeObjectRef<Object>> retainedReferences;
    for (const auto &[field, binding] : _savedReferences) {
        auto object = binding.resolve();
        if (!object || retainedObjects.count(object.get()) == 0) continue;
        retainedReferenceIds.emplace(field, object->id());
        retainedReferences.emplace(field, object);
    }
    _savedReferenceIds = std::move(retainedReferenceIds);
    _savedReferences = std::move(retainedReferences);
    _lastHostileActor.reset();
    auto lastDamager = _lastDamager.resolve();
    if (!lastDamager || retainedObjects.count(lastDamager.get()) == 0) {
        _lastDamager.reset();
        if (_savedLastDamagerId) {
            _savedLastDamagerId = script::kObjectInvalid;
        }
    } else {
        _savedLastDamagerId = lastDamager->id();
    }
}

void Object::resolveSavedReferences(
    const std::function<std::shared_ptr<Object>(uint32_t)> &resolver) {
    _savedReferences.clear();
    for (const auto &[field, id] : _savedReferenceIds) {
        if (auto object = resolver(id)) {
            _savedReferences.emplace(field, object);
        }
    }
    _lastDamager.reset();
    if (_savedLastDamagerId &&
        *_savedLastDamagerId != script::kObjectInvalid) {
        _lastDamager = resolver(*_savedLastDamagerId);
    }
}

std::shared_ptr<Object> Object::savedReference(std::string_view field) const {
    auto found = _savedReferences.find(std::string(field));
    return found == _savedReferences.end() ? nullptr : found->second.resolve();
}

uint32_t Object::getLastHostileActor() const {
    auto actor = _lastHostileActor.resolve();
    return actor ? actor->id() : script::kObjectInvalid;
}

void Object::setLastHostileActor(uint32_t actor) {
    if (actor == script::kObjectInvalid) {
        _lastHostileActor.reset();
        return;
    }
    _lastHostileActor = _game.getObjectById(actor);
}

uint32_t Object::getLastDamager() const {
    auto damager = _lastDamager.resolve();
    return damager ? damager->id() : script::kObjectInvalid;
}

void Object::setLastDamager(const std::shared_ptr<Object> &damager) {
    _lastDamager = damager;
    _savedLastDamagerId = damager
                              ? damager->id()
                              : script::kObjectInvalid;
}

void Object::clearAllActions(bool force) {
    for (auto &slot : _loadedSaveActionSlots) {
        slot.unsupportedPending = false;
    }
    // If the current front action clears the queue while it is executing, keep
    // that action and its queued continuation alive instead of trimming from the
    // back and deleting the follow-up it is about to hand off to.
    if (!force) {
        auto executingAction = _executingAction.lock();
        if (executingAction) {
            while (!_actions.empty() && _actions.front() != executingAction) {
                const std::shared_ptr<Action> &action = _actions.front();
                if (action->locked()) {
                    break;
                }
                action->cancel(action, *this);
                action->markCancelled();
                _actions.pop_front();
            }
            if (!_actions.empty() && _actions.front() == executingAction) {
                return;
            }
        }
    }

    while (!_actions.empty()) {
        const std::shared_ptr<Action> &action = _actions.back();
        if (!force && action->locked()) {
            break;
        }
        _actions.back()->cancel(action, *this);
        action->markCancelled();
        _actions.pop_back();
    }
}

void Object::removeRandomWalks(const Action &incoming) {
    // "All commands will remove a RandomWalk() from the action queue"
    // (nwscript.nss), a new random walk included.
    auto executing = _executingAction.lock();
    for (auto it = _actions.begin(); it != _actions.end();) {
        const std::shared_ptr<Action> &queued = *it;
        if (queued->type() != ActionType::RandomWalk || queued.get() == &incoming) {
            ++it;
            continue;
        }
        queued->cancel(queued, *this);
        queued->markCancelled();
        if (queued == executing) {
            // Still on the call stack: finish it and let the queue drop it.
            queued->complete();
            ++it;
        } else {
            it = _actions.erase(it);
        }
    }
}

void Object::addAction(std::shared_ptr<Action> action) {
    if (!isRuntimeLive()) return;
    if (auto conversation = dyn_cast<StartConversationAction>(action)) {
        conversation->admit();
    }
    removeRandomWalks(*action);
    _actions.push_back(std::move(action));
}

bool Object::cancelLastQueuedAction() {
    for (auto it = _actions.rbegin(); it != _actions.rend(); ++it) {
        if ((*it)->isUserAction()) {
            _actions.erase(std::next(it).base());
            return true;
        }
    }
    return false;
}

void Object::addActionOnTop(std::shared_ptr<Action> action) {
    if (!isRuntimeLive()) return;
    if (auto conversation = dyn_cast<StartConversationAction>(action)) {
        conversation->admit();
    }
    removeRandomWalks(*action);
    _actions.push_front(std::move(action));
}

void Object::delayAction(std::shared_ptr<Action> action, float seconds) {
    if (!isRuntimeLive()) return;
    DelayedAction delayed;
    delayed.action = std::move(action);
    delayed.timer = std::make_unique<Timer>(seconds);
    _delayed.push_back(std::move(delayed));
}

void Object::updateActions(float dt) {
    if (isDead()) {
        clearAllActions(/*force=*/true);
        return;
    }
    removeCompletedActions();
    updateDelayedActions(dt);
}

void Object::removeCompletedActions() {
    while (true) {
        std::shared_ptr<Action> action(getCurrentAction());
        if (!action || !action->isCompleted())
            return;

        _actions.pop_front();
    }
}

void Object::updateDelayedActions(float dt) {
    std::vector<std::shared_ptr<Action>> due;
    for (auto &delayed : _delayed) {
        delayed.timer->update(dt);
        if (delayed.timer->elapsed()) {
            due.push_back(std::move(delayed.action));
        }
    }
    auto delayedToRemove = std::remove_if(
        _delayed.begin(),
        _delayed.end(),
        [](const DelayedAction &delayed) { return delayed.timer->elapsed(); });
    _delayed.erase(delayedToRemove, _delayed.end());

    if (!canExecuteActions()) {
        // A DelayCommand is a timed event, not an action: it fires on time
        // while its owner is held. k_ptar_calocut's delayed EffectDeath on
        // a horrified Calo waited forever on the queue, and the Ebon Hawk's
        // ramp (k_ptar_ramp) stayed shut while Calo lived.
        for (auto &action : due) {
            if (action->runtimeDependenciesLive()) {
                action->execute(action, *this, 0.0f);
            }
        }
        return;
    }
    // In reverse order, so addActionOnTop keeps the original order.
    for (auto action = due.rbegin(), end = due.rend(); action != end; ++action) {
        addActionOnTop(std::move(*action));
    }
}

bool Object::currentActionKeepsConversationTime() const {
    // A held conversation participant still keeps the conversation's time:
    // k_ptar_calofade horrifies Calo for good, then queues pause, wait and
    // resume on him. Bodily actions stay suspended.
    if (!_isInConversation || _actions.empty()) {
        return false;
    }
    switch (_actions.front()->type()) {
    case ActionType::Wait:
    case ActionType::PauseConversation:
    case ActionType::ResumeConversation:
        return true;
    default:
        return false;
    }
}

void Object::executeActions(float dt) {
    if (_actions.empty()) {
        return;
    }
    std::shared_ptr<Action> action(_actions.front());
    if (!action->runtimeDependenciesLive()) {
        action->cancel(action, *this);
        action->markCancelled();
        if (!action->isCompleted()) {
            action->complete();
        }
        return;
    }
    _executingAction = action;
    try {
        action->execute(action, *this, dt);
    } catch (...) {
        _executingAction.reset();
        throw;
    }
    _executingAction.reset();
}

bool Object::hasUserActionsPending(const Action *excluded) const {
    // TODO: must only work during combat
    for (const auto &action : _actions) {
        if (action.get() != excluded && action->isUserAction()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Action> Object::getCurrentAction() const {
    return _actions.empty() ? nullptr : _actions.front();
}

std::shared_ptr<Item> Object::addItem(const std::string &resRef, int stackSize, bool dropable) {
    std::shared_ptr<Item> result;
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::vector<std::shared_ptr<Item>> replacement(_items);
    ItemAttributes replacementAttributes;
    auto creature = dyn_cast<Creature>(this);
    if (creature) replacementAttributes = creature->itemAttributes();
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            auto candidate = _game.newItemFromBlueprint(resRef);
            candidate->setStackSize(stackSize);
            candidate->setDropable(dropable);
            candidate->setOwner(_id);
            result = appendOwnedItemCandidate(
                replacement, candidate, false);
            if (creature && result.get() == candidate.get()) {
                replacementAttributes.addItem(candidate, _services.game);
            }
        },
        [&]() noexcept {
            _items = std::move(replacement);
            if (creature) {
                creature->itemAttributes() =
                    std::move(replacementAttributes);
            }
        });

    return result;
}

void Object::addItem(const std::shared_ptr<Item> &item) {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly())) {
        throw ValidationException("Cannot own a non-live runtime item");
    }
    if (item->isEquipped()) {
        throw ValidationException(
            "Cannot add an Item while it still has an equipment owner edge");
    }
    if (isActiveAreaOwnedItem(_game, item)) {
        throw ValidationException(
            "Cannot add an Item while it still has an Area ownership edge");
    }
    if (item->owner() != 0 && item->owner() != script::kObjectInvalid &&
        item->owner() != _id) {
        throw ValidationException("Runtime item already has another owner");
    }
    auto alreadyOwned = std::find(_items.begin(), _items.end(), item);
    if (alreadyOwned != _items.end()) {
        // Re-adding an owned stack restores one consumed item.
        if ((*alreadyOwned)->stackSize() < (*alreadyOwned)->maxStackSize()) {
            (*alreadyOwned)->setStackSize((*alreadyOwned)->stackSize() + 1);
        }
        return;
    }
    for (const auto &existing : _items) {
        if (!existing->isStackCompatibleWith(*item)) {
            continue;
        }
        if (existing->mergeStackFrom(*item)) {
            item->setOwner(0);
            _game.destroyRuntimeObjectGraph(item);
            return;
        }
    }
    std::vector<std::shared_ptr<Item>> replacement(_items);
    replacement.push_back(item);
    auto creature = dyn_cast<Creature>(this);
    ItemAttributes replacementAttributes;
    if (creature) {
        replacementAttributes = creature->itemAttributes();
        replacementAttributes.addItem(item, _services.game);
    }
    _items = std::move(replacement);
    item->setOwner(_id);
    if (creature) {
        creature->itemAttributes() = std::move(replacementAttributes);
    }
}

bool Object::removeItem(const std::shared_ptr<Item> &item, bool &last) {
    auto maybeItem = find(_items.begin(), _items.end(), item);
    if (maybeItem == _items.end())
        return false;

    last = false;

    int stackSize = (*maybeItem)->stackSize();
    if (stackSize > 1) {
        (*maybeItem)->setStackSize(stackSize - 1);
    } else {
        last = true;
        _items.erase(maybeItem);
        item->setOwner(0);
        if (Creature *creature = dyn_cast<Creature>(this)) {
            creature->itemAttributes().removeItem(item);
        }
    }

    return true;
}

bool Object::removeItemStack(const std::shared_ptr<Item> &item) {
    auto maybeItem = find(_items.begin(), _items.end(), item);
    if (maybeItem == _items.end()) {
        return false;
    }

    _items.erase(maybeItem);
    if (Creature *creature = dyn_cast<Creature>(this)) {
        creature->itemAttributes().removeItem(item);
    }
    item->setOwner(0);

    return true;
}

float Object::getDistanceTo(const glm::vec2 &point) const {
    return glm::distance(glm::vec2(_position), point);
}

float Object::getSquareDistanceTo(const glm::vec2 &point) const {
    return glm::distance2(glm::vec2(_position), point);
}

float Object::getDistanceTo(const glm::vec3 &point) const {
    return glm::distance(_position, point);
}

float Object::getSquareDistanceTo(const glm::vec3 &point) const {
    return glm::distance2(_position, point);
}

float Object::getDistanceTo(const Object &other) const {
    return glm::distance(_position, other._position);
}

float Object::getSquareDistanceTo(const Object &other) const {
    return glm::distance2(_position, other._position);
}

bool Object::contains(const glm::vec3 &point) const {
    if (!_sceneNode)
        return false;

    const AABB &aabb = _sceneNode->aabb();

    return (aabb * _transform).contains(point);
}

void Object::face(const Object &other) {
    if (_id != other._id) {
        face(other._position);
    }
}

void Object::face(const glm::vec3 &point) {
    if (point == _position)
        return;

    glm::vec2 dir(glm::normalize(point - _position));
    _orientation = yawQuat(-portable::atan2f(dir.x, dir.y));
    updateTransform();
}

void Object::faceAwayFrom(const Object &other) {
    if (_id == other._id || _position == other.position())
        return;

    glm::vec2 dir(glm::normalize(_position - other.position()));
    _orientation = yawQuat(-portable::atan2f(dir.x, dir.y));
    updateTransform();
}

bool Object::moveDropableItemTo(uint32_t itemId, Object &other) {
    auto it = std::find_if(_items.begin(), _items.end(), [itemId](const auto &item) {
        return item && item->id() == itemId;
    });
    if (it == _items.end() || !(*it)->isDropable()) {
        return false;
    }
    std::shared_ptr<Item> item(*it);
    _items.erase(it);
    if (Creature *creature = dyn_cast<Creature>(this)) {
        creature->itemAttributes().removeItem(item);
    }
    if (_game.party().isMember(other) && item->isCredits()) {
        _game.party().giveGold(item->stackSize());
        item->setOwner(0);
        _game.destroyRuntimeObjectGraph(item);
    } else {
        std::string tag = item->tag();
        item->setOwner(0);
        other.addItem(item);
        raisePartyItemEvents(_game, tag, item, this, &other);
    }
    return true;
}

void Object::moveDropableItemsTo(Object &other) {
    bool otherInParty = _game.party().isMember(other);
    for (auto it = _items.begin(); it != _items.end();) {
        if ((*it)->isDropable()) {
            std::shared_ptr<Item> item(*it);
            it = _items.erase(it);
            if (Creature *creature = dyn_cast<Creature>(this)) {
                creature->itemAttributes().removeItem(item);
            }
            // Credits looted by the party feed the shared gold pool instead of
            // the inventory; the stack size is the credit amount.
            if (otherInParty && item->isCredits()) {
                _game.party().giveGold(item->stackSize());
                item->setOwner(0);
                _game.destroyRuntimeObjectGraph(item);
            } else {
                std::string tag = item->tag();
                item->setOwner(0);
                other.addItem(item);
                raisePartyItemEvents(_game, tag, item, this, &other);
            }
        } else {
            ++it;
        }
    }
}

void Object::applyEffect(const std::shared_ptr<Effect> &effect, DurationType durationType, float duration) {
    if (!isRuntimeLive()) return;
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(effect)) {
        EffectInstance instance(saved->instance());
        if (instance.hasStableId()) {
            _game.importEffectId(instance.id);
        } else {
            instance.id = _game.allocateEffectId();
        }

        instance.subType = static_cast<uint16_t>(
            (instance.subType & ~static_cast<uint16_t>(0x7)) |
            static_cast<uint16_t>(durationType));
        instance.duration = duration;
        instance.remainingDuration = durationType == DurationType::Temporary
                                         ? std::optional<float>(duration)
                                         : std::nullopt;
        instance.expiryOrigin = durationType == DurationType::Temporary
                                    ? EffectExpiryOrigin::RuntimeCountdown
                                    : EffectExpiryOrigin::None;
        instance.expiryDay = 0;
        instance.expiryTime = 0;
        instance.skipOnLoad = false;

        // The save-facing instance remains authoritative. Supported retail
        // payloads execute through the canonical EffectInstance, while
        // unsupported values remain typed and queryable with a null payload.
        restoreEffect(std::move(instance));
        return;
    }

    EffectInstance instance = effect->saveFacingInstance();
    instance.effect = effect;
    instance.id = _game.allocateEffectId();
    instance.subType = static_cast<uint16_t>(
        (instance.subType & ~static_cast<uint16_t>(0x7)) |
        static_cast<uint16_t>(durationType));
    instance.duration = duration;
    if (durationType == DurationType::Temporary) {
        instance.remainingDuration = duration;
        instance.expiryOrigin = EffectExpiryOrigin::RuntimeCountdown;
    }
    instance.exposed = 1;
    restoreEffect(std::move(instance));
}

bool Object::restoreEffect(EffectInstance effect) {
    if (!effect.shouldRestoreOnLoad()) {
        return false;
    }
    if (effect.durationType() == DurationType::Instant && effect.effect) {
        if (effect.effect->onApply(*this, effect)) {
            effect.effect->onRemove(*this, effect);
        }
        return true;
    }
    _effects.push_back(std::move(effect));
    EffectId appliedId = _effects.back().id;
    bool applied = !_effects.back().effect ||
                   _effects.back().effect->onApply(*this, _effects.back());
    // A query made while a link's children were still being applied may have
    // cached children that were refused after it.
    _linkChildInstances.erase(appliedId);
    if (!applied) {
        _effects.pop_back();
        return false;
    }
    return true;
}

size_t Object::removeEffectsById(EffectId id) {
    size_t removed = 0;
    for (auto it = _effects.begin(); it != _effects.end();) {
        if (it->id == id) {
            EffectInstance removedEffect = std::move(*it);
            it = _effects.erase(it);
            if (removedEffect.effect) {
                removedEffect.effect->onRemove(*this, removedEffect);
            }
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void Object::updateEffects(float dt) {
    // Periodic payloads (poison) run first. They may apply new effects to this
    // object, so each one sees a copy and removals are made by id afterwards.
    std::vector<EffectId> finished;
    size_t count = _effects.size();
    for (size_t i = 0; i < count && i < _effects.size(); ++i) {
        std::shared_ptr<Effect> payload(_effects[i].effect);
        if (!payload) {
            continue;
        }
        EffectInstance instance(_effects[i]);
        if (!payload->onUpdate(*this, instance, dt)) {
            finished.push_back(instance.id);
        }
    }
    for (EffectId id : finished) {
        removeEffectsById(id);
    }

    for (auto it = _effects.begin(); it != _effects.end();) {
        EffectInstance &effect = *it;
        const bool retiredEquippedSource =
            effect.durationType() == DurationType::Equipped &&
            !effect.hasLiveRuntimeSource();
        bool temporary = effect.durationType() == DurationType::Temporary && effect.remainingDuration;
        if (temporary) {
            *effect.remainingDuration = glm::max(0.0f, *effect.remainingDuration - dt);
        }
        if (retiredEquippedSource ||
            (temporary && *effect.remainingDuration == 0.0f)) {
            EffectInstance removedEffect = std::move(effect);
            it = _effects.erase(it);
            if (removedEffect.effect) {
                removedEffect.effect->onRemove(*this, removedEffect);
            }
        } else {
            ++it;
        }
    }
}

void Object::playAnimation(AnimationType animation, AnimationProperties properties) {
}

bool Object::isSelectable() const {
    return false;
}

glm::vec3 Object::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    return model ? model->getWorldCenterOfAABB() : _position;
}

void Object::setRoom(Room *room) {
    if (_room) {
        _room->removeTenant(this);
    }
    _room = room;

    if (_room) {
        _room->addTenant(this);
    }
}

void Object::setPosition(const glm::vec3 &position) {
    _position = position;
    updateTransform();
}

void Object::updateTransform() {
    _transform = glm::translate(glm::mat4(1.0f), _position);
    _transform *= glm::mat4_cast(_orientation);

    if (_sceneNode && !_stunt) {
        _sceneNode->setLocalTransform(_transform);
    }
}

void Object::setFacing(float facing) {
    _orientation = yawQuat(facing);
    updateTransform();
}

void Object::setVisible(bool visible) {
    if (_visible == visible)
        return;

    _visible = visible;

    if (_sceneNode) {
        _sceneNode->setEnabled(visible);
    }
}

std::shared_ptr<Item> Object::getFirstItem() {
    _itemIndex = 0;
    return getNextItem();
}

std::shared_ptr<Item> Object::getNextItem() {
    int itemCount = static_cast<int>(_items.size());
    if (itemCount > _itemIndex) {
        return _items[_itemIndex++];
    }
    return nullptr;
}

std::shared_ptr<Item> Object::getItemByTag(const std::string &tag) {
    for (auto &item : _items) {
        if (item->tag() == tag)
            return item;
    }
    return nullptr;
}

void Object::clearAllEffects() {
    std::vector<EffectInstance> removed;
    removed.reserve(_effects.size());
    for (EffectInstance &effect : _effects) {
        if (effect.effect) {
            removed.push_back(std::move(effect));
        }
    }
    _effects.clear();

    for (const EffectInstance &effect : removed) {
        effect.effect->onRemove(*this, effect);
    }
    onEffectsCleared();
}

void Object::removeEffect(const std::shared_ptr<Effect> &effect) {
    for (auto it = _effects.begin(); it != _effects.end(); ++it) {
        if (it->effect == effect) {
            EffectInstance removed = std::move(*it);
            _effects.erase(it);
            removed.effect->onRemove(*this, removed);
            return;
        }
    }
}

namespace {

// A dynamic_cast to LinkEffectsEffect costs a type-name comparison per effect
// for every effect that is not a link, and the effect lists are scanned many
// times a tick. Only an effect typed LinkEffects can be one.
std::shared_ptr<LinkEffectsEffect> asLink(const std::shared_ptr<Effect> &effect) {
    if (!effect || effect->type() != EffectType::LinkEffects) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<LinkEffectsEffect>(effect);
}

void collectLinkChildren(
    const EffectInstance &link,
    const std::shared_ptr<Effect> &effect,
    std::vector<EffectInstance> &out) {
    if (!effect) {
        return;
    }
    if (auto nested = std::dynamic_pointer_cast<LinkEffectsEffect>(effect)) {
        // Children the target refused at apply do not count as applied.
        for (const auto &child : {nested->childEffect(), nested->parentEffect()}) {
            if (child && nested->accepts(link.id, *child)) {
                collectLinkChildren(link, child, out);
            }
        }
        return;
    }
    EffectInstance child;
    try {
        child = effect->saveFacingInstance();
    } catch (const std::exception &) {
        // Not representable as parameters: typed by the payload only.
    }
    child.effect = effect;
    child.id = link.id;
    child.subType = static_cast<uint16_t>((child.subType & ~static_cast<uint16_t>(0x7)) | (link.subType & 0x7));
    child.duration = link.duration;
    child.remainingDuration = link.remainingDuration;
    child.creatorId = link.creatorId;
    child.creator = link.creator;
    child.spellId = link.spellId;
    out.push_back(std::move(child));
}

} // namespace

std::vector<const EffectInstance *> Object::flatEffects() const {
    std::vector<const EffectInstance *> result;
    result.reserve(_effects.size());
    size_t links = 0;
    for (const EffectInstance &applied : _effects) {
        auto link = asLink(applied.effect);
        if (!link) {
            result.push_back(&applied);
            continue;
        }
        ++links;
        auto &children = _linkChildInstances[applied.id];
        if (children.empty()) {
            collectLinkChildren(applied, link, children);
        }
        for (const EffectInstance &child : children) {
            result.push_back(&child);
        }
    }
    if (_linkChildInstances.size() > links) {
        for (auto it = _linkChildInstances.begin(); it != _linkChildInstances.end();) {
            bool live = std::any_of(_effects.begin(), _effects.end(), [&it](const EffectInstance &applied) {
                return applied.id == it->first;
            });
            it = live ? std::next(it) : _linkChildInstances.erase(it);
        }
    }
    return result;
}

bool Object::hasEffect(EffectType type) const {
    if (_effects.empty()) {
        return false; // most creatures: and this is asked a few dozen times a tick
    }
    return std::any_of(
        _effects.begin(),
        _effects.end(),
        [type](const EffectInstance &applied) {
            // Settle the cheap type question first: only an effect of the type
            // asked for, or a link that may carry it, can answer yes.
            const EffectType appliedType = applied.type();
            if (appliedType != type && appliedType != EffectType::LinkEffects) {
                return false;
            }
            if (!applied.hasLiveRuntimeSource()) {
                return false;
            }
            if (appliedType == type) {
                return true;
            }
            // A linked effect carries its children's types (a linked stun
            // still stops the creature acting).
            auto link = std::dynamic_pointer_cast<LinkEffectsEffect>(applied.effect);
            return link && link->containsType(type, applied.id);
        });
}

void Object::replaceEffectState(
    std::deque<EffectInstance> replacement) noexcept {
    auto containsId = [](const auto &effects, EffectId id) {
        return std::any_of(
            effects.begin(), effects.end(),
            [id](const EffectInstance &effect) { return effect.id == id; });
    };

    for (const EffectInstance &effect : _effects) {
        if (!containsId(replacement, effect.id) && effect.effect) {
            effect.effect->onRemove(*this, effect);
        }
    }
    _effects.swap(replacement);
    for (const EffectInstance &effect : _effects) {
        if (!containsId(replacement, effect.id) && effect.effect &&
            !effect.effect->onApply(*this, effect)) {
            std::terminate();
        }
    }
}

int Object::applyDamageToHitPoints(int amount, int currentHitPoints) {
    bool minimumOne = isMinOneHP();
    int minimumHitPoints = minimumOne ? 1 : 0;
    int adjustedAmount = minimumOne
                             ? std::min(amount, std::max(0, currentHitPoints - minimumHitPoints))
                             : amount;
    _currentHitPoints = std::max(minimumHitPoints, currentHitPoints - amount);
    return adjustedAmount;
}

void Object::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
}

void Object::startStuntMode() {
    if (_sceneNode) {
        _sceneNode->setLocalTransform(glm::mat4(1.0f));
        _sceneNode->setCullingEnabled(false);
    }
    snapPresentation();
    _stunt = true;
}

void Object::stopStuntMode() {
    if (!_stunt)
        return;

    if (_sceneNode) {
        _sceneNode->setLocalTransform(_transform);
        _sceneNode->setCullingEnabled(true);
    }
    snapPresentation();
    _stunt = false;
}

std::shared_ptr<Effect> Object::getFirstEffect() {
    _effectIndex = 1;
    return !_effects.empty() ? _effects[0].effect : nullptr;
}

std::shared_ptr<Effect> Object::getNextEffect() {
    return (_effectIndex < _effects.size()) ? _effects[_effectIndex++].effect : nullptr;
}

} // namespace game

} // namespace reone
