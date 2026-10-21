# Dedicated co-op host

`engine --dedicated` runs a Free Mode co-op session with no local player. The
host process simulates the world and accepts guests; nobody has to sit at it.
Single-player and normal hosting (`--host`, the Co-op screen) are unchanged.

## Running it

```
engine --game /path/to/kotor --dedicated --host 27015 --coop-max-players 6 \
       --coop-password tableword --dedicated-save-slot 1000 --dedicated-autosave 300
```

Everything is a command-line option and a `reone.cfg` key of the same name
(`dedicated=true`, `host=27015`, ...).

| Option | Default | Meaning |
|---|---|---|
| `--dedicated` | off | Run as a dedicated host. Implies `--headless`, no audio, `--coop-mode free`. |
| `--host <port>` | 27015 | UDP port. Not a story host here: the dedicated host starts its own Free Mode session. |
| `--coop-max-players <1-8>` | 8 | Guests it takes. The host seat is not a player, so this is the whole game. With `--coop-server` the join-code service caps a game at 7. |
| `--coop-password <text>` | none | Guests must present it. Also usable with a normal `--host` and on the guest's `--join`. |
| `--coop-server <host[:port]>` | none | The join-code service: the host also gets a join code (`dedicated status`/`coopcode`). |
| `--dedicated-save-slot <n>` | 1000 | The save it resumes from and autosaves to. Slots 0 and 1 are the game's quick and auto saves and are refused. |
| `--dedicated-autosave <seconds>` | 300 | Interval between autosaves; 0 turns the interval off (shutdown still saves). |
| `--dedicated-start-module <name>` | game's first | With no save in the slot: the module a new game starts in. |
| `--dedicated-stdin <0/1>` | 1 | Read admin commands from standard input. |
| `--control-port <port>` | off | The existing loopback console; carries the admin commands too. |

Start-up: the host starts Free Mode hosting, then loads the save in
`--dedicated-save-slot` if there is one, else starts a quick new game (the same
one the `newgame` console command makes) and warps to `--dedicated-start-module`.
When the world is playable it logs `the world is up` and `status` reports
`running`. Guests join with `--join <ip:port>` or the Co-op screen as they would
any Free Mode host; their characters come from their own machines.

Exit codes: 0 after a clean shutdown (signal or admin), 4 if hosting could not
start, the save could not be restored or the world did not come up in ten
minutes.

## Saving

* Every `--dedicated-autosave` seconds of game time, and when a save is refused
  (loading, mid-transition) it is retried after 10 s rather than a whole interval.
* On shutdown, always: a save already running is waited for (30 s at most), then
  one more is taken, then the guests are sent away and the process exits.
* Saves go to `--dedicated-save-slot` as an ordinary manual save named
  "Dedicated autosave" / "Dedicated save" / "Dedicated shutdown". They carry
  the Free Mode loot ledger like any host save, and the next start resumes from
  them. Guest characters live on the guests' machines, as always.
* `SIGINT` and `SIGTERM` set a flag only; the main loop does the saving, so
  `kill`, Ctrl-C and `docker stop` are all clean.

## Who leads: the rules with no local leader

A normal Free Mode host has a person who clicks through conversations and
terminals and walks the party into area exits. The dedicated host has none, so:

1. **The story driver** is the guest who has been in the session longest (and
   whose character has arrived). If that guest leaves, the next-longest takes
   over. A guest who returns joins the back of the line (a reconnect is a new
   client id). The driver is told ("You lead the story") and shows in
   `players` / `status`. Nobody is a driver while the session is empty.
2. **Conversations and terminals.** The driver may start a conversation with any
   NPC or terminal (`HostLeadsConversation` is lifted for the driver only, and
   story terminals need no hand-over). A conversation the world starts (a
   script, a cutscene) is given to the driver to answer. The conversation runs
   as a handed-over terminal does today: it is shown to every guest, the driver
   picks replies, the driver's character speaks (skill checks use its skills).
   Other guests still cannot start conversations; they get the usual refusal.
3. **Nobody answers.** If no driver is present, a conversation waiting for a
   reply takes its first reply after 3 s; with a driver present it waits 90 s
   for a choice and then takes the first reply, so a driver who walks away does
   not freeze the world for everyone.
4. **Area transitions follow Free Mode rules.** A guest walking into an exit or
   using a transition door starts the ready check as usual; the party moves
   when every guest is ready, or a majority is after the 30 s timeout. The host
   is not counted: there is no host player to ask, so the guests alone decide.
   With no guest present a scripted transition simply happens.
5. **Pausing, stores, benches, minigames, Pazaak** work as in Free Mode for the
   guests. Nothing asks the host for approval: join requests are accepted
   (`--coop-ask-join` is ignored); use the password and `kick`.
6. **The host's own party** (the creature the engine uses as the leader and its
   AI companions) stays where it is and fights when attacked; nothing steers it.

## Admin interface

Commands work on standard input (one per line) and on the control port as
`dedicated <command>`. Replies are one JSON object per line with `"ok"`.

| Command | Does |
|---|---|
| `help` | Lists the commands. |
| `status` | State, uptime, module, players, story driver, autosave counters. The password is never shown. |
| `players` | `{"players":[{"id","name","companion","waiting","storyDriver"}]}` |
| `kick <id\|name>` | Removes the player and tells them why. Not a ban: they may join again (change the password for that). |
| `save` | Saves now (refused if a save is running or the game cannot save yet). |
| `shutdown` | (also `stop`, `quit`, `exit`) Saves, says goodbye, exits 0. |

The control port is loopback only and has no authentication; it is for the
operator's tooling, not for the network. Standard input is closed or empty in
many service managers: that only turns the stdin admin off; it never stops the
host.

## Password protocol note

The password travels at the end of the `Hello` message and only when set, so a
`Hello` without one is byte-for-byte what it was before. On the integrated
alpha-0.4.4 branch it shares protocol version 6 with co-op chat (one bump above
5 for both). A wrong or missing password gets `Reject` reason 10
(`bad_password`). No new message ids were added (the co-op chat branch uses 100
and up); the admin interface is not on the wire at all.

## What needs a real install

The unit tests (`dedicated.cpp`, `dedicatedoptions.cpp`, the `CoopDedicated...`
cases in `coopnet.cpp`) cover the settings, the leaderless rules, autosave
scheduling, the controller's start/resume/shutdown sequence against a fake game,
the admin commands, the password and the kick. They run without game data.

Needing a real K1/K2 install and not exercised here: loading a real save and
resuming it, a real conversation handed to a driver and the 3 s / 90 s fallback
(they need the conversation GUI and dialog files), real area exits, and a
guest and the dedicated host playing together. A manual check is: start the
host with `--dedicated --control-port 27300`, join from a second machine,
walk to an NPC, talk, `save`, send SIGTERM, start it again and confirm it
resumes. The host still creates the OpenGL context the engine always creates
(the window is hidden); on a machine with no display it needs a software GL
such as Mesa llvmpipe under `xvfb-run` until the resource loading no longer
needs a context.
