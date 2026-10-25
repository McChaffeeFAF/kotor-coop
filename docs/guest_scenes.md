# Guests in scenes, and the factions guests see

Two rules for co-op guests (`src/libs/game/coopscene.cpp`).

## Scripted jumps take Free Mode guests along

In Story Mode a guest plays a member of the host's party, so a script that jumps
the party (`JumpToLocation`, `JumpToObject`, `ActionJumpToLocation`,
`ActionJumpToObject` on the PC or on `GetPartyMemberByIndex(n)`) moves the
guest's creature with everyone else. In Free Mode a guest's own character is a
puppet outside the party table, so no script names it. It could be left behind
a door the scene then locks.

**The rule.** When a script jumps **the host's party leader** more than 2 m
during a **shared scene**, every Free Mode guest character in the area goes too.
A shared scene is any of these:

- a conversation (the conversation screen, or the leader in a conversation),
- a cutscene (the leader is not commandable),
- a movie,
- a global fade: opaque, still moving, or held for a dialog,
- the 3 seconds after any of these. A scene usually fades out, jumps the party
  a moment later, and only then starts its conversation. `k_pend_door18` on the
  Endar Spire does this when it moves the party into `end_room3`.

Each guest lands on a free spot near the leader's destination. The spots are
the stand-in formation offsets, each made free of other creatures with
`Area::findFreeCreaturePosition`. A spot the destination's walkmesh does not
reach, such as one on the far side of a wall, is skipped for a free spot around
the destination itself. A guest that moves also has its actions cleared and
faces the way the leader faces. The move happens on the same host tick as the
leader's jump, so it is hidden by the same fade or cut.

Not counted:

- jumps of other creatures. Scripts move "the party" member by member, and the
  leader's jump is the one that moves guests.
- moves of 2 m or less. These place actors for a shot and are not travel.
- jumps in free play outside a scene.

On a dedicated host the leader is the host's own player character. The story
driver's character stays in the world while it answers a conversation, and this
rule is what moves it with the scene.

**Pull-along.** When a shared scene ends, the host checks each guest that is
still standing in the world and is more than 2 m from the leader. It plans a
path from the guest to the leader, and the plan honours closed locked doors
(the path barriers). If no path exists, the guest is moved beside the leader,
as above. This covers a guest that the scene shut in or shut out.

**Unchanged.** Guests still leave the world for conversations, cutscenes and
area transitions, and they come back beside the leader. A module transition
therefore always brings every guest to the new area's entry, wherever it stood.

The host's `netstatus` counts these moves as `standIns.sceneJumps` and
`standIns.pulledAlong`, and its log names each one.

## Factions reach every guest

