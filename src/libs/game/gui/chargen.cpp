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

#include "reone/game/coopsession.h"
#include "reone/game/gui/chargen.h"
#include "reone/resource/k2merge.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/d20/forms.h"

#include "reone/graphics/di/services.h"
#include "reone/gui/control/label.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"

#include "reone/game/d20/classes.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/portraits.h"
#include "reone/game/types.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kModelScale = 1.1f;

CharacterGeneration::CharacterGeneration(Game &game, ServicesView &services) :
    GameGUI(game, services) {
    _resRef = guiResRef("maincg");

    if (game.isTSL()) {
        _musicResRef = "mus_main";
        _loadScreenResRef = "load_default";
    } else {
        _musicResRef = "mus_theme_rep";
        _loadScreenResRef = "load_chargen";
    }
}

void CharacterGeneration::onGUILoaded() {
    loadBackground(BackgroundType::Menu);
    bindControls();

    _controls.LBL_LEVEL_VAL->setVisible(false);
    _controls.LBL_NAME->setTextMessage("");

    if (!isTSL()) {
        _controls.LBL_LEVEL->setVisible(false);
        _controls.VIT_ARROW_LBL->setVisible(false);
        _controls.DEF_ARROW_LBL->setVisible(false);
        _controls.FORT_ARROW_LBL->setVisible(false);
        _controls.REFL_ARROW_LBL->setVisible(false);
        _controls.WILL_ARROW_LBL->setVisible(false);
        _controls.OLD_LBL->setVisible(false);
        _controls.NEW_LBL->setVisible(false);
    }

    loadClassSelection();
    loadQuickOrCustom();
    loadQuick();
    loadPortraitSelection();
    loadNameEntry();
    loadCustom();
    loadAbilities();
    loadSkills();
    loadFeats();
    loadPowers();
    loadLevelUp();
    loadPrestige();
}

void CharacterGeneration::loadClassSelection() {
    _classSelection = std::make_unique<ClassSelection>(*this, _game, _services);
    _classSelection->init();
}

void CharacterGeneration::loadQuickOrCustom() {
    _quickOrCustom = std::make_unique<QuickOrCustom>(*this, _game, _services);
    _quickOrCustom->init();
}

void CharacterGeneration::loadQuick() {
    _quick = std::make_unique<QuickCharacterGeneration>(*this, _game, _services);
    _quick->init();
}

void CharacterGeneration::loadCustom() {
    _custom = std::make_unique<CustomCharacterGeneration>(*this, _game, _services);
    _custom->init();
}

void CharacterGeneration::loadPortraitSelection() {
    _portraitSelection = std::make_unique<PortraitSelection>(*this, _game, _services);
    _portraitSelection->init();
}

void CharacterGeneration::loadAbilities() {
    _abilities = std::make_unique<CharGenAbilities>(*this, _game, _services);
    _abilities->init();
}

void CharacterGeneration::loadSkills() {
    _charGenSkills = std::make_unique<CharGenSkills>(*this, _game, _services);
    _charGenSkills->init();
}

void CharacterGeneration::loadFeats() {
    _charGenFeats = std::make_unique<CharGenFeats>(*this, _game, _services);
    _charGenFeats->init();
}

void CharacterGeneration::loadPowers() {
    _charGenPowers = std::make_unique<CharGenPowers>(*this, _game, _services);
    _charGenPowers->init();
}

void CharacterGeneration::loadNameEntry() {
    _nameEntry = std::make_unique<NameEntry>(*this, _game, _services);
    _nameEntry->init();
}

void CharacterGeneration::loadLevelUp() {
    _levelUp = std::make_unique<LevelUpMenu>(*this, _game, _services);
    _levelUp->init();
}

void CharacterGeneration::loadPrestige() {
    _prestige = std::make_unique<PrestigeSelection>(*this, _game, _services);
    _prestige->init();
}

bool CharacterGeneration::handle(const input::Event &event) {
    if (getSubGUI()->handle(event)) {
        return true;
    }
    return _gui->handle(event);
}

