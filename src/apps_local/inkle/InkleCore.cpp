#include "InkleCore.h"

#include <cstring>

#include "InkleWords.h"

namespace inkle {

namespace {

int compareWord(const char* a, const char* b) { return memcmp(a, b, WORD_LEN); }

bool contains(const char* list, size_t count, const char* word) {
  size_t lo = 0, hi = count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int c = compareWord(list + mid * WORD_LEN, word);
    if (c == 0) return true;
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return false;
}

bool wellFormed(const char* word) {
  if (!word) return false;
  for (size_t i = 0; i < WORD_LEN; ++i) {
    if (word[i] < 'a' || word[i] > 'z') return false;
  }
  return word[WORD_LEN] == '\0';
}

}  // namespace

void evaluate(const char* guess, const char* answer, Tile out[5]) {
  int remaining[LETTERS] = {0};
  for (size_t i = 0; i < WORD_LEN; ++i) {
    out[i] = Tile::Absent;
    if (guess[i] == answer[i]) {
      out[i] = Tile::Correct;
    } else {
      ++remaining[answer[i] - 'a'];
    }
  }
  for (size_t i = 0; i < WORD_LEN; ++i) {
    if (out[i] == Tile::Correct) continue;
    int& left = remaining[guess[i] - 'a'];
    if (left > 0) {
      out[i] = Tile::Present;
      --left;
    }
  }
}

void mergeHints(const char* guess, const Tile tiles[5], Hint hints[LETTERS]) {
  for (size_t i = 0; i < WORD_LEN; ++i) {
    Hint next = Hint::Unknown;
    switch (tiles[i]) {
      case Tile::Correct:
        next = Hint::Correct;
        break;
      case Tile::Present:
        next = Hint::Present;
        break;
      case Tile::Absent:
        next = Hint::Absent;
        break;
      case Tile::Empty:
        break;
    }
    Hint& slot = hints[guess[i] - 'a'];
    if (static_cast<uint8_t>(next) > static_cast<uint8_t>(slot)) slot = next;
  }
}

bool isAnswer(const char* word) { return wellFormed(word) && contains(ANSWERS, ANSWER_COUNT, word); }

bool isAllowed(const char* word) {
  return wellFormed(word) && (contains(ALLOWED, ALLOWED_COUNT, word) || contains(ANSWERS, ANSWER_COUNT, word));
}

int32_t dayIndexFromCivil(int year, int month, int day) {
  // Days from civil date (Howard Hinnant's algorithm), then offset so that
  // 2021-06-19 is day 0.
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const int32_t yoe = year - era * 400;
  const int32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int32_t daysSinceEpoch = era * 146097 + doe - 719468;
  constexpr int32_t FIRST_PUZZLE_DAY = 18797;  // 2021-06-19
  return daysSinceEpoch - FIRST_PUZZLE_DAY;
}

void civilFromDayIndex(const int32_t dayIndex, int& year, int& month, int& day) {
  // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
  constexpr int32_t FIRST_PUZZLE_DAY = 18797;
  const int32_t z = dayIndex + FIRST_PUZZLE_DAY + 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = yoe + era * 400 + (month <= 2);
}

void answerForDay(int32_t dayIndex, Word& out) {
  // A multiplicative stride that is coprime with the list length visits every
  // answer exactly once before repeating, in a scrambled order.
  const uint32_t n = static_cast<uint32_t>(ANSWER_COUNT);
  uint32_t stride = 1103;  // prime; adjusted below if it divides n
  while (n % stride == 0) ++stride;
  const uint32_t day = static_cast<uint32_t>(dayIndex < 0 ? -dayIndex : dayIndex);
  const uint32_t index = static_cast<uint32_t>((static_cast<uint64_t>(day) * stride) % n);
  memcpy(out.text, ANSWERS + index * WORD_LEN, WORD_LEN);
  out.text[WORD_LEN] = '\0';
}

}  // namespace inkle
