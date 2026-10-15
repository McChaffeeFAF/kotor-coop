# Gamepad support and key rebinding

Input stays local; nothing about a pad goes on the co-op wire.

## Layers

1. **SDL to engine events** (`graphics/gamepadevents.h`): buttons (A is the bottom face button), axes
   scaled to -1..1 (triggers 0..1) and hot-plug become `input::Event`s. The engine opens a pad when it
   is plugged in and closes it when it goes; a machine without gamepad support plays on.
2. **Actions** (`game/keymap.h`, `game/padinput.h`): the in-world actions are the keymap's `KeyAction`s,
   as before. `Keymap` (keys) and `PadBindings` (pad buttons) both map to the same actions, so a pad
   button does exactly what the action's key does, whatever that key is bound to (or if it is bound to
   none). Defaults for keys are unchanged, so keyboard players see no difference.
3. **`PadInput`** turns pad events into outputs for the current mode (`Game::padMode`):

   | mode | when | what the pad does |
   |---|---|---|
   | World | the party in the world, third-person camera | left stick walks (W/S/Z/C), right stick turns the camera (A/D), buttons do their bound actions, Start opens the game menu |
   | Menu | any other screen and the confirm popup | D-pad and left stick move focus (held, they repeat after 0.4 s every 0.12 s), A uses the focused control, B and Start back out (Escape) |
   | Minigame | swoop race, turret | stick and D-pad are the arrow keys, A or the right trigger fire (Space) |
   | None | cutscenes, movies | B and Start back out |

   Sticks are four digital directions each with a dead zone (default 0.25, `pad.deadzone` in the
   settings) and hysteresis: pressed at half travel, let go below the dead zone. Triggers press at half
   pull and let go at a third. A mode change lets go of everything held, so nothing sticks.
4. **Menu focus** (`gui/navigation.h`, `GUI::handleNavigate`): a `Navigate` event moves the focused
   control to the nearest selectable, visible, enabled control that way (controls in line win over
   nearer ones off to the side), or uses it (the same click a mouse gives). A list steps through its
   rows first (`ListBox::handlePadDirection`), a slider moves a tenth per press.

## Default layout

A: default action (and use in menus) / B: cancel combat (back in menus) / X: pause / Y: map /
LB, RB: previous and next target / LT: change leader / RT: solo mode / D-pad: quick slots 1 to 3
(left, up, right) and the personal power slot (down) / Back: journal / L3: stealth / R3: flourish.
Start is the game menu and cannot be rebound.

## Pads

Hot-plug works; with several pads the first one plugged in drives the local player and the others
are ignored until it goes, when the next takes over. A pad that is unplugged lets go of everything it held.

## Rebinding

The controls page of the options menu (the existing `optkeymapping` list) shows each action's key and,
in brackets, its pad button. Choosing an action opens the key-entry popup: the next key binds the key,
the next pad button binds the pad side (Start cancels). A bind that takes a key or button another action
holds swaps the two, as keys always did; the reserved ones (Escape, Pause, Start, Guide) refuse.
`Keymap::conflict`/`locked` and `PadBindings::conflict` report this before a bind. Bindings live in
the player's settings file (`reone_settings.ini`, next to `reone.cfg`) as `key.<Action>=<key>` and
`pad.<Action>=<button>`; defaults are not written, `None` is an unbound action. "Reset to defaults"
resets both.

## Limits

- Sticks are digital: walking speed does not vary with how far the stick is pushed.
- Tested here with synthetic events only; nothing in this environment has a real pad or a display, so the
  menu focus drawing, the options page and hot-plug through SDL have not been seen on a screen.
- Menu focus works on the screens built from controls (buttons, lists, sliders); a screen that handles only
  the mouse itself (the galaxy map, Pazaak boards) needs its own use of `Navigate`.
- A radial quick-slot menu was not built; the D-pad gives the first three target slots and the personal power slot.