GameGUI *CharacterGeneration::getSubGUI() const {
    switch (_screen) {
    case CharGenScreen::ClassSelection:
        return _classSelection.get();
    case CharGenScreen::QuickOrCustom:
        return _quickOrCustom.get();
    case CharGenScreen::Quick:
        return _quick.get();
    case CharGenScreen::Custom:
        return _custom.get();
    case CharGenScreen::PortraitSelection:
        return _portraitSelection.get();
    case CharGenScreen::Abilities:
        return _abilities.get();
    case CharGenScreen::Skills:
        return _charGenSkills.get();
    case CharGenScreen::Feats:
        return _charGenFeats.get();
    case CharGenScreen::Powers:
        return _charGenPowers.get();
    case CharGenScreen::Name:
        return _nameEntry.get();
    case CharGenScreen::LevelUp:
        return _levelUp.get();
    case CharGenScreen::Prestige:
        return _prestige.get();
    default:
        throw std::logic_error("Invalid screen: " + std::to_string(static_cast<int>(_screen)));
    }
}

void CharacterGeneration::update(float dt) {
    GameGUI::update(dt);
    getSubGUI()->update(dt);
}

void CharacterGeneration::render() {
    GameGUI::render();
    getSubGUI()->render();
}

void CharacterGeneration::openClassSelection() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::ClassSelection);
}

void CharacterGeneration::changeScreen(CharGenScreen screen) {
    auto gui = getSubGUI();
    if (gui) {
        gui->clearSelection();
    }
    _screen = screen;
}

void CharacterGeneration::openQuickOrCustom() {
    setAttributesVisible(false);
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::QuickOrCustom);
}

void CharacterGeneration::setAttributesVisible(bool visible) {
    std::vector<Label *> attributesLabels {
        _controls.LBL_VIT.get(),
        _controls.LBL_DEF.get(),
        _controls.STR_AB_LBL.get(),
        _controls.DEX_AB_LBL.get(),
        _controls.CON_AB_LBL.get(),
        _controls.INT_AB_LBL.get(),
        _controls.WIS_AB_LBL.get(),
        _controls.CHA_AB_LBL.get()};
    if (!isTSL()) {
        attributesLabels.push_back(_controls.OLD_FORT_LBL.get());
        attributesLabels.push_back(_controls.OLD_REFL_LBL.get());
        attributesLabels.push_back(_controls.OLD_WILL_LBL.get());
    } else {
        attributesLabels.push_back(_controls.NEW_FORT_LBL.get());
        attributesLabels.push_back(_controls.NEW_REFL_LBL.get());
        attributesLabels.push_back(_controls.NEW_WILL_LBL.get());
    }
    for (auto &label : attributesLabels) {
        label->setVisible(visible);
    }
}

void CharacterGeneration::startCustom() {
    _type = Type::Custom;
    _custom->setStep(0);
    openSteps();
}

void CharacterGeneration::startQuick() {
    setAttributesVisible(true);
    _type = Type::Quick;
    _quick->setStep(0);
    openSteps();
}

void CharacterGeneration::startLevelUp(const std::shared_ptr<Creature> &member) {
    _type = Type::LevelUp;

    std::shared_ptr<Creature> partyLeader(member ? member : _game.party().getLeader());
    _levelUpMember = partyLeader;

    Character character;
    character.name = partyLeader->name();
    character.appearance = partyLeader->appearance();
    character.gender = partyLeader->gender();
    character.attributes = partyLeader->attributes();
    setCharacter(std::move(character));

    int nextLevel = partyLeader->attributes().getAggregateLevel() + 1;
    bool hasAttributes = nextLevel % 4 == 0;
    _levelUp->setStep(hasAttributes ? 0 : 1);

    // K2 prestige classes (design note k2_merge): once unlocked, a level-up
    // starts with the choice.
    // A co-op guest's own character is a player character to the host's rules.
    auto *coop = _game.coop();
    auto offered = _game.prestigeOffersFor(*partyLeader, coop && coop->isClient());
    if (!offered.empty()) {
        _prestige->reset(std::move(offered));
        openPrestige();
        return;
    }

    openSteps();
}

