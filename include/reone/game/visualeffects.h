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
}

namespace audio {
class AudioClip;
}

namespace resource {
class TwoDAs;
class ITwoDAs;
class TwoDA;
class AudioClips;
class Models;
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

    VisualEffects(resource::ITwoDAs &twoDas,
                  resource::AudioClips &audioClips,
                  resource::Models &models) :
        _twoDas(twoDas),
        _audioClips(audioClips),
        _models(models) {}

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
        const SoundLoader &loadSound);

private:
    resource::ITwoDAs &_twoDas;
    resource::AudioClips &_audioClips;
    resource::Models &_models;
    std::map<uint32_t, VisualEffectDesc> _effects;
};

} // namespace game

} // namespace reone
