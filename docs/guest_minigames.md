# Guests and minigames (swoop races, turrets)

The host's game simulates every minigame (`MiniGameSession` and the module's own scripts, as
always). A guest takes part by owning the input:

- **A race a guest starts (Free Mode).** The NPC prompt for a minigame NPC now has a *Race*
  answer. Accepting it makes the host load the race module (found from the dialog's
  `StartNewModule("...mg")`, without running the dialog's story scripts). When the module's
  minigame starts, the guest owns it: the host's own keys are ignored and the guest's input
  drives the one input seam (`MiniGameInput`). Everyone else watches the host's screen.
- **A turret the host hands over.** When the host starts a turret (or any minigame) it can give the
  controls to a guest: a prompt on the host's screen, or `CoopSession::handMiniGame(clientId, true)`
  (the same shape as the terminal hand-over). The host's own story minigame keeps its effects.
- **Letting go.** The owner's `MiniGameQuit` command, or leaving the game, gives the controls back to
  the host, who finishes it (a minigame ends when its scripts leave the module; there is no early exit).

## Protocol (version 5)

The ids follow the workbench additions (`BenchView` 27, `BenchUpgrade`..`BenchClose` 27-31, rejects 33-37).

| addition | |
|---|---|
| `MiniGameControl` (message 28, client to host) | `seq`, `steer` (-1..1), `fire`, `pitchRate`/`yawRate` (-1..1), `pitchDelta`/`yawDelta` (one-shot degrees, at most 45) |
| `MiniGameState` (message 29, host to client) | about ten times a second: kind, module, owner, ticks, elapsed, speed, offset (turret aim), hit points, `firing`, every actor's role, hit points, dead flag, position and rotation; on the last message `active` false, `finished`, `quit` and `prize` |
| `MiniGameQuit` command (32) | |
| `NotYourMiniGame` (38), `MiniGameBusy` (39) rejects | not the player driving it; a minigame is running or already requested |

A control message that is out of range or not finite fails to decode, which counts against the sender
like any malformed message. A message from a player who is not driving (or when nothing is running) is
ignored and counted.

## Determinism

A run is a function of the owner's inputs and the host tick each was applied on. `CoopMiniGame::apply`
writes the newest input into the seam once per tick, before the simulation reads it. Inputs older than
half a second (15 ticks) fall back to neutral, so a stalled player lets go of the controls; one-shot aim
deltas add up (at most a quarter turn) and are used by exactly one tick; a lower or repeated `seq` is dropped.

## Results

- **Story effects.** A race a guest starts is sandboxed: the party's credits and XP and every global
  variable are saved when it is requested and put back when it ends, so a guest winning the Taris race
  does not win the story's race. `guestMinigamesAffectStory` (host option, off) keeps them. A minigame the
  host hands over is the host's story minigame and keeps its effects.
- **Prize.** A swoop race the guest finishes with the bike alive pays `guestRacePrize` credits (default
  100): Free Mode pays the guest's own credits, Story Mode the shared credits. Turrets pay nothing, and a
  guest who quit gets nothing. The engine has no result of its own (the scripts write globals), so
  "finished" means the minigame ran to its end with the player's actor alive.
- **Time.** The guest gets a notice with its time and prize; the same figures are in the final state.
- `guestMinigames` (host option, on) turns guest-started minigames off.

## Drawing it on the guest's screen

Every client follows the host into the minigame module (`ModuleInfo`) and loads it from its own
install, so `Game::startMiniGame` builds the same session, models, tracks and camera as on the host.
On a client the minigame is a *replica* (`Game::miniGameReplica`): its creation scripts never run and
the session is never stepped. Instead, once a tick (`Game::tickMiniGameReplica`):

- **Playback.** `MiniGameReplay` keeps the last second of `MiniGameState`s and runs a clock in host
  ticks about one and a half updates (4.5 ticks) behind the newest, pulled a tenth of the way back to
  that delay each tick when updates bunch or stall (a stall of a second snaps it). `sample()` blends
  the two states around the clock: positions, rotations (slerp), speed, elapsed time and the turret's
  aim (yaw the short way round); hit points and deaths come from the older state. Nothing is
  extrapolated, so the guest sees the host's run a few ticks late but never a guess.
- **Placing.** `applyMiniGameState` writes the blended state into the local session: each actor's
  position and track frame (`hook`; the swoop's bike is put back at `hook * offset`), the swoop's
  offset or the turret's aim, speed and hit points. Deaths since the last call go to the view, which
  plays them as on the host. The views then draw as they always do: `SwoopRace::update` places the
  bikes and pads and the chase camera; on a replica turret `Turret::prepareReplica` sets the aim
  from the state, takes the fighters off their rails and puts them where the host has them, and
  draws the player's bolts at the guns' rate while the host reports `firing`. Enemy guns are drawn
  too, but on a replica no bolt hits anything and no minigame script runs: the host decides.
- **Animations.** What the host's minigame scripts animate (`SWMG_PlayAnimation` and its removal:
  the gear chevrons, the engine glow, the turret's HUD) goes out with the next state, at most 64
  per state, and the client plays it on the same actor's models.
- **Aim prediction.** For the owner of a turret, the drawn aim runs ahead of the sampled state by the
  one-shot (mouse) aim it has sent that the host had not used when that state was captured (the
  state's `controlSeq` says which input the host has used; `MiniGameAimPrediction`). Without it the
  aim would lag the mouse by the playback delay plus the trip to the host. Keyboard aim rates are not
  predicted the same way: the client runs the turret's own rate integrator on the keys it sends
  (`Turret::replicaRateStep`) and adds the angle each unused input gives. The harness turret
  autopilot on a client also leads the fighters by the playback delay.
- **Sounds.** What the host's minigame plays goes out with the next state (at most 16 per state):
  the scripts' `PlaySound` (countdown, boost, penalty), actors' death sounds, and the bolts' hit
  sounds the host's turret plays. The client plays them; the shots and enemy guns its own view
  draws make their own sounds. There was no sound event in the co-op stream to reuse (VFX and
  projectiles only), so these ride on `MiniGameState`.
- **Input.** The guest's keys and mouse (and the harness autopilots `swoopauto`/`turretauto`, which
  steer from what is drawn) write the local input seam as on the host; the owner's client sends it as
  a `MiniGameControl` every tick (`CoopSession::clientMiniGameInput`), one-shot aim deltas once.
  A player who only watches sends nothing. Escape lets go (`MiniGameQuit`).
- **The end.** The final state (`active` false) shows everyone a result line
  (`miniGameResultText`): the race time and prize, or how many fighters the turret destroyed; the
  owner also gets the host's notice. The client keeps the finished minigame on screen for 2.5 s
  with the line drawn over it (over the race's own closing fade to black), holding the module the
  host has already moved on to (`_moduleHeldForResult`); events of that visit that arrive meanwhile
  are kept. Then it follows the host.

A guest can also start a race itself in Free Mode: using the Taris race announcer (whose
conversation runs `k_ptar_swooprun`) offers *Race*; answering it sends the host to `tar_m03mg` with
the guest driving, the story put back afterwards and the prize paid to the guest's own credits.

For this the state carries, per actor, its track frame as a rotation (four 16-bit fractions; a
decoder refuses one that is not of unit length), the player's `firing` flag, the owner's newest used
input (`controlSeq`) and the animations played since the last state (actor index, name of at most
32 characters, flags: looping, queue, overlay, remove) and the sounds played since the last state.

Harness: `coophandmg <client id> [1|0]` (host hands over / takes back), `coopmg` (the co-op side as
JSON on either end: owner, inputs used, the client's playback clock and buffer, the result line),
`coopmgquit` (client lets go); `mgstate` on a client reports `"replica":1` and the same `coop` object.
An integration probe (not included) runs a swoop race and the Ebon Hawk turret handed to a guest.

## Not done

- No command-line switch for the host options above.
- Sounds are played flat (not placed in the world), as the host's minigame plays them.
- The bike's engine loop (the player's `Sounds.Engine`) is played by each view itself, pitched with the
  speed, so a guest hears it without forwarding; the Taris track's area names none, so there is
  none to hear there (its wind and other sounds come from the scripts and are forwarded).
- A guest who joins mid-minigame misses the animations played before it joined.
