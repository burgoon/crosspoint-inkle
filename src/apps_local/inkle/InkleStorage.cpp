// Save-file format (version 2), one "key value" pair per line:
//   v 2, day/solved/guess (daily board), aday/asolved/aguess (archive board),
//   played/won/streak/max/lastwon, aplayed/awon, pbits/sbits (hex day bitsets).
// Version 1 files (daily board and stats only) still load.
#include <HalStorage.h>

#include <cstdio>
#include <cstring>

#include "InkleState.h"

namespace inkle {

namespace {

constexpr const char* STATE_PATH = "/.crosspoint/inkle.txt";
constexpr int FORMAT_VERSION = 2;

bool validWord(const char* w) {
  if (strlen(w) != WORD_LEN) return false;
  for (size_t i = 0; i < WORD_LEN; ++i) {
    if (w[i] < 'a' || w[i] > 'z') return false;
  }
  return true;
}

void addGuess(Board& board, const char* word) {
  if (board.guessCount < MAX_GUESSES && validWord(word)) {
    strcpy(board.guesses[board.guessCount].text, word);
    ++board.guessCount;
  }
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void parseBits(DayBits& bits, const char* hex, const size_t len) {
  for (size_t i = 0; i + 1 < len && i / 2 < sizeof(bits.bytes); i += 2) {
    const int hi = hexValue(hex[i]), lo = hexValue(hex[i + 1]);
    if (hi < 0 || lo < 0) return;
    bits.bytes[i / 2] = static_cast<uint8_t>(hi * 16 + lo);
  }
}

void appendBits(String& out, const char* key, const DayBits& bits) {
  const int last = bits.lastUsedByte();
  if (last < 0) return;
  out += key;
  out += ' ';
  char hex[3];
  for (int i = 0; i <= last; ++i) {
    snprintf(hex, sizeof(hex), "%02x", bits.bytes[i]);
    out += hex;
  }
  out += '\n';
}

void appendBoard(String& out, const char* prefix, const Board& board) {
  char line[48];
  snprintf(line, sizeof(line), "%sday %ld\n%ssolved %d\n", prefix, static_cast<long>(board.dayIndex), prefix,
           board.solved ? 1 : 0);
  out += line;
  for (int i = 0; i < board.guessCount; ++i) {
    snprintf(line, sizeof(line), "%sguess %s\n", prefix, board.guesses[i].text);
    out += line;
  }
}

}  // namespace

bool load(State& state) {
  const String text = Storage.readFile(STATE_PATH);
  if (text.length() == 0) return false;

  State parsed;
  int version = 0;
  const char* p = text.c_str();
  while (*p) {
    const char* nl = strchr(p, '\n');
    const size_t len = nl ? static_cast<size_t>(nl - p) : strlen(p);
    if (strncmp(p, "pbits ", 6) == 0) {
      parseBits(parsed.playedDays, p + 6, len - 6);
    } else if (strncmp(p, "sbits ", 6) == 0) {
      parseBits(parsed.solvedDays, p + 6, len - 6);
    } else if (len < 48) {
      char line[48];
      memcpy(line, p, len);
      line[len] = '\0';
      char word[16];
      long n = 0;
      if (sscanf(line, "v %ld", &n) == 1) {
        version = static_cast<int>(n);
      } else if (sscanf(line, "day %ld", &n) == 1) {
        parsed.daily.dayIndex = static_cast<int32_t>(n);
      } else if (sscanf(line, "solved %ld", &n) == 1) {
        parsed.daily.solved = n != 0;
      } else if (sscanf(line, "guess %15s", word) == 1) {
        addGuess(parsed.daily, word);
      } else if (sscanf(line, "aday %ld", &n) == 1) {
        parsed.archive.dayIndex = static_cast<int32_t>(n);
      } else if (sscanf(line, "asolved %ld", &n) == 1) {
        parsed.archive.solved = n != 0;
      } else if (sscanf(line, "aguess %15s", word) == 1) {
        addGuess(parsed.archive, word);
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
      } else if (sscanf(line, "aplayed %ld", &n) == 1) {
        parsed.archivePlayed = static_cast<uint16_t>(n);
      } else if (sscanf(line, "awon %ld", &n) == 1) {
        parsed.archiveWon = static_cast<uint16_t>(n);
      }
    }
    if (!nl) break;
    p = nl + 1;
  }
  if (version < 1 || version > FORMAT_VERSION) return false;
  state = parsed;
  return true;
}

bool save(const State& state) {
  String out;
  out.reserve(512);
  char line[96];
  snprintf(line, sizeof(line), "v %d\n", FORMAT_VERSION);
  out += line;
  appendBoard(out, "", state.daily);
  if (state.archive.active()) appendBoard(out, "a", state.archive);
  snprintf(line, sizeof(line), "played %u\nwon %u\nstreak %u\nmax %u\nlastwon %ld\naplayed %u\nawon %u\n", state.played,
           state.won, state.streak, state.maxStreak, static_cast<long>(state.lastWonDay), state.archivePlayed,
           state.archiveWon);
  out += line;
  appendBits(out, "pbits", state.playedDays);
  appendBits(out, "sbits", state.solvedDays);
  Storage.mkdir("/.crosspoint");
  return Storage.writeFile(STATE_PATH, out);
}

}  // namespace inkle
