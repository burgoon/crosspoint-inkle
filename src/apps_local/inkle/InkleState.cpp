#include "InkleState.h"

#include <HalStorage.h>

#include <cstdio>
#include <cstring>

namespace inkle {

namespace {

constexpr const char* STATE_PATH = "/.crosspoint/inkle.txt";
constexpr int FORMAT_VERSION = 1;

bool validWord(const char* w) {
  if (strlen(w) != WORD_LEN) return false;
  for (size_t i = 0; i < WORD_LEN; ++i) {
    if (w[i] < 'a' || w[i] > 'z') return false;
  }
  return true;
}

}  // namespace

void State::startDay(const int32_t day) {
  if (lastWonDay >= 0 && day > lastWonDay + 1) streak = 0;
  dayIndex = day;
  guessCount = 0;
  solved = false;
  for (auto& g : guesses) g.text[0] = '\0';
}

void State::recordResult() {
  ++played;
  if (solved) {
    ++won;
    streak = (lastWonDay == dayIndex - 1) ? streak + 1 : 1;
    if (streak > maxStreak) maxStreak = streak;
    lastWonDay = dayIndex;
  } else {
    streak = 0;
  }
}

bool load(State& state) {
  const String text = Storage.readFile(STATE_PATH);
  if (text.length() == 0) return false;

  State parsed;
  int version = 0;
  const char* p = text.c_str();
  while (*p) {
    const char* nl = strchr(p, '\n');
    const size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
    char line[48];
    if (len < sizeof(line)) {
      memcpy(line, p, len);
      line[len] = '\0';
      char word[16];
      long n = 0;
      if (sscanf(line, "v %ld", &n) == 1) {
        version = static_cast<int>(n);
      } else if (sscanf(line, "day %ld", &n) == 1) {
        parsed.dayIndex = static_cast<int32_t>(n);
      } else if (sscanf(line, "guess %15s", word) == 1) {
        if (parsed.guessCount < MAX_GUESSES && validWord(word)) {
          strcpy(parsed.guesses[parsed.guessCount].text, word);
          ++parsed.guessCount;
        }
      } else if (sscanf(line, "solved %ld", &n) == 1) {
        parsed.solved = n != 0;
      } else if (sscanf(line, "played %ld", &n) == 1) {
        parsed.played = static_cast<uint16_t>(n);
      } else if (sscanf(line, "won %ld", &n) == 1) {
        parsed.won = static_cast<uint16_t>(n);
      } else if (sscanf(line, "streak %ld", &n) == 1) {
        parsed.streak = static_cast<uint16_t>(n);
      } else if (sscanf(line, "max %ld", &n) == 1) {
        parsed.maxStreak = static_cast<uint16_t>(n);
      } else if (sscanf(line, "lastwon %ld", &n) == 1) {
        parsed.lastWonDay = static_cast<int32_t>(n);
      }
    }
    if (!nl) break;
    p = nl + 1;
  }
  if (version != FORMAT_VERSION) return false;
  state = parsed;
  return true;
}

bool save(const State& state) {
  char buf[320];
  int n = snprintf(buf, sizeof(buf), "v %d\nday %ld\nsolved %d\nplayed %u\nwon %u\nstreak %u\nmax %u\nlastwon %ld\n",
                   FORMAT_VERSION, static_cast<long>(state.dayIndex), state.solved ? 1 : 0, state.played, state.won,
                   state.streak, state.maxStreak, static_cast<long>(state.lastWonDay));
  for (int i = 0; i < state.guessCount && n > 0 && n < static_cast<int>(sizeof(buf)); ++i) {
    n += snprintf(buf + n, sizeof(buf) - n, "guess %s\n", state.guesses[i].text);
  }
  if (n <= 0 || n >= static_cast<int>(sizeof(buf))) return false;
  Storage.mkdir("/.crosspoint");
  return Storage.writeFile(STATE_PATH, String(buf));
}

}  // namespace inkle