void CharacterGeneration::openPrestige() {
    setAttributesVisible(true);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Prestige);
}

void CharacterGeneration::choosePrestigeClass(ClassType type) {
    std::shared_ptr<CreatureClass> clazz;
    try {
        clazz = _services.game.classes.get(type);
    } catch (const std::exception &) {
        clazz.reset();
    }
    auto member = levelUpMember();
    if (!clazz || !member) {
        declinePrestigeClass();
        return;
    }
    Character character(_character);
    if (character.attributes.addPrestigeClass(clazz.get())) {
        // The Grey feat (ours): taken while in the neutral band.
        if (prestige::grantsGreyFeat(member->goodEvil())) {
            if (auto *merge = _services.resource.k2Merge) {
                int grey = merge->ownRowIndex("feat", prestige::kGreyFeatLabel);
                if (grey >= 0) {
                    character.attributes.addFeat(static_cast<FeatType>(grey));
                }
            }
        }
    }
    setCharacter(std::move(character));
    int nextLevel = _character.attributes.getAggregateLevel() + 1;
    _levelUp->setStep(nextLevel % 4 == 0 ? 0 : 1);
    openSteps();
}

void CharacterGeneration::declinePrestigeClass() {
    openSteps();
}

void CharacterGeneration::grantDueForms(CreatureAttributes &attributes, int goodEvil) const {
    if (!_services.resource.k2Merge) {
        return;
    }
    ClassType line = ClassType::Invalid;
    int forceLevel = 0;
    for (const auto &[clazz, level] : attributes.classLevels()) {
        if (clazz->forcePointsPerLevel() <= 0) {
            continue;
        }
        forceLevel += level;
        if (prestige::isPrestigeClass(clazz->type()) || line == ClassType::Invalid) {
            line = clazz->type();
        }
    }
    for (int form : forms::formsDue(line, forceLevel, goodEvil)) {
        attributes.addSpell(static_cast<SpellType>(form));
    }
}

std::shared_ptr<Creature> CharacterGeneration::levelUpMember() const {
    if (auto member = _levelUpMember.resolve()) {
        return member;
    }
    return _game.party().getLeader();
}

void CharacterGeneration::openSteps() {
    switch (_type) {
    case Type::LevelUp:
        openLevelUp();
        break;
    case Type::Custom:
        openCustom();
        break;
    default:
        openQuick();
        break;
    }
}

void CharacterGeneration::openQuick() {
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::Quick);
}

void CharacterGeneration::openCustom() {
    setAttributesVisible(_custom->step() > 1);
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::Custom);
}

void CharacterGeneration::openPortraitSelection() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::PortraitSelection);
}

void CharacterGeneration::openAbilities() {
    _abilities->reset(_type != Type::LevelUp);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Abilities);
}

void CharacterGeneration::openSkills() {
    _charGenSkills->reset(_type != Type::LevelUp);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Skills);
}

void CharacterGeneration::openFeats() {
    _charGenFeats->reset(_type == Type::LevelUp);
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Feats);
}

void CharacterGeneration::openPowers() {
    _charGenPowers->reset();
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Powers);
}

void CharacterGeneration::openNameEntry() {
    _controls.MODEL_LBL->setVisible(false);
    changeScreen(CharGenScreen::Name);
}

void CharacterGeneration::openLevelUp() {
    _levelUp->reset();
    // A chargen screen shown earlier may have hidden them; level-up always
    // shows the member's current attributes.
    setAttributesVisible(true);
    _controls.MODEL_LBL->setVisible(true);
    changeScreen(CharGenScreen::LevelUp);
}

void CharacterGeneration::cancel() {
    if (_type == Type::LevelUp) {
        _game.openInGame();
    } else {
        _game.openMainMenu();
    }
}

