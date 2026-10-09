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

/**
 * A small NWScript (.nss) to NCS compiler for tests. It is NOT part of the engine
 * and is never built into the game or its package: it exists so unit tests can run
 * real script logic without game files (docs/testing_scripts.md).
 *
 * The language subset: int, float, string, object, vector, location (and the
 * opaque effect, event and talent types); globals and locals with initialisers;
 * if/else, while, do/while, for, break, continue, return; the usual operators with
 * NWScript's int-to-float widening; user functions (prototypes, any order of
 * definition); engine routine calls by name through the routine table; and
 * action parameters (AssignCommand, DelayCommand, ActionDoCommand and the like),
 * compiled to STORE_STATE closures. Missing from the subset: switch, structs,
 * default argument values, #include and the preprocessor.
 */

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "reone/script/program.h"

namespace reone {

namespace script {

class IRoutines;

}

namespace testsupport {

class NssError : public std::runtime_error {
public:
    NssError(int line, const std::string &message) :
        std::runtime_error("line " + std::to_string(line) + ": " + message),
        _line(line) {
    }

    int line() const { return _line; }

private:
    int _line;
};

struct NssOptions {
    /// Extra integer constants a script may use (OBJECT_TYPE_CREATURE and so on).
    std::map<std::string, int> intConstants;
    /// Extra float constants.
    std::map<std::string, float> floatConstants;
    /// Extra string constants.
    std::map<std::string, std::string> stringConstants;
};

/**
 * Compile source to a program. TRUE, FALSE, OBJECT_SELF and OBJECT_INVALID are
 * always defined. The entry point is `void main()` or `int StartingConditional()`.
 * Throws NssError on a syntax or type error.
 */
std::shared_ptr<script::ScriptProgram> compileNss(
    const std::string &name,
    const std::string &source,
    script::IRoutines &routines,
    const NssOptions &options = {});

/// The program as NCS file bytes (what the game would load).
std::vector<char> writeNcsBytes(script::ScriptProgram &program);

/// Reads NCS file bytes back through the engine's reader.
std::shared_ptr<script::ScriptProgram> readNcsBytes(const std::string &name, const std::vector<char> &bytes);

/// Compile, write and read back: the program a game load would see.
std::shared_ptr<script::ScriptProgram> compileNssRoundTrip(
    const std::string &name,
    const std::string &source,
    script::IRoutines &routines,
    const NssOptions &options = {});

/// Whether two programs hold the same instructions with the same operands.
bool sameProgram(const script::ScriptProgram &a, const script::ScriptProgram &b);

} // namespace testsupport

} // namespace reone
