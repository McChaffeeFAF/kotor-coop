/* Copyright (c) 2026 The reone project contributors */

// Co-op stand-ins (co-op design notes item 12, design note coop_standins). The
// host decides when a player gets one, builds it, and removes it; a client
// only builds the look of the stand-in the host announced.

#include "reone/system/portablemath.h"
#include "reone/game/coopstandin.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <boost/algorithm/string.hpp>

#include "reone/game/autolevel.h"
#include "reone/game/cameo.h"
#include "reone/game/coopcast.h"
#include "reone/game/coopreplication.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/party.h"
#include "reone/game/saveprovenance.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/gffs.h"
#include "reone/system/logutil.h"

using namespace reone::net;

namespace reone {

namespace game {

// Rules and helpers

const std::vector<CoopStandInSection> &coopStandInSections() {
    // From the survey of the K1 data (design note coop_standins). A section
    // not listed plays: stand-ins spawn and fight beside the solo character.
    static const std::vector<CoopStandInSection> sections {
        {"tar_m02ae", CoopStandInRule::Watch, "Taris Upper City cantina: the refereed one-on-one duel ring"},
        {"sta_m45ac", CoopStandInRule::Watch, "Star Forge: the final duel"},
        {"unk_m44ab", CoopStandInRule::Watch, "Rakatan Temple: the forced duel with Bastila"},
        {"unk_m44ac", CoopStandInRule::Watch, "Rakatan Temple: the forced duel with Bastila"},
        {"stunt_*", CoopStandInRule::Watch, "cutscene modules: no gameplay"},
    };
    return sections;
}

CoopStandInRule coopStandInRule(const std::string &module) {
    auto name = boost::to_lower_copy(module);
    for (const auto &section : coopStandInSections()) {
        std::string pattern(section.module);
        bool prefix = !pattern.empty() && pattern.back() == '*';
        if (prefix ? name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0 && name.size() >= pattern.size() - 1
                   : name == pattern) {
            return section.rule;
        }
    }
    return CoopStandInRule::Play;
}

const char *coopStandInRuleName(CoopStandInRule rule) {
    return rule == CoopStandInRule::Watch ? "watch" : "play";
}

std::string coopStandInResRef(int appearance, int gender, uint32_t sourceHostId, const std::string &templ) {
    auto result = std::string(kStandInPrefix) + std::to_string(appearance) + ":" + std::to_string(gender) + ":" +
                  std::to_string(sourceHostId);
    if (isCoopTemplateName(templ)) {
        result += ":" + templ;
    }
    return result;
}

bool isCoopTemplateName(const std::string &templ) {
    return !templ.empty() && templ.size() <= 16 && std::all_of(templ.begin(), templ.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string coopStandInLabel(const std::string &creatureName, const std::string &playerName) {
    if (creatureName.empty() || creatureName == playerName) {
        return playerName;
    }
    if (playerName.empty()) {
        return creatureName;
    }
    return creatureName + " (" + playerName + ")";
}

bool parseCoopStandInResRef(const std::string &resref, int &appearance, int &gender, uint32_t &sourceHostId, std::string *templ) {
    size_t prefix = std::strlen(kStandInPrefix);
    if (resref.size() <= prefix || resref.compare(0, prefix, kStandInPrefix) != 0 || resref.size() > 64) {
        return false;
    }
    std::vector<std::string> parts;
    auto body = resref.substr(prefix);
    boost::split(parts, body, boost::is_any_of(":"));
    if (parts.size() != 3 && parts.size() != 4) {
        return false;
    }
    if (parts.size() == 4 && !isCoopTemplateName(parts[3])) {
        return false;
    }
    uint64_t values[3] {};
    for (size_t i = 0; i < 3; ++i) {
        const auto &part = parts[i];
        if (part.empty() || part.size() > 10 ||
            !std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return false;
        }
        values[i] = std::stoull(part);
    }
    if (values[0] > 100000 || values[1] > 4 || values[2] == 0 || values[2] > 0xffffffffull) {
        return false;
    }
    appearance = static_cast<int>(values[0]);
    gender = static_cast<int>(values[1]);
    sourceHostId = static_cast<uint32_t>(values[2]);
    if (templ) {
        *templ = parts.size() == 4 ? parts[3] : std::string();
    }
    return true;
}

std::vector<glm::vec2> coopStandInOffsets(float facing, int index) {
    // Forward and right of the solo character. Players alternate sides; a
    // later pair stands a step further back.
    glm::vec2 forward(portable::cosf(facing), portable::sinf(facing));
    glm::vec2 right(forward.y, -forward.x);
    float side = (index % 2 == 0) ? 1.0f : -1.0f;
    float back = 1.5f + 1.0f * static_cast<float>(index / 2);
    std::vector<glm::vec2> result {
        -forward * back + right * (1.2f * side),
        right * (1.5f * side),
        -forward * (back + 0.5f),
        -forward * back - right * (1.2f * side),
        -right * (1.5f * side),
        -forward * (back + 1.5f) + right * (1.5f * side),
    };
    return result;
}

namespace {

// A built stand-in is a puppet: it runs no script and starts no conversation
// (no companion dialogue, no plot globals, no influence), and it belongs to
// nobody's party.
void scrubStandIn(Creature &creature) {
    creature.setConversation(std::string());
    creature.setOnHeartbeat(std::string());
    creature.setOnSpawn(std::string());
    creature.setOnDeath(std::string());
    creature.setOnNotice(std::string());
    creature.setOnEndRound(std::string());
    creature.setOnSpellAt(std::string());
    creature.setOnAttacked(std::string());
    creature.setOnDamaged(std::string());
    creature.setOnDisturbed(std::string());
    creature.setOnEndDialogue(std::string());
    creature.setOnRested(std::string());
    creature.setOnBlocked(std::string());
    creature.setOnDialogue(std::string());
    creature.setPlotFlag(false);
    creature.setImmortal(false);
    creature.setFaction(Faction::Player);
    creature.setPuppet(true);
}

// Everything the creature carries or wears goes away (destroyed, never moved).
void clearItems(Game &game, Creature &creature) {
    std::vector<std::shared_ptr<Item>> worn;
    for (const auto &[slot, item] : creature.equipment()) {
        if (item) {
            worn.push_back(item);
        }
    }
    for (auto &item : worn) {
        if (creature.takeEquippedItem(item)) {
            game.destroyRuntimeObjectGraph(item);
        }
    }
    std::vector<std::shared_ptr<Item>> carried(creature.items().begin(), creature.items().end());
    for (auto &item : carried) {
        creature.removeItemStack(item);
        game.destroyRuntimeObjectGraph(item);
    }
}

bool equipNew(Game &game, Creature &creature, int slot, const std::string &resRef) {
    if (resRef.empty() || slot < 0) {
        return false;
    }
    auto item = game.newItemFromBlueprint(resRef);
    if (!item) {
        return false;
    }
    if (!creature.equip(slot, item)) {
        game.destroyRuntimeObjectGraph(item);
        return false;
    }
    return true;
}

// Items of a base type the tier table lists, below the planet's tier, give
// way to the tier's item of the same type.
int tierGear(Game &game, Creature &creature, int tier) {
    int swapped = 0;
    std::vector<std::pair<int, std::shared_ptr<Item>>> worn;
    for (const auto &[slot, item] : creature.equipment()) {
        if (item) {
            worn.emplace_back(slot, item);
        }
    }
    for (auto &[slot, item] : worn) {
        auto wanted = coopTieredItem(item->baseItemType(), item->blueprintResRef(), tier);
        if (wanted.empty()) {
            continue;
        }
        auto better = game.newItemFromBlueprint(wanted);
        if (!better || !creature.takeEquippedItem(item)) {
            if (better) {
                game.destroyRuntimeObjectGraph(better);
            }
            continue;
        }
        if (creature.equip(slot, better)) {
            game.destroyRuntimeObjectGraph(item);
            ++swapped;
        } else {
            game.destroyRuntimeObjectGraph(better);
            creature.equip(slot, item);
        }
    }
    return swapped;
}

// Nothing a stand-in holds is loot: it never reaches a body bag.
void lockItems(Creature &creature) {
    for (const auto &[slot, item] : creature.equipment()) {
        if (item) {
            item->setDropable(false);
        }
    }
}

std::shared_ptr<resource::Gff> shepardGff(const CreatureClass &clazz) {
    using resource::Gff;
    const auto &shepard = coopShepard();
    const auto &defaults = clazz.defaultAttributes();
    int hp = std::max(1, clazz.hitdie());
    auto classEntry = Gff::Builder()
                          .type(2)
                          .field(Gff::Field::newInt("Class", static_cast<int>(clazz.type())))
                          .field(Gff::Field::newShort("ClassLevel", 1))
                          .build();
    auto score = [&defaults](Ability ability) { return static_cast<uint32_t>(std::max(8, defaults.getAbilityScore(ability))); };
    return Gff::Builder()
        .field(Gff::Field::newCExoLocString("FirstName", -1, shepard.firstName))
        .field(Gff::Field::newCExoLocString("LastName", -1, shepard.lastName))
        .field(Gff::Field::newWord("Appearance_Type", static_cast<uint32_t>(shepard.appearance)))
        .field(Gff::Field::newByte("Gender", static_cast<uint32_t>(shepard.gender)))
        .field(Gff::Field::newWord("PortraitId", static_cast<uint32_t>(shepard.portrait)))
        .field(Gff::Field::newWord("SoundSetFile", static_cast<uint32_t>(shepard.soundSet)))
        .field(Gff::Field::newByte("Race", 6))
        .field(Gff::Field::newByte("Str", score(Ability::Strength)))
        .field(Gff::Field::newByte("Dex", score(Ability::Dexterity)))
        .field(Gff::Field::newByte("Con", score(Ability::Constitution)))
        .field(Gff::Field::newByte("Int", score(Ability::Intelligence)))
        .field(Gff::Field::newByte("Wis", score(Ability::Wisdom)))
        .field(Gff::Field::newByte("Cha", score(Ability::Charisma)))
        .field(Gff::Field::newShort("HitPoints", hp))
        .field(Gff::Field::newShort("MaxHitPoints", hp))
        .field(Gff::Field::newShort("CurrentHitPoints", hp))
        .field(Gff::Field::newShort("ForcePoints", clazz.forcePointsPerLevel()))
        .field(Gff::Field::newShort("CurrentForce", clazz.forcePointsPerLevel()))
        .field(Gff::Field::newByte("GoodEvil", 50))
        .field(Gff::Field::newList("ClassList", {classEntry}))
        .build();
}

} // namespace

std::shared_ptr<Creature> buildCoopShepard(
    Game &game,
    ClassType clazz,
    int level,
    const std::vector<std::pair<int, const char *>> &outfit) {

    std::shared_ptr<CreatureClass> creatureClass;
    try {
        creatureClass = game.services().game.classes.get(clazz);
    } catch (const std::exception &) {
        creatureClass.reset();
    }
    if (!creatureClass) {
        return nullptr;
    }
    auto shepard = game.newCreature();
    try {
        shepard->deserialize(*shepardGff(*creatureClass), SerializedIdentityContext::templateResource(kCoopShepardTemplate));
    } catch (const std::exception &e) {
        // The record is read before the model: without the model he still plays.
        warn(std::string("Co-op: John Shepard's appearance did not load: ") + e.what());
    }
    if (coopShepard().headRow >= 0) {
        shepard->setHeadOverride(coopShepard().headRow);
    }
    scrubStandIn(*shepard);
    shepard->setPuppet(false); // a puppet only as a stand-in
    for (const auto &[slot, item] : outfit) {
        if (item) {
            equipNew(game, *shepard, slot, item);
        }
    }
    autoLevelCreature(game, *shepard, level);
    shepard->setCurrentHitPoints(shepard->maxHitPoints());
    shepard->setCurrentForcePoints(shepard->maxForcePoints());
    return shepard;
}

// Host

bool CoopSession::isStandIn(uint32_t creatureId) const {
    return isHost() && creatureId != 0 && _standInSource.count(creatureId) != 0;
}

void CoopSession::hostRegisterCameo(uint32_t creatureId) {
    if (!isHost() || !creatureId) {
        return;
    }
    // Its own source: no guest owns it. The wire carries the cameo template.
    _standInSource[creatureId] = creatureId;
    _standInTemplate[creatureId] = kCoopCameoTemplate;
}

void CoopSession::hostForgetCameo(uint32_t creatureId) {
    if (!isHost() || !creatureId) {
        return;
    }
    hostReleaseStandInIds(creatureId);
}

bool CoopSession::hostGuestPlaysShepard(const std::string &module) const {
    if (!isHost()) {
        return false;
    }
    auto name = boost::to_lower_copy(module);
    for (const auto &[connection, client] : _clients) {
        if (client.welcomed && client.cast.kind == CoopCastKind::Generic && boost::to_lower_copy(client.castModule) == name) {
            return true;
        }
    }
    return false;
}

uint32_t CoopSession::hostControlledId(const HostClient &client) const {
    if (client.slot.status == SlotStatus::Assigned) {
        return client.slot.creatureId;
    }
    return client.guestId ? client.guestId : client.standIn;
}

SlotAssign CoopSession::hostEffectiveSlot(const HostClient &client) const {
    if (client.guestId) {
        SlotAssign slot;
        slot.status = SlotStatus::Guest;
        slot.npc = -1;
        slot.creatureId = client.guestId;
        if (auto object = _game.getObjectById(client.guestId)) {
            slot.tag = object->tag().substr(0, kMaxStringBytes);
        }
        return slot;
    }
    if (client.standIn && client.slot.status != SlotStatus::Assigned && client.slot.status != SlotStatus::Declined) {
        SlotAssign slot;
        slot.status = SlotStatus::StandIn;
        slot.npc = -1;
        slot.creatureId = client.standIn;
        if (auto object = _game.getObjectById(client.standIn)) {
            slot.tag = object->tag().substr(0, kMaxStringBytes);
        }
        return slot;
    }
    return client.slot;
}

void CoopSession::hostSendSlot(HostClient &client, bool force) {
    auto slot = hostEffectiveSlot(client);
    const auto &sent = client.slotSent;
    if (!force && client.slotSentValid && sent.status == slot.status && sent.creatureId == slot.creatureId &&
        sent.npc == slot.npc && sent.tag == slot.tag) {
        return;
    }
    client.slotSent = slot;
    client.slotSentValid = true;
    send(client.connection, encode(slot));
}

std::string CoopSession::hostStandInPause() const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return "no_area";
    }
    if (!_game._nextModule.empty()) {
        return "transition";
    }
    if (_game._movie) {
        return "movie";
    }
    if (_hostDefeated) {
        return "defeat";
    }
    // The host's own screens (inventory, equipment, character, map, journal,
    // containers, stores, benches, saving) are personal: nobody else is held.
    bool personal = coopPersonalConversation(_game);
    switch (_game._screen) {
    case Game::Screen::InGame:
    case Game::Screen::InGameMenu:
    case Game::Screen::Container:
    case Game::Screen::Store:
    case Game::Screen::Upgrade:
    case Game::Screen::SaveLoad:
        break;
    case Game::Screen::Conversation:
        if (personal) {
            break; // a workbench's own talk before its screen
        }
        return "conversation";
    default:
        return "screen";
    }
    auto leader = _game.party().getLeader();
    if (!leader || leader->isDead()) {
        return "no_leader";
    }
    if (leader->isInConversation() && !personal) {
        return "conversation";
    }
    if (!leader->isCommandable()) {
        // A script holds the solo character for a scene.
        return "cutscene";
    }
    if (coopStandInRule(module->name()) == CoopStandInRule::Watch) {
        return "watch";
    }
    if (_game.party().isSoloMode()) {
        // SetSoloMode: the story means the solo character to be alone here
        // (the final duels set it).
        return "solo_mode";
    }
    return std::string();
}

void CoopSession::hostUpdateStandIns() {
    if (_inModuleSnapshot) {
        return;
    }
    _standInPause = hostStandInPause();
    auto leader = _game.party().getLeader();
    int index = 0;
    for (auto &[connection, client] : _clients) {
        if (!client.welcomed) {
            continue;
        }
        // Design item 12 and "Lost companion": a player with no companion
        // free plays a stand-in, once the host has let them in.
        bool wants = client.slotRequested && client.slotAnswered && client.slot.status != SlotStatus::Assigned &&
                     client.slot.status != SlotStatus::Declined && client.approval != HostClient::Approval::Pending &&
                     client.approval != HostClient::Approval::Declined;
        std::shared_ptr<Creature> existing = client.standIn ? _game.getObjectById<Creature>(client.standIn) : nullptr;
        if (client.standIn && (!existing || !existing->isRuntimeLive())) {
            hostReleaseStandInIds(client.standIn);
            client.standIn = 0;
            existing.reset();
        }
        bool out = wants && _standInPause.empty() && leader;
        if (existing && out && !_standInTemplate.count(existing->id())) {
            // The party is down to someone else now (a script handed control
            // to another character): a clone becomes a copy of them.
            auto source = _standInSource.find(existing->id());
            if (source == _standInSource.end() || source->second != leader->id()) {
                hostDespawnStandIn(client, false);
                existing.reset();
            }
        }
        if (existing && out && _standInTemplate.count(existing->id())) {
            // The party levelled: so does a copy or the character of the place.
            int target = hostPartyLevel();
            if (existing->attributes().getAggregateLevel() < target) {
                int maxBefore = existing->maxHitPoints();
                int added = autoLevelCreature(_game, *existing, target);
                existing->setCurrentHitPoints(existing->currentHitPoints() + std::max(0, existing->maxHitPoints() - maxBefore));
                existing->setCurrentForcePoints(existing->maxForcePoints());
                client.castLevel = existing->attributes().getAggregateLevel();
                if (added) {
                    info("Co-op: " + client.name + "'s stand-in levels with the party to " + std::to_string(client.castLevel));
                }
            }
        }
        if (!out && existing) {
            hostDespawnStandIn(client, true);
        } else if (out && !existing) {
            hostSpawnStandIn(client, *leader, index);
        }
        if (wants) {
            ++index;
        }
        if (client.slotAnswered) {
            hostSendSlot(client, false);
        }
    }
}

std::shared_ptr<Creature> CoopSession::hostSpawnStandIn(HostClient &client, const Creature &source, int index) {
    auto module = _game.module();
    if (!module || !module->area()) {
        return nullptr;
    }
    auto &area = *module->area();
    std::shared_ptr<Creature> standIn;
    std::string templ;
    const auto &cast = hostDecideCast(client);
    if (cast.kind == CoopCastKind::Copy || cast.kind == CoopCastKind::Generic) {
        standIn = hostBuildCastStandIn(client, cast, templ);
        if (!standIn) {
            client.cast.kind = CoopCastKind::Clone;
            client.cast.reason += "; its build failed, so a copy of the solo character";
        }
    }
    if (!standIn) try {
        standIn = _game.newCreature();
        standIn->setTag("coop_standin_" + std::to_string(client.clientId));
        // The guest's own name: tags, hover and the HUD show who plays it.
        standIn->setName(client.name);
        standIn->setGender(source.gender());
        standIn->setAppearance(source.appearance());
        if (source.headRow() >= 0) {
            standIn->setHeadOverride(source.headRow());
        }
        try {
            standIn->loadAppearance();
        } catch (const std::exception &e) {
            // Without its model it still plays; the client builds its own look.
            warn(std::string("Co-op: the stand-in's appearance did not load: ") + e.what());
        }
        standIn->setFaction(Faction::Player);
        // A copy of the solo character: class levels, abilities, skills,
        // feats and powers, hit and force points, and the equipped gear.
        standIn->attributes() = source.attributes();
        standIn->setGoodEvil(source.goodEvil());
        standIn->setMaxHitPoints(source.hitPoints());
        standIn->setCurrentHitPoints(standIn->maxHitPoints());
        standIn->addBaseForcePoints(source.forcePoints());
        standIn->setCurrentForcePoints(standIn->maxForcePoints());
        for (const auto &[slot, item] : source.equipment()) {
            if (!item) {
                continue;
            }
            auto copy = _game.newItemClone(*item);
            if (copy && !standIn->equip(slot, copy)) {
                _game.destroyRuntimeObjectGraph(copy);
            }
        }
        // Not in the party table (K2's puppet mechanism): party counts and
        // checks still see the solo character alone.
        standIn->setPuppet(true);
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build a stand-in: ") + e.what());
        return nullptr;
    }
    // Back after a scene or a save: where it stood, while that spot still
    // joins the solo character; else beside the solo character.
    bool returned = hostPlaceAtAwaySpot(client, *standIn, source);
    bool placed = returned;
    for (const auto &offset : coopStandInOffsets(source.getFacing(), index)) {
        if (placed) {
            break;
        }
        standIn->setPosition(source.position() + glm::vec3(offset, 0.0f));
        if (area.landObject(*standIn)) {
            placed = true;
        }
    }
    if (!placed) {
        standIn->setPosition(source.position());
    }
    if (!returned) {
        standIn->setFacing(source.getFacing());
    }
    area.add(standIn);
    if (client.standInHp > 0) {
        // Back after a conversation or a save in the same module visit: the
        // stand-in keeps its wounds.
        standIn->setCurrentHitPoints(std::min(client.standInHp, standIn->maxHitPoints()));
    }
    client.standInHp = 0;
    client.standIn = standIn->id();
    _standInSource[standIn->id()] = source.id();
    client.castTemplate = templ;
    client.castLevel = standIn->attributes().getAggregateLevel();
    if (!templ.empty()) {
        _standInTemplate[standIn->id()] = templ;
    }
    hostMarkApproved(client); // only a player the host let in gets a stand-in
    if (templ.empty()) {
        info("Co-op: " + client.name + " plays a stand-in of " + source.tag() + " (id " + std::to_string(standIn->id()) + ")" +
             (placed ? "" : " on the solo character's spot"));
    } else {
        info("Co-op: " + client.name + " plays " + standIn->name() + " (" + coopCastKindName(client.cast.kind) + ", " + templ +
             ", level " + std::to_string(client.castLevel) + ", id " + std::to_string(standIn->id()) + "): " + client.cast.reason);
        if (templ == kCoopShepardTemplate && client.cast.planet && client.shepardPlanets.insert(client.cast.planet).second) {
            // That guy again: a line from his sound set on each new planet.
            standIn->playSound(resource::SoundSetEntry::RejoinParty);
        }
    }
    return standIn;
}

void CoopSession::hostDespawnStandIn(HostClient &client, bool remember) {
    if (!client.standIn) {
        return;
    }
    uint32_t id = client.standIn;
    client.standIn = 0;
    hostReleaseStandInIds(id);
    client.pastStandIns.push_back(id);
    if (client.pastStandIns.size() > 8) {
        client.pastStandIns.erase(client.pastStandIns.begin());
    }
    if (client.viewing) {
        hostSendContainer(client, false);
        client.viewing = 0;
        client.viewSent.clear();
    }
    auto creature = _game.getObjectById<Creature>(id);
    auto module = _game.module();
    if (!creature || !module || !module->area()) {
        return;
    }
    client.standInHp = remember && !creature->isDead() ? creature->currentHitPoints() : 0;
    if (remember) {
        hostRememberAwaySpot(client, *creature);
    } else {
        client.away = HostClient::AwaySpot();
    }
    creature->clearAllActions();
    module->area()->destroyObject(*creature);
    flushDestroyedObjects(*module->area());
    info("Co-op: " + client.name + "'s stand-in went away (" + (_standInPause.empty() ? "replaced" : _standInPause) + ")");
}

// Who a stand-in is (coopcast.h)

int CoopSession::hostPartyLevel() const {
    auto player = _game.party().player();
    if (!player) {
        player = _game.party().getLeader();
    }
    return player ? std::max(1, player->attributes().getAggregateLevel()) : 1;
}

CoopCastInput CoopSession::hostCastInput(const HostClient &client) const {
    CoopCastInput input;
    auto module = _game.module();
    input.module = module ? module->name() : std::string();
    auto &party = _game.party();
    for (const auto &member : party.members()) {
        if (member.creature) {
            input.partyRoles.insert(coopClassRole(member.creature->attributes().getEffectiveClass()));
        }
        if (member.npc != kNpcPlayer) {
            input.partyNpcs.insert(member.npc);
        }
    }
    for (const auto &[connection, other] : _clients) {
        if (&other == &client || !other.welcomed || other.castModule != input.module) {
            continue;
        }
        if (other.cast.kind == CoopCastKind::Copy) {
            input.takenNpcs.insert(other.cast.npc);
        }
        if (other.cast.kind == CoopCastKind::Copy || other.cast.kind == CoopCastKind::Generic) {
            input.partyRoles.insert(other.cast.role);
        }
    }
    // The companions the module has as NPCs (their tags, templates or names).
    if (module && module->area()) {
        for (const auto &object : module->area()->objects()) {
            if (!object || object->type() != ObjectType::Creature || !object->isRuntimeLive() || party.isMember(*object) ||
                _standInSource.count(object->id())) {
                continue;
            }
            auto tag = boost::to_lower_copy(object->tag());
            auto templ = boost::to_lower_copy(object->blueprintResRef());
            auto name = boost::to_lower_copy(static_cast<const Creature &>(*object).name());
            for (const auto &entry : coopRoster()) {
                auto first = boost::to_lower_copy(std::string(entry.name));
                bool named = name == first || name.rfind(first + " ", 0) == 0;
                if (named || tag.find(entry.key) != std::string::npos || templ.find(entry.key) != std::string::npos) {
                    input.presentNpcs.insert(entry.npc);
                }
            }
        }
    }
    input.globalBoolean = [this](const std::string &name) { return _game.getGlobalBoolean(name); };
    return input;
}

const CoopCastChoice &CoopSession::hostDecideCast(HostClient &client) {
    auto module = _game.module();
    std::string name = module ? module->name() : std::string();
    if (client.cast.kind != CoopCastKind::None && client.castModule == name) {
        if (client.cast.kind != CoopCastKind::Copy) {
            return client.cast;
        }
        auto why = coopCopyExclusion(hostCastInput(client), client.cast.npc);
        if (why.empty()) {
            return client.cast; // the same character across conversations and saves
        }
        info("Co-op: " + client.name + "'s " + client.cast.name + " no longer fits: " + why);
    }
    client.cast = chooseCoopCast(hostCastInput(client));
    client.castModule = name;
    std::string skipped;
    for (const auto &[who, why] : client.cast.skipped) {
        skipped += (skipped.empty() ? "" : "; ") + who + " " + why;
    }
    info("Co-op: " + client.name + " gets " + coopCastKindName(client.cast.kind) + " " + client.cast.name + ": " +
         client.cast.reason + (skipped.empty() ? std::string() : " (not: " + skipped + ")"));
    return client.cast;
}

std::shared_ptr<Creature> CoopSession::hostBuildCastStandIn(HostClient &client, const CoopCastChoice &cast, std::string &templ) {
    std::shared_ptr<Creature> standIn;
    int level = hostPartyLevel();
    try {
        if (cast.kind == CoopCastKind::Generic) {
            const auto *generic = coopGenericFor(client.castModule);
            if (!generic) {
                return nullptr;
            }
            standIn = buildCoopShepard(_game, generic->clazz, level, generic->kit);
            if (!standIn) {
                return nullptr;
            }
            templ = kCoopShepardTemplate;
        } else {
            auto utc = _game.services().resource.gffs.get(cast.templ, resource::ResType::Utc);
            if (!utc) {
                warn("Co-op: no template " + cast.templ + " for " + client.name + "'s stand-in");
                return nullptr;
            }
            standIn = _game.newCreature();
            try {
                standIn->loadFromBlueprint(cast.templ);
            } catch (const std::exception &e) {
                // Without its model it still plays; the client builds its own look.
                warn("Co-op: " + cast.templ + "'s appearance did not load: " + e.what());
            }
            // The template's own gear, slot by slot (Equip_ItemList: the struct
            // id is the slot's bit), then what the story hands the companion.
            clearItems(_game, *standIn);
            for (const auto &entry : utc->getList("Equip_ItemList")) {
                uint32_t bits = entry->type();
                if (bits == 0 || (bits & (bits - 1)) != 0) {
                    continue;
                }
                int slot = 0;
                while (bits > 1) {
                    bits >>= 1;
                    ++slot;
                }
                equipNew(_game, *standIn, slot, boost::to_lower_copy(entry->getString("EquippedRes")));
            }
            if (const auto *entry = coopRosterEntry(cast.npc); entry && entry->kitItem && !standIn->getEquippedItem(entry->kitSlot)) {
                equipNew(_game, *standIn, entry->kitSlot, entry->kitItem);
            }
            tierGear(_game, *standIn, cast.tier);
            scrubStandIn(*standIn);
            autoLevelCreature(_game, *standIn, level);
            templ = cast.templ;
        }
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build ") + cast.name + " for " + client.name + ": " + e.what());
        if (standIn) {
            _game.destroyRuntimeObjectGraph(standIn);
        }
        return nullptr;
    }
    standIn->setTag("coop_standin_" + std::to_string(client.clientId));
    standIn->setPuppet(true);
    lockItems(*standIn);
    standIn->setCurrentHitPoints(standIn->maxHitPoints());
    standIn->setCurrentForcePoints(standIn->maxForcePoints());
    return standIn;
}

void CoopSession::hostReleaseStandInIds(uint32_t id) {
    _standInSource.erase(id);
    _standInTemplate.erase(id);
}

bool CoopSession::isStandInItem(uint32_t itemId) const {
    if (!isHost() || !itemId || freeMode()) {
        return false;
    }
    for (const auto &[connection, client] : _clients) {
        auto creature = client.standIn ? _game.getObjectById<Creature>(client.standIn) : nullptr;
        if (!creature) {
            continue;
        }
        for (const auto &[slot, item] : creature->equipment()) {
            if (item && item->id() == itemId) {
                return true;
            }
        }
        for (const auto &item : creature->items()) {
            if (item && item->id() == itemId) {
                return true;
            }
        }
    }
    return false;
}

void CoopSession::beginModuleSnapshot() {
    if (!isHost() || _inModuleSnapshot) {
        return;
    }
    _inModuleSnapshot = true;
    // Stand-ins are never saved: they are rebuilt beside the solo character.
    auto pause = _standInPause;
    _standInPause = "snapshot";
    for (auto &[connection, client] : _clients) {
        hostDespawnStandIn(client, true);
        if (client.guestId) {
            // Guests keep their state in the host's record, never in the save.
            hostDespawnGuest(client);
        }
    }
    _standInPause = pause;
    // Scaled enemies are saved with their base hit points (design item 6:
    // co-op must not leak into the host's save). The exact live values come
    // back afterwards.
    _snapshotHp.clear();
    for (const auto &[id, scaled] : _scaled) {
        auto creature = _game.getObjectById<Creature>(id);
        if (!creature || !creature->isRuntimeLive() || scaled.factor == 1.0f) {
            continue;
        }
        int base = creature->hitPoints();
        int current = creature->currentHitPoints();
        _snapshotHp[id] = {base, current};
        int maximum = creature->maxHitPoints();
        creature->setMaxHitPoints(scaled.base);
        // The same share of the pool as now, never below 1 for a live one.
        int restored = maximum > 0 ? static_cast<int>(std::lround(static_cast<double>(current) * creature->maxHitPoints() / maximum))
                                   : current;
        creature->setCurrentHitPoints(current > 0 ? std::max(1, restored) : current);
    }
}

void CoopSession::endModuleSnapshot() {
    if (!_inModuleSnapshot) {
        return;
    }
    for (const auto &[id, hp] : _snapshotHp) {
        if (auto creature = _game.getObjectById<Creature>(id)) {
            creature->setMaxHitPoints(hp.base);
            creature->setCurrentHitPoints(hp.current);
        }
    }
    _snapshotHp.clear();
    _inModuleSnapshot = false;
}

// Client

std::shared_ptr<Object> CoopSession::clientSpawnStandIn(Replica &replica) {
    const auto &s = replica.state;
    int appearance = 0;
    int gender = 0;
    uint32_t source = 0;
    std::string templ;
    auto module = _game.module();
    if (!module || !module->area() || !parseCoopStandInResRef(s.resref, appearance, gender, source, &templ)) {
        return nullptr;
    }
    std::shared_ptr<Creature> standIn;
    try {
        if (templ == kCoopShepardTemplate || templ == kCoopCameoTemplate) {
            // John Shepard: his identity from the table; the outfit the host
            // gave him here follows with the look.
            standIn = buildCoopShepard(_game, ClassType::Soldier, 1, {});
        } else if (!templ.empty() && _game.services().resource.gffs.get(templ, resource::ResType::Utc)) {
            // A companion copy: name, portrait, sound set from this install's template.
            standIn = _game.newCreature();
            standIn->loadFromBlueprint(templ);
            clearItems(_game, *standIn);
        }
        if (standIn) {
            scrubStandIn(*standIn);
        } else {
            standIn = _game.newCreature();
            auto local = std::dynamic_pointer_cast<Creature>(localObjectForHost(source));
            // The player who plays it; the host's copy of the look follows in apply().
            standIn->setName(!s.look.playerName.empty() ? s.look.playerName : (local ? local->name() : std::string("Stand-in")));
        }
        standIn->setTag(s.tag);
        standIn->setGender(static_cast<Gender>(gender));
        standIn->setAppearance(appearance);
        if (s.look.headRow >= 0) {
            standIn->setHeadOverride(s.look.headRow);
        }
        standIn->loadAppearance();
        standIn->setFaction(Faction::Player);
        standIn->setPuppet(true);
        if (templ == kCoopCameoTemplate) {
            // A cameo on this screen too: clicking him barks, nothing more.
            makeCameo(_game, *standIn, nullptr);
        }
    } catch (const std::exception &e) {
        warn(std::string("Co-op: cannot build the stand-in: ") + e.what());
        return nullptr;
    }
    standIn->setPosition(glm::vec3(s.position.x, s.position.y, s.position.z));
    standIn->setFacing(s.facing);
    module->area()->add(standIn);
    replica.spawned = true;
    if (templ == kCoopShepardTemplate) {
        const auto *planet = coopPlanetFor(module->name());
        if (planet && _clientShepardPlanets.insert(planet->name).second) {
            standIn->playSound(resource::SoundSetEntry::RejoinParty);
        }
    }
    return standIn;
}

} // namespace game

} // namespace reone
