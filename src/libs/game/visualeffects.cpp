/*
 * Copyright (c) 2026 The reone project contributors
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

#include "reone/game/visualeffects.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/graphics/texture.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/system/logutil.h"

using namespace reone::resource;

namespace reone {

namespace game {

std::shared_ptr<graphics::Model> VisualEffectDesc::rootModel() const {
    if (impRootMNode) {
        return impRootMNode;
    }
    if (impRootSNode) {
        return impRootSNode;
    }
    if (impRootLNode) {
        return impRootLNode;
    }
    return impRootHNode;
}

// Programmed beams: visualeffects.2da names no model for its VFX_BEAM_* rows,
// only a progfx_duration number (608-621). K1 ships exactly fourteen models
// that no table names, each a lightning (or flame/ion) emitter with a
// reattachable reference at its far end, and their names match the fourteen
// beam labels one to one. Inferred from the game data (design note
// v13_beams); not read from any executable.
std::string VisualEffectDesc::beamModelResRef(int progfx) {
    switch (progfx) {
    case 608: return "v_lightns_dur";  // VFX_BEAM_LIGHTNING_DARK_S
    case 609: return "v_lightnx_dur";  // VFX_BEAM_LIGHTNING_DARK_L
    case 610: return "v_drddisab_dur"; // VFX_BEAM_DROID_DISABLE
    case 611: return "v_drdkill_dur";  // VFX_BEAM_DROID_DESTROY
    case 612: return "v_deathfld_dur"; // VFX_BEAM_DEATH_FIELD_TENTACLE
    case 613: return "v_drain_dur";    // VFX_BEAM_DRAIN_LIFE
    case 614: return "v_flame_dur";    // VFX_BEAM_FLAME_SPRAY
    case 615: return "v_stunray_dur";  // VFX_BEAM_STUN_RAY
    case 616: return "v_coldray_dur";  // VFX_BEAM_COLD_RAY
    case 617: return "v_ionray01_dur"; // VFX_BEAM_ION_RAY_01
    case 618: return "v_ionray02_dur"; // VFX_BEAM_ION_RAY_02
    case 619: return "v_fstorm_dur";   // VFX_BEAM_FORCE_STORM_ARC
    case 620: return "v_drdstun_dur";  // VFX_BEAM_DROID_STUN
    case 621: return "v_fshock_dur";   // VFX_BEAM_SHOCK
    default: return "";
    }
}

// Programmed body effects: K1 ships 26 textures fx_tex_01..fx_tex_26 that
// no model or table names, and their colours match the 14xx progfx rows one
// to one once fx_tex_13 (authored apart: 64x64, mip-mapped) is left out:
// 1401-1412 take 01-12, 1413-1425 take 14-26, 1426 (the stealth field) takes
// fx_tex_stealth. Inferred from the game data (design note
// v14_programmed_effects, section 2); not read from any executable.
std::optional<VisualEffectDesc::BodyLook> VisualEffectDesc::bodyLookFor(int progfx) {
    BodyLook look;
    if (progfx >= 1401 && progfx <= 1425) {
        int index = progfx <= 1412 ? progfx - 1400 : progfx - 1399;
        look.texture = str(boost::format("fx_tex_%02d") % index);
        return look;
    }
    switch (progfx) {
    case 1426: // VFX_DUR_STEALTH_FIELD
    case 2000: // VFX_DUR_STEALTH_PULSE
    case 2001: // VFX_DUR_INVISIBILITY
        look.texture = "fx_tex_stealth";
        look.opacity = 0.35f;
        return look;
    case 1800: // VFX_DUR_DISTORTION (and VFX_DUR_CLOAK_REMOVAL's duration)
        look.texture = "fx_distortion";
        look.opacity = 0.5f;
        return look;
    default:
        return std::nullopt;
    }
}

// Programmed effects drawn with a model: K1 ships effect models that no
// table names, and four of their names match progfx rows: the fizzle
// (VFX_COM_JEDI_FORCE_FIZZLE, 1201), the medal (VFX_DUR_MEDAL, 1700) and
// Revan's two masks (VFX_DUR_MASK_01/02, 1701/1702). 1202 is left undrawn:
// VFX_COM_SNIPER_SHOT shares it, so the unnamed v_fresist_imp ring would
// show on every sniper shot. Inferred from the game data
// (design note v15_vfx_leftovers); not read from any executable.
std::string VisualEffectDesc::programmedModelResRef(int progfx, ProgfxPlace &place) {
    switch (progfx) {
    case 1201:
        place = ProgfxPlace::AboveHead;
        return "v_fizzle_imp";
    case 1700:
        place = ProgfxPlace::Chest;
        return "v_medal_dur";
    case 1701:
        place = ProgfxPlace::Head;
        return "v_revmask1_dur";
    case 1702:
        place = ProgfxPlace::Head;
        return "v_revmask2_dur";
    default:
        return "";
    }
}

static std::optional<uint32_t> parseRowLabel(const std::string &label) {
    if (label.empty() || label.size() > 9) {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (char ch : label) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10 + static_cast<uint32_t>(ch - '0');
    }
    return value;
}

static std::string cell(const TwoDA &twoDa, int row, const std::string &column) {
    std::string value = twoDa.getString(row, column);
    return value == "****" ? std::string() : value;
}

static int cellInt(const TwoDA &twoDa, int row, const std::string &column) {
    std::string value = cell(twoDa, row, column);
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return 0;
    }
    return std::stoi(value);
}

std::map<uint32_t, VisualEffectDesc> VisualEffects::parse(
    const TwoDA &twoDa,
    const ModelLoader &loadModel,
    const SoundLoader &loadSound,
    const TextureLoader &loadTexture) {

    auto model = [&](int row, const char *column) -> std::shared_ptr<graphics::Model> {
        std::string resRef = cell(twoDa, row, column);
        return resRef.empty() ? nullptr : loadModel(resRef);
    };
    auto sound = [&](int row, const char *column) -> std::shared_ptr<audio::AudioClip> {
        std::string resRef = cell(twoDa, row, column);
        return resRef.empty() ? nullptr : loadSound(resRef);
    };

    std::map<uint32_t, VisualEffectDesc> effects;
    for (int row = 0; row < twoDa.getRowCount(); ++row) {
        // Scripts pass the row label (1001, 3003, ...), not the row index:
        // K1's labels are not contiguous.
        auto id = parseRowLabel(twoDa.rows()[row].label);
        if (!id) {
            continue;
        }
        VisualEffectDesc desc;
        desc.id = *id;
        desc.label = twoDa.getString(row, "label");
        std::string typeFd = cell(twoDa, row, "type_fd");
        desc.kind = (!typeFd.empty() && (typeFd[0] == 'D' || typeFd[0] == 'd'))
                        ? VisualEffectDesc::Kind::Duration
                        : VisualEffectDesc::Kind::FireAndForget;
        desc.orientWithGround = twoDa.getInt(row, "orientwithground", 0) != 0;
        desc.orientationOff = twoDa.getInt(row, "orientationoff", 0) != 0;

        desc.impHeadConNode = model(row, "imp_headcon_node");
        desc.impImpactNode = model(row, "imp_impact_node");
        desc.impRootSNode = model(row, "imp_root_s_node");
        desc.impRootMNode = model(row, "imp_root_m_node");
        desc.impRootLNode = model(row, "imp_root_l_node");
        desc.impRootHNode = model(row, "imp_root_h_node");
        desc.cesHeadConNode = model(row, "ces_headcon_node");
        desc.cesImpactNode = model(row, "ces_impact_node");
        desc.cesRootMNode = model(row, "ces_root_m_node");

        // progfx_* (programmed effects): the beams (6xx) are drawn by their
        // model (design note v13_beams), shields, auras, the body crackle and the
        // cloaks (14xx, 1800, 2000s) as an overlay on the target (v14).
        desc.progfxImpact = cellInt(twoDa, row, "progfx_impact");
        desc.progfxDuration = cellInt(twoDa, row, "progfx_duration");
        std::string beamResRef = VisualEffectDesc::beamModelResRef(desc.progfxDuration);
        if (!beamResRef.empty()) {
            desc.beamModel = loadModel(beamResRef);
        }
        bool duration = desc.kind == VisualEffectDesc::Kind::Duration;
        auto look = VisualEffectDesc::bodyLookFor(duration ? desc.progfxDuration : desc.progfxImpact);
        if (!look) {
            look = VisualEffectDesc::bodyLookFor(duration ? desc.progfxImpact : desc.progfxDuration);
        }
        if (look) {
            if (loadTexture) {
                desc.bodyTexture = loadTexture(look->texture);
            }
            desc.bodyLook = std::move(*look);
        }

        desc.speedTrail = VisualEffectDesc::isSpeedProgfx(desc.progfxImpact) || VisualEffectDesc::isSpeedProgfx(desc.progfxDuration);
        for (int progfx : {duration ? desc.progfxDuration : desc.progfxImpact, duration ? desc.progfxImpact : desc.progfxDuration}) {
            auto place = VisualEffectDesc::ProgfxPlace::Impact;
            std::string resRef = VisualEffectDesc::programmedModelResRef(progfx, place);
            if (!resRef.empty()) {
                desc.progfxModel = loadModel(resRef);
                desc.progfxPlace = place;
                break;
            }
        }

        desc.soundImpact = sound(row, "soundimpact");
        desc.soundDuration = sound(row, "soundduration");
        desc.soundCessation = sound(row, "soundcessastion"); // spelt so in the data

        desc.shakeType = twoDa.getInt(row, "shaketype", 0);
        desc.shakeDelay = twoDa.getFloat(row, "shakedelay", 0.0f);
        desc.shakeDuration = twoDa.getFloat(row, "shakeduration", 0.0f);
        desc.rumblePattern = twoDa.getInt(row, "rumblepattern", 0);
        desc.rumbleCutoff = twoDa.getFloat(row, "rumblecutoff", 0.0f);

        effects[*id] = std::move(desc);
    }
    return effects;
}

std::map<uint32_t, VisualEffectDesc> VisualEffects::parseCastHands(
    const TwoDA &spells,
    const ModelLoader &loadModel) {
    std::map<uint32_t, VisualEffectDesc> effects;
    for (int row = 0; row < spells.getRowCount(); ++row) {
        std::string resRef = cell(spells, row, "casthandvisual");
        if (resRef.empty()) {
            resRef = cell(spells, row, "conjhandvisual");
        }
        if (resRef.empty()) {
            continue;
        }
        auto model = loadModel(resRef);
        if (!model) {
            continue; // v_con_light: named by seven K1 powers, shipped by neither game
        }
        VisualEffectDesc desc;
        desc.id = VisualEffectDesc::castHandId(row);
        desc.label = "CASTHAND_" + spells.getString(row, "label");
        desc.kind = VisualEffectDesc::Kind::Duration; // the cast ends it
        desc.handModel = std::move(model);
        effects[desc.id] = std::move(desc);
    }
    return effects;
}

void VisualEffects::init() {
    std::shared_ptr<TwoDA> effectsDa(_twoDas.get("visualeffects"));
    if (!effectsDa) {
        return;
    }
    _effects = parse(
        *effectsDa,
        // Some rows name models neither game ships (v_speed_dur, the 14xx
        // shield impacts): retail draws them through their progfx programmed
        // effect. Those are not reported as missing (design note p2_combat, item 10).
        [this](const std::string &resRef) { return _models.getOptional(boost::to_lower_copy(resRef)); },
        [this](const std::string &resRef) { return _audioClips.get(boost::to_lower_copy(resRef)); },
        [this](const std::string &resRef) -> std::shared_ptr<graphics::Texture> {
            return _textures ? _textures->get(resRef, graphics::TextureUsage::MainTex) : nullptr;
        });
    if (std::shared_ptr<TwoDA> spells = _twoDas.get("spells")) {
        auto hands = parseCastHands(*spells, [this](const std::string &resRef) {
            return _models.getOptional(boost::to_lower_copy(resRef));
        });
        _effects.insert(hands.begin(), hands.end());
    }
}

void VisualEffects::clear() {
    _effects.clear();
}

std::optional<const VisualEffectDesc *> VisualEffects::get(uint32_t id) const {
    auto it = _effects.find(id);
    if (it == _effects.end()) {
        return std::nullopt;
    }
    return &it->second;
}

} // namespace game

} // namespace reone
