#pragma once

#ifndef GAME_RANDOMOPENING_H_
#define GAME_RANDOMOPENING_H_

#include "../program/play.h"

namespace RandomOpening {
  void initializeBalancedRandomOpening(
    Search* botB,
    Search* botW,
    Board& board,
    BoardHistory& hist,
    Player& nextPlayer,
    Rand& gameRand,
    const GameRunner* gameRunner,
    const OtherGameProperties& otherGameProps);
}  // namespace RandomOpening

#endif
