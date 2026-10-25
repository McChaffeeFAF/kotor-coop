# Testing with NWScript sources

Engine tests can run real script logic without game files. `test/support/nsscomp.h`
holds a small NWScript (`.nss`) to NCS compiler for tests: a test writes a script as
a string, compiles it against the engine's own routine table, and runs it on the
engine's script VM (or hands it to the game under a resource name).

The compiler lives in `test/support/` and is built into the unit-test binary only.
It is not part of the engine libraries, the game, or the player package, and nothing
in the shipped build depends on it.

## Usage

```cpp
#include "../support/nsscomp.h"

using namespace reone::testsupport;

// Against the engine's routines (K1 here), served to the game under "trig_enter":
auto program = compileNssRoundTrip("trig_enter", R"(
    void main() {
        object oPC = GetEnteringObject();
        SetLocalNumber(OBJECT_SELF, 1, GetLocalNumber(OBJECT_SELF, 1) + 1);
    }
)", routines);
EXPECT_CALL(engine.resourceModule().scripts(), get("trig_enter")).WillRepeatedly(Return(program));
game.scriptRunner().run("trig_enter", callerObjectId);
```

* `compileNss(name, source, routines, options)` returns the `ScriptProgram`.
* `compileNssRoundTrip(...)` also writes the NCS file bytes and reads them back with
  the engine's NCS reader, so the program is the one a real load would produce. Use
  it when a test is about the engine, and `compileNss` for compiler-level tests.
* `writeNcsBytes` / `readNcsBytes` / `sameProgram` expose the file round trip.
* `NssOptions` defines extra constants (`OBJECT_TYPE_CREATURE` and the like). `TRUE`,
  `FALSE`, `OBJECT_SELF` and `OBJECT_INVALID` are always defined; declare anything else
  in the script (`int OBJECT_TYPE_CREATURE = 1;`) or through the options.
* A syntax or type error throws `NssError`, whose message names the line.
* The entry point is `void main()` or `int StartingConditional()`. The result of a
  conditional is what `VirtualMachine::run()` returns.

Routines are found by name through `IRoutines::getIndexByName`, and their argument
types and optional arguments come from the same table the game uses, so a script
that calls a routine the engine lacks, or with too many arguments, fails to compile.

Examples: `test/script/nsscomp.cpp` (the language, against a small recording routine
table) and `test/game/nssscripts.cpp` (scripts against game fixtures: a trigger
OnEnter that starts a conversation, a door OnFailToOpen that counts attempts).

## The language subset

Supported: `int`, `float`, `string`, `object`, `vector`, `location` (and the opaque
`effect`, `event`, `talent`); globals and locals with initialisers; `if`/`else`,
`while`, `do`/`while`, `for`, `break`, `continue`, `return`; the C operators with
NWScript's int-to-float widening, `?:`, `++`/`--`, compound assignment; vector
literals `[x, y, z]`, `.x`/`.y`/`.z` and vector arithmetic; user functions with
prototypes and recursion; engine routine calls; and action parameters
(`AssignCommand`, `DelayCommand`, `ActionDoCommand`, ...), compiled to `STORE_STATE`
closures that capture the globals and the caller's stack.

Not supported: `switch`, structs, default argument values, `#include`/`#define`,
and the engine constants beyond the four above (supply them as shown).

Differences from the retail compiler worth knowing:

* `&&` and `||` evaluate both sides (as the KotOR scripts expect from `LOGANDII` and
  `LOGORII`); there is no short circuit.
* Code layout differs from retail output (no `#globals` naming, no `RESTOREBP`), but
  uses the same instructions and the same stack conventions, so the VM runs both.
* Vectors sit on the stack with `x` deepest and `z` on top. The VM pushes a vector that
  a routine returns as `z, y, x` instead (pinned by `VirtualMachine.should_run_script_program__action_with_vectors`),
  so after such a call the compiler turns the three slots around: `.x` of a returned
  vector is its x, whichever way the VM is later made to agree with itself.
* The compiler is for tests: it does not optimise and does not try to match
  retail byte for byte.

## Rules

* No game data. Scripts are inline strings; synthetic content only.
* Do not add the compiler to a player package, an installer, or a library target.