A guest's client runs no scripts. Its copies of the host's creatures used to
keep the factions in their templates. When the host turned a creature hostile
(`ChangeToStandardFaction`, `ChangeFaction`, a faction-modifier effect, such as
the Endar Spire Sith or Davik's guests on Taris), the guest still saw it as
neutral. The creature was drawn green, a click on it walked over to talk instead
of attacking, and the target frame and combat music did not change. Changes to
the reputation table (`AdjustReputation`, or a save's `repute.fac`) never
reached the guest either.

**The rule.** The guest's view of hostility uses the host's data, judged from
the guest's own character:

- Every creature in a snapshot carries `faction` (`kFieldFaction`), its
  `reputationFaction()` on the host. Party members and Free Mode guests count
  as the player faction. A change goes in the next delta on its own. The client
  sets it on its copy.
- The host sends its whole reputation table (`ReputationTable`) to each client
  when the client joins, and again whenever the table changes. The host checks
  for a change every 10 ticks. The client replaces its own table with the
  host's. It applies the table again if a module load resets the local table
  to the install's `repute.2da`.
- On the client, the guest's own character (or its companion in Story Mode) is
  the local party leader and the player faction. Target colour (red or green),
  click-to-attack versus click-to-talk (`interceptClick`), the HUD target frame
  and combat music all ask the local reputation rules about that character, so
  they now give the answer the host gives for it.

## Protocol 9

These were protocol 8 on their branch; Stealth Mode also bumped to 8 on its own
branch, so the merged build (alpha-0.4.6) is protocol 9 and refuses either 8 build.

| addition | |
|---|---|
| `kFieldFaction` (entity field bit 11), `EntityState::faction` | one byte: the faction id, less than `kMaxFactions` (64), or `kNoFaction` (255) for objects with none. Any other value fails to decode. |
| `ReputationTable` (message 120, host to client) | a count N (at most 64), then N x N bytes, `values[source][target]` in 0..100. A larger count or a value above 100 fails to decode. |

Ids from 120 up are kept for faction messages. Stealth Mode, merged in the same
bump, uses none of these: entity flag bit 6 (`kFlagStealthed`), command 33
(`Stealth`) and rejects 40/41 (`StealthUnavailable`, `StealthInCombat`).

## Protocol 11

Guest control, co-op scenes and co-op presence (playtest 2026-10-22,
`design note coop_playtest_1022`) were each protocol 10 on their own branch;
the merged build (alpha-0.4.7) is protocol 11 and refuses any 10 build.

| addition | |
|---|---|
| `Steer` (message 130, client to host) | `seq`, `actor`, `moving`, `facing` (radians, at most 64 in size, finite), `run`. The host moves the guest's creature directly each tick until a stop or `kSteerTimeoutSeconds` (0.4 s) without an update; at most `kMaxSteersPerSecond` (40). A steer naming an earlier stand-in of the same player in this module visit is for the current one. Ids from 130 up are kept for guest control messages. |
| `kFlagAutoCombat` (entity flag bit 7) | auto-combat is on for that creature; the HUD toggle shows it lit. Every flag bit is now in use, so no flag byte is out of range. |
| `ItemEquippedByPlayer` (command reject 42) | Story Mode bench: another player has that item equipped. Anything past 42 fails to decode. |
| `kFieldLook` (entity field bit 12), `EntityState::look` | a player's name (string), chat colour (u8), head row, body variation and texture variation (i16 each, -1 for the default; head at least -1, body at most 25, texture at most 255), then a count (at most `kMaxLookSlots`, 16) of extra equipment slots, each a slot byte and a template resref. `kFieldAllMask` is `(1 << 13) - 1`. |

`test/net/roundtrip.cpp` `protocol_11_carries_control_scenes_and_presence_without_colliding`
pins the combined table: every field and flag bit used once, message ids 121-140
free but 130, rejects 40-42 named.

## Protocol 14

Guest NPC scripts (`design note guest_npc_scripts`) and the shared area map
were each protocol 13 on their own branch; the merged build (alpha-0.4.9) is
protocol 14 and refuses any 13 build.

| addition | |
|---|---|
| `kFieldProgress` (entity field bit 13), `EntityState::progress` | creatures: the host's level-up state, one byte, only `kProgressLevelUp` (bit 0) defined; any other bit fails to decode. A client's copy answers `isLevelUpPending` from it. `kFieldAllMask` is `(1 << 14) - 1`. |
| `MapState` (message 170, host to client) | the area's resref, `columns` and `rows` (at most 256 each), the explored grid one bit per cell (row-major, least significant bit first, `(columns * rows + 7) / 8` bytes), then a count (at most 128) of enabled map-note waypoint tags. The host sends it per client when it changes (checked every 10 ticks). Ids from 170 up are kept for map messages. |

No command, flag or reject was added. `test/net/roundtrip.cpp`
`protocol_14_carries_level_up_state_and_the_shared_map_without_colliding` pins
the combined table: field bit 13, mask `(1 << 14) - 1`, message ids 131-200 free
but 170, nothing past reject 42.

## Join requests

With `--coop-ask-join 1` (or a host with a display), a new player waits for the
host's Accept / Decline: in Story Mode once it asks for a slot, in Free Mode once
its character reaches the host. `CoopJoinQueue` (`coopjoinqueue.h`) holds the
waiting requests, oldest first, and the one prompt that is up for them:

- one prompt at a time, in request order, only in the game and only when no
  other pop-up is up; requests made on the same tick keep the order they came in;
- a prompt something else takes down unanswered (a screen change such as a
  conversation or a load screen, another pop-up in its place) is counted
  (`joinPromptsLost`) and that request is asked again first;
- an answer from anywhere (the prompt's buttons, the Co-op screen, `coopapprove`)
  takes only that client's request off, and takes the pop-up down only if it is
  still that client's prompt; a player who leaves takes their request along;
- a request unanswered for 60 s (`kJoinReminderSeconds`) tells its player it is
  still waiting, again every 60 s.

`netstatus` shows `joinQueue` (client ids, oldest first), `joinPrompt` (the
client the prompt is about, 0 for none), `joinPromptsLost` and, per client,
`approval` and `guestReceived`. The host log has one line per event
("Co-op: join request N in line", "join prompt up for client N", "the host
accepted X (client N)", "was taken down unanswered"). `coopapprove` answers only
a request that is pending: a harness waits for `approval` to be `pending` first.
`guiclick BTN_OK` / `BTN_CANCEL` press the pop-up's own buttons while it is up.

## Tests

- `test/net/protocol.cpp`, `test/net/roundtrip.cpp`: the faction field in full
  snapshots and deltas, its limits, and `ReputationTable` at its limits, with
  every truncation and a trailing byte refused. The message type is host-only.
- `test/game/coopnet.cpp`:
  - `each_creature_s_faction_travels_and_a_change_travels_alone`
  - `the_reputation_table_goes_to_each_client_when_it_changes`
  - `a_creature_the_host_turns_hostile_turns_hostile_on_the_guest`
  - `the_host_s_reputation_table_replaces_the_guest_s_own`
  - `a_table_the_guest_already_has_is_not_applied_again`
  - `the_leader_s_scripted_jump_in_a_scene_takes_the_guest_along`
  - `a_jump_soon_after_a_scene_s_fade_still_belongs_to_it`
  - `outside_a_scene_and_for_other_creatures_or_short_moves_guests_stay`
- an integration probe (not included): a two-engine Free Mode run. The host plays
  the Endar Spire to `end_door02`, and its scene fades and jumps the party into
  `end_room3`. The run checks that the guest's character ends up in the room
  beside the leader. On the guest's client, every Sith there must be hostile to
  the guest's character (red, attack on click), with the host's faction, and
  hostility must agree with the host's for every creature.
  On a normal host the guest has already left the world by the time
  `k_pend_door18` jumps the leader, because the leader is not commandable, and
  it comes back beside the leader. The probe therefore also queues a scene's
  jump with the guest in the world. The harness command `coopscenejump x y
  [fade]` on the host does what the script does: a fade-out, then a
  `JumpToLocation` action on top of the leader's queue. The probe checks that
  the guest lands beside the leader, on the host and on the guest's screen, and
  that the same jump without a fade in free play leaves the guest where it is.
