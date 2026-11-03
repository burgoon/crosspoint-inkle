#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "InkleCore.h"
#include "InkleState.h"
#include "InkleWords.h"

using namespace inkle;

namespace {

std::string score(const char* guess, const char* answer) {
  Tile tiles[5];
  evaluate(guess, answer, tiles);
  std::string s;
  for (Tile t : tiles) s += t == Tile::Correct ? 'G' : t == Tile::Present ? 'Y' : '.';
  return s;
}

}  // namespace

TEST(Evaluate, ExactMatch) { EXPECT_EQ(score("crane", "crane"), "GGGGG"); }

TEST(Evaluate, NoMatch) { EXPECT_EQ(score("blimp", "stack"), "....."); }

TEST(Evaluate, PresentLetters) { EXPECT_EQ(score("earth", "heart"), "YYYYY"); }

TEST(Evaluate, DuplicateGuessLetterCountedOnce) {
  // Answer has one 'l'; the second 'l' in the guess must not be marked.
  EXPECT_EQ(score("llama", "pilot"), "Y....");
}

TEST(Evaluate, ExactMatchClaimsBeforePresent) {
  // Answer "abbey": guess "babes" -> b(Y) a(Y) b(G) e(G) s(.)
  EXPECT_EQ(score("babes", "abbey"), "YYGG.");
}

TEST(Evaluate, DuplicateAnswerLettersBothSatisfied) {
  // Answer "eerie": exact e's at 1 and 4 are claimed first; the guess's middle
  // 'e' then takes the one remaining 'e' (position 0 of the answer).
  EXPECT_EQ(score("geese", "eerie"), ".GY.G");
}

TEST(Hints, StrongerResultWins) {
  Hint hints[LETTERS] = {};
  Tile first[5], second[5];
  evaluate("llama", "pilot", first);
  mergeHints("llama", first, hints);
  EXPECT_EQ(hints['l' - 'a'], Hint::Present);
  EXPECT_EQ(hints['a' - 'a'], Hint::Absent);
  evaluate("pilot", "pilot", second);
  mergeHints("pilot", second, hints);
  EXPECT_EQ(hints['l' - 'a'], Hint::Correct);
  EXPECT_EQ(hints['a' - 'a'], Hint::Absent);  // untouched by the second row
}

TEST(Words, ListsAreSortedAndWellFormed) {
  for (size_t i = 1; i < ANSWER_COUNT; ++i) EXPECT_LT(memcmp(ANSWERS + (i - 1) * 5, ANSWERS + i * 5, 5), 0);
  for (size_t i = 1; i < ALLOWED_COUNT; ++i) EXPECT_LT(memcmp(ALLOWED + (i - 1) * 5, ALLOWED + i * 5, 5), 0);
  for (size_t i = 0; i < ANSWER_COUNT * 5; ++i) EXPECT_TRUE(ANSWERS[i] >= 'a' && ANSWERS[i] <= 'z');
}

TEST(Words, LookupsAndRejects) {
  EXPECT_TRUE(isAnswer("crane"));
  EXPECT_TRUE(isAllowed("crane"));
  EXPECT_FALSE(isAllowed("zzzzz"));
  EXPECT_FALSE(isAllowed("cran"));
  EXPECT_FALSE(isAllowed("CRANE"));
  EXPECT_FALSE(isAllowed(nullptr));
}

TEST(Days, FirstPuzzleIsDayZero) {
  EXPECT_EQ(dayIndexFromCivil(2021, 6, 19), 0);
  EXPECT_EQ(dayIndexFromCivil(2021, 6, 20), 1);
  EXPECT_EQ(dayIndexFromCivil(2022, 1, 1), 196);
  EXPECT_EQ(dayIndexFromCivil(2026, 11, 1), 1961);
  EXPECT_LT(dayIndexFromCivil(2020, 1, 1), 0);
}

