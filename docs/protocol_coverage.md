# Co-op protocol: coverage and hardening, round 2

The first round fuzzed the wire format as it was. This round lists what the protocol has now, what each part
is tested and fuzzed by, and what the host does with a player who names what is not theirs.

## What is covered

| | count | round-trip tests | fuzz harness (`fuzz/fuzz_net.cpp`) | seeds (`fuzz/netseed.cpp`) |
|---|---|---|---|---|
| message types | 23 (Hello ... Offer, BenchView) | all 23, before and after (`test/net/roundtrip.cpp`: limits, every prefix and a trailing byte refused) | **22 before**, 23 after: BenchView was missing | one each before; after: every enum value of every message, every entity and world field alone, deltas against a base, both installs and a token in Hello, closed views |
| command types | 31 | every one with extreme values (the loop stopped at a hard-coded last type; it now scans all 256 values by name, so a new type is covered automatically) | via `Command` | **1 before** (MoveTo), all 31 after |
| refusals (CommandReject) | 38 | every one (scanned by name, as above) | via `CommandResult` | 1 before, all 38 after |
| other enums (reject reasons, slot status, notice, offer, event kind, ready state and answer, message type byte) | | **new** `NetCoverage` tests: each decodes exactly when the protocol names it, across all 256 byte values | via their messages | all values |
| entity and world deltas (`diffEntity`, `mergeEntity`, `mergeWorld` ...) | | unit tests as before | **not fuzzed before**; now merging all fields of a decoded snapshot must reproduce it exactly (a trap otherwise) | n/a |
| name tables, `isClientMessage`/`isHostMessage`, facing quantisers | | | **not fuzzed before** (0% executed); now fed every byte / any float | n/a |
| join-code link (`SignalLink`) | | **new** `SignalLinkHostile` tests against a local fake service | n/a (sockets) | n/a |

Line coverage of `src/libs/net/protocol.cpp` by the fuzz corpus: **64.8 % with the old seeds, 73.1 % after the
first 15-minute run, 98.4 % now** (functions 100 %). The few lines left are error paths inside
`exactDiffEntity`/`exactDiffWorld` that only differing states reach.

## Fuzzing

`tools/cloud/fuzz.sh net`, ASan + UBSan (clang 18, libFuzzer):

| run | executions | crashes, hangs, oversized allocations |
|---|---|---|
| 15 minutes, old harness | 52,220,374 | none |
| 2 workers x 40 minutes, extended harness and seeds | 94,364,666 | none |

About 147 million executions (roughly 1.6 CPU-hours) found nothing in the decoders: they check every length,
enum and float before using it, and every message is canonical (decode, encode, decode agrees). The gap was
coverage, not bugs: one message and the delta code were never reached. The `SignalLink` test found nothing
either: it closes on any line that is not one JSON object, caps a line at 16 KiB, rejects nesting past the
JSON parser's depth limit, and drops (rather than grows) past 1024 queued messages.

## Host authority

Tests in `test/game/coopnet.cpp` (`CoopHostileTest`, `CoopFreeHostileTest`):

- `commands_naming_what_a_player_does_not_own_or_an_index_out_of_range_change_nothing`: 40 commands from a
  companion's player naming a hostile's items, a container never opened, doors, non-existent ids, slots, skills,
  feats, powers, replies and Pazaak results out of range, and level-up text of every shape. Each is refused and
  the world (positions, actions, what every object holds and wears, credits, XP) is byte-for-byte as before.
- `a_guest_cannot_use_another_guests_things_open_a_store_it_is_not_at_or_settle_a_game_it_never_played`: the
  same in Free Mode, with a second guest's bag, the host's bag, stores and Pazaak, and both guests' credits.
- `thousands_of_random_commands_...` and `random_commands_from_a_guest_...`: 1,500 random commands each by
  default (every command type, real, foreign and extreme ids, strings of every shape), and for each refused one
  the world is unchanged. `REONE_COOP_FUZZ_ROUNDS` and `REONE_COOP_FUZZ_SEED` lengthen a hunt: 4 seeds x 100,000
  rounds (800,000 commands) passed, and 15,000-round runs pass under ASan/UBSan.
- Stale and forged rejoin tokens are covered by the existing `forged_and_stale_tokens_are_just_new_players`,
  `a_stolen_token_with_another_name_does_not_throw_the_owner_off`, `a_stranger_using_a_dropped_players_name_...`,
  `absent_characters_are_bounded_and_a_reused_name_brings_its_own_character` and the restarted-client tests.

## Found but not fixed

Nothing in the decoders or the host's command validation needed fixing. The random tests refuse nothing
they shouldn't and let nothing through. What a long hunt cannot show is a refusal path nobody thought to generate
(a valid-looking command that is wrongly accepted); for that the generator draws ids from real objects and the
tests above check specific shapes.
