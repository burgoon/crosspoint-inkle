#pragma once
#include <cstdint>

#include "InkleCore.h"

namespace inkle {

// Persisted progress: today's board plus lifetime stats. Lives in one small
// text file on the SD card and is rewritten after every submitted guess.
struct State {
  int32_t dayIndex = -1;  // puzzle the guesses below belong to
  uint8_t guessCount = 0;
  Word guesses[MAX_GUESSES];
  bool solved = false;

  uint16_t played = 0;
  uint16_t won = 0;
  uint16_t streak = 0;
  uint16_t maxStreak = 0;
  int32_t lastWonDay = -1;  // day index of the last solved puzzle, for streaks

  bool finished() const { return solved || guessCount >= MAX_GUESSES; }

  // Starts a fresh board for `day`, keeping the lifetime stats. A missed day
  // (no win on the day before `day`) ends the streak.
  void startDay(int32_t day);

  // Records the outcome of the current board; call once when it finishes.
  void recordResult();
};

bool load(State& state);
bool save(const State& state);

}  // namespace inkle
