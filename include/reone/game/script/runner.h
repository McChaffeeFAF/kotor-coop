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

#include "reone/script/types.h"
#include "reone/script/variable.h"

namespace reone {

namespace script {

class IRoutines;
class ScriptProgram;

}

namespace resource {

class IScripts;

}

namespace game {

class Game;
class SavedScriptContinuation;

class ScriptRunner {
public:
    ScriptRunner(script::IRoutines &routines, resource::IScripts &scripts) :
        _routines(routines),
        _scripts(scripts) {
    }

    int run(
        const std::string &resRef,
        const std::vector<script::Argument> &args);

    // Run a script with a single ArgKind::Caller argument. When callerId is 0,
    // run a script without arguments.
    int run(const std::string &resRef, uint32_t callerId = 0);

    int run(
        const SavedScriptContinuation &continuation,
        const Game &game,
        const std::vector<script::Argument> &args);

    int run(
        const SavedScriptContinuation &continuation,
        const Game &game,
        uint32_t callerId);

    /**
     * A co-op client runs no game scripts: the host owns every story state.
     * While suppressed every run returns -1 without executing.
     */
    void setSuppressed(bool suppressed) { _suppressed = suppressed; }
    bool isSuppressed() const { return _suppressed; }

    /**
     * K2's routine table, for K2 programs (names with the "k2:" prefix) that
     * the K2 systems merge runs in a K1 session. Null: K2 programs run
     * nothing.
     */
    void setK2Routines(script::IRoutines *routines) { _k2Routines = routines; }

    /// The routine table a program runs against.
    script::IRoutines *routinesFor(const script::ScriptProgram &program);

private:
    script::IRoutines &_routines;
    resource::IScripts &_scripts;
    script::IRoutines *_k2Routines {nullptr};
    bool _suppressed {false};
};

} // namespace game

} // namespace reone