void CharacterGeneration::finish() {
    if (_type == Type::LevelUp) {
        if (auto *coop = _game.coop(); coop && coop->isClient()) {
            // Co-op (design item 5): the host checks the choices and applies
            // the level; the companion's sheet comes back with it.
            if (auto member = levelUpMember()) {
                coop->clientLevelUp(member->attributes(), _character.attributes,
                                    static_cast<int>(_character.attributes.getEffectiveClass()));
            }
            _game.openInGame();
            return;
        }
        ClassType classType = _character.attributes.getEffectiveClass();
        std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(classType));
        _character.attributes.addClassLevels(clazz.get(), 1);
        // Feats the class hands out at the new class level (feat.2da <prefix>_granted).
        clazz->grantFeatsUpToLevel(_character.attributes, _character.attributes.getClassLevel(classType));
        std::shared_ptr<Creature> partyLeader(levelUpMember());
        if (!partyLeader) {
            _game.openInGame();
            return;
        }
        // K2 Force forms due at the new Force-class level (K2 merge only).
        grantDueForms(_character.attributes, partyLeader->goodEvil());
        partyLeader->attributes() = _character.attributes;
        partyLeader->setMaxHitPoints(
            partyLeader->hitPoints() + clazz->hitdie());
        // Spellcaster classes add their force die per level (classes.2da forcedie).
        partyLeader->addBaseForcePoints(clazz->forcePointsPerLevel());
        _game.openInGame();
    } else {
        if (auto *coop = _game.coop(); coop && coop->clientNeedsGuest()) {
            // Co-op Free Mode: this is the guest's own character. It is saved
            // on this machine and brought to the host's game.
            coop->clientSetGuest(coopGuestFromCharacter(_game, _character));
            if (_game.module()) {
                _game.openInGame();
            } // else the host's module loads next and opens the game
            return;
        }
        // Character preview objects belong to the pre-playable character-
        // generation runtime. Retire them before publishing the new game.
        _game.retireRuntimeSession();

        std::vector<std::shared_ptr<Object>> noObsolete;
        std::shared_ptr<Creature> player;
        _game.replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                player = _game.newCreature();
                player->setTag(kObjectTagPlayer);
                player->setName(_character.name);
                player->setGender(_character.gender);
                player->setAppearance(_character.appearance);
                player->loadAppearance();
                // The player character belongs to repute.2da's "Player" faction (row 0),
                // as retail K1 saves record. Friendly_1 is hostile to Friendly_2
                // (e.g. the Republic soldiers on the Endar Spire).
                player->setFaction(Faction::Player);
                // Retail player characters can die; --immortal-player keeps
                // the developer shortcut.
                player->setImmortal(_game.options().game.immortalPlayer);
                player->setAutoCombat(_game.options().game.autoCombat);
                player->attributes() = _character.attributes;
                // Retail saves mark the player character IsPC; its force
                // points follow the player progression (design note c03_stats).
                player->setIsPC(true);
                // A new character starts neutral (retail K1 saves: GoodEvil 50).
                player->setGoodEvil(50);
                player->initializeGeneratedVitality();

                player->setOnHeartbeat("k_hen_heartbt01");
                player->setOnSpawn("k_hen_spawn01");
                player->setOnNotice("k_hen_percept01");
                player->setOnEndRound("k_hen_combend01");
                player->setOnAttacked("k_hen_attacked01");
                player->setOnDamaged("k_hen_damage01");
                player->setOnBlocked("k_hen_blocked01");
                player->setOnDialogue("k_hen_dialogue01");
            },
            []() noexcept {});

        Party &party = _game.party();
        party.reset();
        party.initializeNewGameState();
        party.addMember(kNpcPlayer, player);
        party.setPlayer(player);
        // The canonical PC has to exist before any script hands control to a
        // stand-in, or there is nothing to hand control back to.
        party.setActualPlayer(player);

        std::string moduleName(!_game.isTSL() ? "end_m01aa" : "001ebo");
        _game.loadModule(moduleName);
    }
}

void CharacterGeneration::finishQuickForAutomation(int classSlot, std::string name) {
    const Character *slot = _classSelection ? _classSelection->classButtonCharacter(classSlot) : nullptr;
    if (!slot) {
        throw std::out_of_range("Class slot is out of range: " + std::to_string(classSlot));
    }
    setCharacter(*slot);
    // A new appearance rolls a random name, as on entering the name screen;
    // an explicit name replaces it.
    if (!name.empty()) {
        setCharacterName(std::move(name));
    }
    _type = Type::Quick;
    finish();
}

