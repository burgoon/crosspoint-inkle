#pragma once
#include <cstddef>
#include <cstdint>

#include "InkleWords.h"

// Game rules only: no Arduino, no renderer, no storage. Testable on the host.
namespace inkle {

constexpr int MAX_GUESSES = 6;
constexpr int LETTERS = 26;

enum class Tile : uint8_t { Empty = 0, Absent, Present, Correct };

// Letter knowledge for the keyboard. Ordered so that a later, stronger result
// always wins: Correct > Present > Absent > Unknown.
enum class Hint : uint8_t { Unknown = 0, Absent, Present, Correct };

// Lowercase a-z word of WORD_LEN letters, null-terminated.
struct Word {
  char text[6] = {0};
};

// Scores `guess` against `answer` with the usual duplicate-letter rule: each
// answer letter can satisfy only one guess letter, and exact matches are
// claimed first.
void evaluate(const char* guess, const char* answer, Tile out[5]);

// Merges one scored row into the keyboard hints.
void mergeHints(const char* guess, const Tile tiles[5], Hint hints[LETTERS]);

bool isAllowed(const char* word);
bool isAnswer(const char* word);

// Days since 2021-06-19 (local date), the first puzzle. Negative before that.
int32_t dayIndexFromCivil(int year, int month, int day);
// Inverse of dayIndexFromCivil.
void civilFromDayIndex(int32_t dayIndex, int& year, int& month, int& day);

// The puzzle for a day index. Walks the answer list in a fixed shuffled order
// so consecutive days are not alphabetical neighbours.
void answerForDay(int32_t dayIndex, Word& out);

}  // namespace inkle
