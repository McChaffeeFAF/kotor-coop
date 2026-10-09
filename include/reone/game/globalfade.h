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

#include <memory>
#include <glm/vec3.hpp>

namespace reone::game {

/**
 * Screen fade state. Scripts wait on fades, so the state advances in the
 * fixed simulation tick (update is called once per tick). Rendering reads
 * renderOpacity, which only interpolates between the last two ticks.
 */
class GlobalFade {
public:
    enum class Direction { In, Out };
    enum class Source { Automatic, Script };

    // Identity only: keeping a ticket alive grants no authority after replacement.
    struct DialogIdentity {};
    struct ArrivalIdentity {};
    using DialogTicket = std::shared_ptr<const DialogIdentity>;
    using ArrivalTicket = std::shared_ptr<const ArrivalIdentity>;

    bool request(Direction direction, float wait = 0.0f, float length = 0.0f,
                 glm::vec3 color = glm::vec3(0.0f), Source source = Source::Automatic);
    bool stop();
    void update(float dt);

    void holdForDialog() { _hold = true; }
    /** Drop a fade-in held for a dialog, fading in over 1 s (ResetDialogState). */
    void releaseDialogHold() { consumeHold(); }
    void lockUntilScript() { _locked = true; }
    void setMovieOverride(bool active) { _movieOverride = active; }

    ArrivalTicket beginArrival();
    void finishLoading(const ArrivalTicket &arrival);
    void settleArrival(const ArrivalTicket &arrival);
    void invalidateModule();

    /**
     * Admit a dialog request. A pending request refuses later ones, except
     * that a pending player order (click-to-talk still walking) yields to a
     * scripted request: retail starts whichever conversation executes first
     * and clears the participants' actions, so a cutscene a trigger, door or
     * placeable starts is not lost to the player's earlier click.
     */
    DialogTicket admitDialog(bool replace = false, bool playerOrder = false);
    bool isCurrentDialog(const DialogTicket &dialog) const;
    void revealDialog(const DialogTicket &dialog);
    void finishDialog(const DialogTicket &dialog);

    /**
     * Co-op client: show the host's fade. The host's fade logic decides; the
     * client only renders its opacity and colour.
     */
    void applyReplicated(float opacity, glm::vec3 color) {
        _previousOpacity = opacity;
        _opacity = opacity;
        _color = color;
        _active = false;
    }

    /** New game/disk-load retirement, deliberately stronger than locked stop. */
    void resetSession();

    /** Opacity at the last simulation tick (simulation state). */
    float opacity() const { return _opacity; }
    /** Presentation only: opacity between the previous tick and the last one. */
    float renderOpacity(float alpha) const;
    const glm::vec3 &color() const { return _color; }
    bool heldForDialog() const { return _hold; }
    bool locked() const { return _locked; }
    bool dialogPending() const { return _hasDialog && !_dialog.expired(); }
    bool arrivalPending() const { return static_cast<bool>(_arrival); }

private:
    Direction _direction {Direction::In};
    glm::vec3 _color {0.0f};
    double _elapsed {0.0};
    float _wait {0.0f};
    float _length {0.0f};
    float _opacity {0.0f};
    float _previousOpacity {0.0f};
    bool _active {false};
    bool _hold {false};
    bool _locked {false};
    bool _movieOverride {false};
    bool _hasDialog {false};
    bool _dialogPlayerOrder {false};
    std::weak_ptr<const DialogIdentity> _dialog;
    ArrivalTicket _arrival;
    bool _arrivalConsumed {false};

    void evaluate();
    void consumeHold();
};

} // namespace reone::game
