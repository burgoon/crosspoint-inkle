#pragma once
#include <cstdint>

#include "InkleCore.h"

namespace inkle {

// One puzzle in progress or finished: which day, the guesses so far, outcome.
struct Board {
  int32_t dayIndex = -1;
  uint8_t guessCount = 0;
  Word guesses[MAX_GUESSES];
  bool solved = false;

  bool active() const { return dayIndex >= 0; }
  bool finished() const { return solved || guessCount >= MAX_GUESSES; }
  void start(int32_t day);
};

// Which past days have been played and solved, one bit per day since puzzle 0.
// Sized for roughly 26 years of daily puzzles.
struct DayBits {
  static constexpr int32_t CAPACITY = 9600;
  uint8_t bytes[CAPACITY / 8] = {0};

  bool get(int32_t day) const;
  void set(int32_t day);
  // Index of the last byte that holds any bit, or -1; bounds what gets saved.
  int lastUsedByte() const;
};

// Persisted progress: today's board, an optional archive board, lifetime stats.
// Daily results feed the streak and the daily totals; archive results only feed
// their own pair of counters, so the daily numbers keep meaning "showed up".
struct State {
  Board daily;
  Board archive;

  uint16_t played = 0;
  uint16_t won = 0;
  uint16_t streak = 0;
  uint16_t maxStreak = 0;
  int32_t lastWonDay = -1;  // day index of the last solved daily puzzle

  uint16_t archivePlayed = 0;
  uint16_t archiveWon = 0;

  DayBits playedDays;
  DayBits solvedDays;

  // Starts a fresh daily board for `day`, keeping stats. A missed day (no win
  // on the day before `day`) ends the streak.
  void startDaily(int32_t day);
  // Records the daily board's outcome; call once when it finishes.
  void recordDaily();

  // Starts (or replaces) the archive board for a past day.
  void startArchive(int32_t day);
  // Records the archive board's outcome; call once when it finishes.
  void recordArchive();
};

bool load(State& state);
bool save(const State& state);

}  // namespace inkle
