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

#include <memory>

namespace reone {

namespace game {

class Game;
class Object;

/**
 * A companion leading the party does not hold a conversation with an object
 * that does not let party members interact (PartyInteract 0): the player
 * character takes it. The PC swaps places with the companion and becomes the
 * leader, and the companion is remembered so that control goes back to it when
 * the conversation ends, unless a script calls CancelPostDialogCharacterSwitch
 * (design note c11_data, K2 behaviour).
 *
 * @param speaker the one starting the conversation
 * @param other the object it talks to
 * @return true if the PC took the conversation
 */
bool handConversationToPlayer(Game &game, const std::shared_ptr<Object> &speaker, const std::shared_ptr<Object> &other);

/** Gives control back to the companion that handed a conversation to the PC. */
void switchBackAfterConversation(Game &game);

} // namespace game

} // namespace reone