void CharacterGeneration::setCharacter(Character character) {
    bool appearanceChanged = character.appearance != _character.appearance;

    _character = std::move(character);
    _controls.LBL_NAME->setTextMessage(_character.name);

    if (appearanceChanged) {
        reloadCharacterModel();
        _portraitSelection->updatePortraits();
        _nameEntry->loadRandomName();
    }

    updateAttributes();
}

void CharacterGeneration::setCharacterName(std::string name) {
    _character.name = std::move(name);
    _controls.LBL_NAME->setTextMessage(_character.name);
}

void CharacterGeneration::reloadCharacterModel() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneCharGen);
    const Control::Extent &extent = _controls.MODEL_LBL->extent();
    float aspect = extent.width / static_cast<float>(extent.height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&CharacterGeneration::getCharacterModel, this, std::placeholders::_1))
        .modelScale(kModelScale)
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.MODEL_LBL->setSceneName(kSceneCharGen);
    _controls.PORTRAIT_LBL->setBorderFill(_services.game.portraits.getTextureByAppearance(_character.appearance));
}

std::shared_ptr<ModelSceneNode> CharacterGeneration::getCharacterModel(ISceneGraph &sceneGraph) {
    std::shared_ptr<Creature> creature =
        _game.newPresentationCreature(sceneGraph.name());
    creature->setFacing(-glm::half_pi<float>());
    creature->setAppearance(_character.appearance);
    creature->equip("g_a_clothes01");
    creature->loadAppearance();
    creature->updateModelAnimation();

    auto model = sceneGraph.newModel(*_services.resource.models.get("cgbody_light"), ModelUsage::GUI);
    model->attach("cgbody_light", *creature->sceneNode());

    return model;
}

void CharacterGeneration::updateAttributes() {
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(_character.attributes.getEffectiveClass()));
    _controls.LBL_CLASS->setTextMessage(clazz->name());

    int vitality = _character.attributes.getPermanentMaxHitPoints(
        _character.attributes.getAggregateHitDie());
    _controls.LBL_VIT->setTextMessage(std::to_string(vitality));

    int defense = _character.attributes.getDefense();
    _controls.LBL_DEF->setTextMessage(std::to_string(defense));

    _controls.STR_AB_LBL->setTextMessage(std::to_string(_character.attributes.strength()));
    _controls.DEX_AB_LBL->setTextMessage(std::to_string(_character.attributes.dexterity()));
    _controls.CON_AB_LBL->setTextMessage(std::to_string(_character.attributes.constitution()));
    _controls.INT_AB_LBL->setTextMessage(std::to_string(_character.attributes.intelligence()));
    _controls.WIS_AB_LBL->setTextMessage(std::to_string(_character.attributes.wisdom()));
    _controls.CHA_AB_LBL->setTextMessage(std::to_string(_character.attributes.charisma()));

    const SavingThrows &throws = clazz->getSavingThrows(1);

    if (isTSL()) {
        _controls.NEW_FORT_LBL->setTextMessage(std::to_string(throws.fortitude));
        _controls.NEW_REFL_LBL->setTextMessage(std::to_string(throws.reflex));
        _controls.NEW_WILL_LBL->setTextMessage(std::to_string(throws.will));
    } else {
        _controls.OLD_FORT_LBL->setTextMessage(std::to_string(throws.fortitude));
        _controls.OLD_REFL_LBL->setTextMessage(std::to_string(throws.reflex));
        _controls.OLD_WILL_LBL->setTextMessage(std::to_string(throws.will));
    }
}

void CharacterGeneration::goToNextStep() {
    switch (_type) {
    case Type::LevelUp:
        _levelUp->goToNextStep();
        break;
    case Type::Custom:
        _custom->goToNextStep();
        break;
    default:
        _quick->goToNextStep();
        break;
    }
}

} // namespace game

} // namespace reone
