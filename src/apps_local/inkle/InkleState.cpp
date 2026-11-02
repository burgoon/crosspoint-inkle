// Progress bookkeeping only; serialization lives in InkleStorage.cpp so this
// file compiles on the host for tests.
#include "InkleState.h"

namespace inkle {

void Board::start(const int32_t day) {
  dayIndex = day;
  guessCount = 0;
  solved = false;
  for (auto& g : guesses) g.text[0] = '\0';
}

bool DayBits::get(const int32_t day) const {
  if (day < 0 || day >= CAPACITY) return false;
  return (bytes[day / 8] >> (day % 8)) & 1;
}

void DayBits::set(const int32_t day) {
  if (day < 0 || day >= CAPACITY) return;
  bytes[day / 8] |= static_cast<uint8_t>(1u << (day % 8));
}

int DayBits::lastUsedByte() const {
  for (int i = CAPACITY / 8 - 1; i >= 0; --i) {
    if (bytes[i]) return i;
  }
  return -1;
}

void State::startDaily(const int32_t day) {
  if (lastWonDay >= 0 && day > lastWonDay + 1) streak = 0;
  daily.start(day);
}

void State::recordDaily() {
  ++played;
  playedDays.set(daily.dayIndex);
  if (daily.solved) {
    ++won;
    solvedDays.set(daily.dayIndex);
    streak = (lastWonDay == daily.dayIndex - 1) ? streak + 1 : 1;
    if (streak > maxStreak) maxStreak = streak;
    lastWonDay = daily.dayIndex;
  } else {
    streak = 0;
  }
}

void State::startArchive(const int32_t day) { archive.start(day); }

void State::recordArchive() {
  ++archivePlayed;
  playedDays.set(archive.dayIndex);
  if (archive.solved) {
    ++archiveWon;
    solvedDays.set(archive.dayIndex);
  }
}

}  // namespace inkle