TEST(Days, AnswersCycleThroughWholeListWithoutRepeats) {
  std::set<std::string> seen;
  for (int32_t d = 0; d < static_cast<int32_t>(ANSWER_COUNT); ++d) {
    Word w;
    answerForDay(d, w);
    ASSERT_TRUE(isAnswer(w.text)) << w.text;
    seen.insert(w.text);
  }
  EXPECT_EQ(seen.size(), ANSWER_COUNT);
}

TEST(Days, ConsecutiveDaysDiffer) {
  Word a, b;
  answerForDay(1961, a);
  answerForDay(1962, b);
  EXPECT_STRNE(a.text, b.text);
}

TEST(Days, CivilRoundTrip) {
  for (int32_t d = 0; d < 4000; d += 37) {
    int y, m, day;
    civilFromDayIndex(d, y, m, day);
    EXPECT_EQ(dayIndexFromCivil(y, m, day), d) << y << "-" << m << "-" << day;
  }
  int y, m, day;
  civilFromDayIndex(0, y, m, day);
  EXPECT_EQ(y, 2021);
  EXPECT_EQ(m, 6);
  EXPECT_EQ(day, 19);
}

TEST(DayBits, SetGetAndBounds) {
  DayBits bits;
  EXPECT_FALSE(bits.get(5));
  bits.set(5);
  bits.set(1961);
  EXPECT_TRUE(bits.get(5));
  EXPECT_TRUE(bits.get(1961));
  EXPECT_FALSE(bits.get(6));
  EXPECT_FALSE(bits.get(-1));
  bits.set(-1);
  bits.set(DayBits::CAPACITY + 10);
  EXPECT_EQ(bits.lastUsedByte(), 1961 / 8);
}

TEST(State, DailyStreakAndTotals) {
  State s;
  s.startDaily(100);
  s.daily.solved = true;
  s.daily.guessCount = 3;
  s.recordDaily();
  EXPECT_EQ(s.played, 1);
  EXPECT_EQ(s.won, 1);
  EXPECT_EQ(s.streak, 1);
  EXPECT_TRUE(s.solvedDays.get(100));

  s.startDaily(101);
  s.daily.solved = true;
  s.recordDaily();
  EXPECT_EQ(s.streak, 2);
  EXPECT_EQ(s.maxStreak, 2);

  s.startDaily(103);  // skipped 102
  EXPECT_EQ(s.streak, 0);
  s.daily.solved = false;
  s.daily.guessCount = MAX_GUESSES;
  s.recordDaily();
  EXPECT_EQ(s.played, 3);
  EXPECT_EQ(s.won, 2);
  EXPECT_TRUE(s.playedDays.get(103));
  EXPECT_FALSE(s.solvedDays.get(103));
}

TEST(State, ArchiveKeepsDailyStatsUntouched) {
  State s;
  s.startDaily(200);
  s.daily.solved = true;
  s.recordDaily();
  s.startArchive(150);
  s.archive.solved = true;
  s.archive.guessCount = 4;
  s.recordArchive();
  EXPECT_EQ(s.archivePlayed, 1);
  EXPECT_EQ(s.archiveWon, 1);
  EXPECT_EQ(s.played, 1);
  EXPECT_EQ(s.won, 1);
  EXPECT_EQ(s.streak, 1);
  EXPECT_EQ(s.lastWonDay, 200);
  EXPECT_TRUE(s.solvedDays.get(150));
  s.startArchive(149);
  EXPECT_EQ(s.archive.guessCount, 0);
  EXPECT_FALSE(s.archive.finished());
}

TEST(State, ADayIsNeverCountedTwice) {
  State s;
  s.startDaily(300);
  s.daily.solved = true;
  s.recordDaily();
  s.startDaily(300);
  s.daily.solved = true;
  s.recordDaily();
  EXPECT_EQ(s.played, 1);
  EXPECT_EQ(s.won, 1);
  s.startArchive(300);
  s.archive.solved = true;
  s.recordArchive();
  EXPECT_EQ(s.archivePlayed, 0);
}
