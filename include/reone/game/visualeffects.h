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

#pragma once

#include "reone/game/types.h"

namespace reone {

namespace graphics {
class Model;
class Texture;
} // namespace graphics

namespace audio {
class AudioClip;
}

namespace resource {
class TwoDAs;
class ITwoDAs;
class TwoDA;
class AudioClips;
class Models;
class Textures;
} // namespace resource

namespace game {

/**
 * One row of visualeffects.2da, found by its row label (the id scripts pass).
 * Presentation data only: nothing here decides whether an effect exists.
 */
struct VisualEffectDesc {
    enum class Kind {
        FireAndForget, /**< type_fd F: models play their impact animation once */
        Duration       /**< type_fd D: models loop while the effect lasts */
    };

    uint32_t id {0};
    std::string label;
    Kind kind {Kind::FireAndForget};
    bool orientWithGround {false}; /**< root model tilts to the ground normal */
    bool orientationOff {false};   /**< models keep world orientation */

    // Impact models, by attach point on the target

    std::shared_ptr<graphics::Model> impHeadConNode; /**< on the head-conjure point */
    std::shared_ptr<graphics::Model> impImpactNode;  /**< on the impact point */
    std::shared_ptr<graphics::Model> impRootSNode;   /**< at the root, small creature */
    std::shared_ptr<graphics::Model> impRootMNode;   /**< at the root, medium creature (all K1 rows) */
    std::shared_ptr<graphics::Model> impRootLNode;
    std::shared_ptr<graphics::Model> impRootHNode;

    // Cessation models (empty in K1), played when a duration effect ends

    std::shared_ptr<graphics::Model> cesHeadConNode;
    std::shared_ptr<graphics::Model> cesImpactNode;
    std::shared_ptr<graphics::Model> cesRootMNode;

    // Sounds

    std::shared_ptr<audio::AudioClip> soundImpact;
    std::shared_ptr<audio::AudioClip> soundDuration;  /**< loops while the effect is shown */
    std::shared_ptr<audio::AudioClip> soundCessation; /**< column "soundcessastion" */

    // Camera shake and controller rumble

    int shakeType {0}; /**< 0 none, 1 light, 2 heavy */
    float shakeDelay {0.0f};
    float shakeDuration {0.0f};
    int rumblePattern {0}; /**< read only: reone drives no rumble device */
    float rumbleCutoff {0.0f};

    // Programmed effects (progfx_*): numbers the engine draws by its own
    // means rather than through a model column.

    int progfxImpact {0};
    int progfxDuration {0};

    /**
     * The beam model of a programmed beam (progfx_duration 608-621). It sits
     * on the beam's source and its reattachable references are moved onto
     * the target, so its lightning emitters reach from one to the other.
     */
    std::shared_ptr<graphics::Model> beamModel;

    /** The model K1 ships for a programmed beam number, or empty. */
    static std::string beamModelResRef(int progfx);

    /**
     * A programmed body effect (progfx 14xx, the cloaks 1800/2000/2001): an
     * overlay drawn over the target's own meshes, and how see-through it
     * leaves them. See design note v14_programmed_effects.
     */
    struct BodyLook {
        std::string texture;  /**< fx_tex_NN, fx_tex_stealth, fx_distortion */
        float opacity {1.0f}; /**< a cloak leaves the body this opaque */
    };

    /** The body look of a programmed effect number, or none. */
    static std::optional<BodyLook> bodyLookFor(int progfx);

    std::optional<BodyLook> bodyLook;
    std::shared_ptr<graphics::Texture> bodyTexture;

    /**
     * The speed effect (progfx 1500 impact, 1601 duration): afterimages of
     * the target trail behind it as it moves, a pulse when it stands
     * (design note v15_vfx_leftovers). Drawn as a body overlay.
     */
    bool speedTrail {false};

    static bool isSpeedProgfx(int progfx) { return progfx == 1500 || progfx == 1601; }

    /** Where a programmed effect's own model goes on the target. */
    enum class ProgfxPlace {
        Impact,      /**< the impact node, like imp_impact_node */
        AboveHead,   /**< just over the head (the fizzle: at the impact node its flares are inside the body) */
        Chest,  /**< pinned to the front of the chest (the medal) */
        Head    /**< over the head, turning with it (the masks) */
    };

    /**
     * Programmed effects that K1 draws with a model no table names
     * (progfx 1201, 1700-1702): v_fizzle_imp, v_medal_dur,
     * v_revmask1/2_dur (design note v15_vfx_leftovers).
     */
    std::shared_ptr<graphics::Model> progfxModel;
    ProgfxPlace progfxPlace {ProgfxPlace::Impact};

    /** The model K1 ships for such a programmed effect number, or empty. */
    static std::string programmedModelResRef(int progfx, ProgfxPlace &place);

    /**
     * A spell's cast-hand visual (spells.2da casthandvisual), presented as a
     * visual effect of its own so that co-op guests get it through the same
     * events. Its id is castHandId(spell row); it is not a visualeffects row.
     */
    std::shared_ptr<graphics::Model> handModel;

    static constexpr uint32_t kCastHandBase = 0x40000000u;
    static uint32_t castHandId(int spellRow) { return kCastHandBase + static_cast<uint32_t>(spellRow); }

    /** The root model for the target's size (medium in every K1 row). */
    std::shared_ptr<graphics::Model> rootModel() const;
};

class IVisualEffects {
public:
    virtual std::optional<const VisualEffectDesc *> get(uint32_t id) const = 0;
};

class VisualEffects : public IVisualEffects {
public:
    using ModelLoader = std::function<std::shared_ptr<graphics::Model>(const std::string &)>;
    using SoundLoader = std::function<std::shared_ptr<audio::AudioClip>(const std::string &)>;
    using TextureLoader = std::function<std::shared_ptr<graphics::Texture>(const std::string &)>;

    VisualEffects(resource::ITwoDAs &twoDas,
                  resource::AudioClips &audioClips,
                  resource::Models &models,
                  resource::Textures *textures = nullptr) :
        _twoDas(twoDas),
        _audioClips(audioClips),
        _models(models),
        _textures(textures) {}

    void init();
    void clear();

    std::optional<const VisualEffectDesc *> get(uint32_t id) const override;

    /**
     * Read every row of a visualeffects table, keyed by its row label. Rows
     * whose label is not a number are skipped.
     */
    static std::map<uint32_t, VisualEffectDesc> parse(
        const resource::TwoDA &twoDa,
        const ModelLoader &loadModel,
        const SoundLoader &loadSound,
        const TextureLoader &loadTexture = nullptr);

    /**
     * The cast-hand visuals of spells.2da (casthandvisual, else
     * conjhandvisual), one per spell row that names a model that loads,
     * keyed by VisualEffectDesc::castHandId.
     */
    static std::map<uint32_t, VisualEffectDesc> parseCastHands(
        const resource::TwoDA &spells,
        const ModelLoader &loadModel);

private:
    resource::ITwoDAs &_twoDas;
    resource::AudioClips &_audioClips;
    resource::Models &_models;
    resource::Textures *_textures;
    std::map<uint32_t, VisualEffectDesc> _effects;
};

} // namespace game

} // namespace reone
