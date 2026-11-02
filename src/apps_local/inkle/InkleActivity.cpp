#include "InkleActivity.h"

#include <HalDisplay.h>
#include <TrustedTime.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using inkle::Hint;
using inkle::Tile;

namespace {

constexpr const char* KEY_ROWS[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
constexpr int TILE_SIZE = 60;
constexpr int TILE_GAP = 8;
constexpr int KEY_WIDTH = 42;
constexpr int KEY_WIDE = 66;  // ENTER and DEL
constexpr int KEY_HEIGHT = 56;
constexpr int KEY_GAP = 4;
constexpr int KEY_ROW_GAP = 8;
constexpr int TILE_BORDER_PENDING = 2;
constexpr int TILE_BORDER_PRESENT = 4;
constexpr int STATUS_HEIGHT = 36;
constexpr int32_t PRACTICE_BASE = 100000;  // keeps practice words away from the daily sequence

// Local calendar date -> puzzle index; -1 when the clock is not trustworthy.
int32_t todayIndex() {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return -1;
  const time_t t = static_cast<time_t>(now);
  struct tm local {};
  localtime_r(&t, &local);
  return inkle::dayIndexFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

}  // namespace

InkleActivity::InkleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Inkle", renderer, mappedInput) {}

char InkleActivity::keyLetter(const int key) {
  if (key < 10) return KEY_ROWS[0][key];
  if (key < 19) return KEY_ROWS[1][key - 10];
  if (key == KEY_ENTER || key == KEY_DEL) return '\0';
  return KEY_ROWS[2][key - 20];
}

int InkleActivity::keyRow(const int key) { return key < 10 ? 0 : key < 19 ? 1 : 2; }

void InkleActivity::computeLayout() {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const int boardWidth = 5 * TILE_SIZE + 4 * TILE_GAP;
  const int boardHeight = inkle::MAX_GUESSES * TILE_SIZE + (inkle::MAX_GUESSES - 1) * TILE_GAP;
  const int keyboardHeight = 3 * KEY_HEIGHT + 2 * KEY_ROW_GAP;
  const Rect header = UITheme::getInstance().getHeaderRect(renderer, true, false);
  const int top = header.y + header.height;
  const int bottom = height - metrics.buttonHintsHeight;
  // Split the leftover vertical space evenly around board, status line and keyboard.
  const int spare = bottom - top - boardHeight - STATUS_HEIGHT - keyboardHeight;
  const int pad = spare > 0 ? spare / 4 : 0;

  const int boardX = (width - boardWidth) / 2;
  const int boardY = top + pad;
  for (int r = 0; r < inkle::MAX_GUESSES; ++r) {
    for (int c = 0; c < 5; ++c) {
      tiles[r][c] = {boardX + c * (TILE_SIZE + TILE_GAP), boardY + r * (TILE_SIZE + TILE_GAP), TILE_SIZE, TILE_SIZE};
    }
  }
  statusY = boardY + boardHeight + pad;

  const int keyboardY = statusY + STATUS_HEIGHT + pad;
  int key = 0;
  for (int row = 0; row < 3; ++row) {
    const int letters = static_cast<int>(strlen(KEY_ROWS[row]));
    const int wide = row == 2 ? 2 : 0;
    const int rowWidth = letters * KEY_WIDTH + wide * KEY_WIDE + (letters + wide - 1) * KEY_GAP;
    int x = (width - rowWidth) / 2;
    const int y = keyboardY + row * (KEY_HEIGHT + KEY_ROW_GAP);
    if (row == 2) {
      keys[KEY_ENTER] = {x, y, KEY_WIDE, KEY_HEIGHT};
      x += KEY_WIDE + KEY_GAP;
      key = KEY_ENTER + 1;
    }
    for (int i = 0; i < letters; ++i) {
      keys[key++] = {x, y, KEY_WIDTH, KEY_HEIGHT};
      x += KEY_WIDTH + KEY_GAP;
    }
    if (row == 2) keys[KEY_DEL] = {x, y, KEY_WIDE, KEY_HEIGHT};
  }
}

void InkleActivity::rebuildBoard() {
  memset(rows, 0, sizeof(rows));
  memset(hints, 0, sizeof(hints));
  for (int i = 0; i < state.guessCount; ++i) {
    inkle::evaluate(state.guesses[i].text, answer.text, rows[i]);
    inkle::mergeHints(state.guesses[i].text, rows[i], hints);
  }
  current[0] = '\0';
  currentLen = 0;
}

void InkleActivity::onEnter() {
  Activity::onEnter();
  computeLayout();

  inkle::load(state);
  const int32_t today = todayIndex();
  practice = today < 0;
  const int32_t puzzle = practice ? PRACTICE_BASE + state.played : today;
  if (state.dayIndex != puzzle) {
    // A practice board never counts toward stats, so it does not close the
    // streak the way a missed calendar day does.
    if (practice) {
      state.dayIndex = puzzle;
      state.guessCount = 0;
      state.solved = false;
    } else {
      state.startDay(puzzle);
    }
  }
  inkle::answerForDay(puzzle, answer);
  rebuildBoard();

  if (practice) {
    snprintf(subtitle, sizeof(subtitle), "Practice");
    status = "Clock unset: connect to Wi-Fi once";
  } else {
    snprintf(subtitle, sizeof(subtitle), "Puzzle %ld", static_cast<long>(puzzle));
  }
  cursorKey = 0;
  fullRefreshNext = true;
  resultShown = false;
  if (state.finished()) showResult();
  requestUpdate(true);
}

void InkleActivity::onExit() {
  popup.dismiss();
  Activity::onExit();
}

void InkleActivity::pressKey(const int key) {
  if (state.finished()) {
    if (key == KEY_ENTER) showResult();
    return;
  }
  status.clear();
  if (key == KEY_ENTER) {
    submitGuess();
    return;
  }
  if (key == KEY_DEL) {
    if (currentLen > 0) current[--currentLen] = '\0';
    return;
  }
  if (currentLen < 5) {
    current[currentLen++] = keyLetter(key);
    current[currentLen] = '\0';
  }
}

void InkleActivity::submitGuess() {
  if (currentLen < 5) {
    status = "Not enough letters";
    return;
  }
  if (!inkle::isAllowed(current)) {
    status = "Not in word list";
    return;
  }
  const int row = state.guessCount;
  strcpy(state.guesses[row].text, current);
  ++state.guessCount;
  inkle::evaluate(current, answer.text, rows[row]);
  inkle::mergeHints(current, rows[row], hints);
  state.solved = strcmp(current, answer.text) == 0;
  current[0] = '\0';
  currentLen = 0;
  fullRefreshNext = true;

  if (state.finished()) {
    if (!practice) state.recordResult();
    showResult();
  }
  inkle::save(state);
}

void InkleActivity::showResult() {
  static char title[48];
  static char headline[96];
  static const char* const options[] = {"Close"};
  char upper[6];
  for (int i = 0; i < 6; ++i) upper[i] = answer.text[i] >= 'a' ? answer.text[i] - 32 : answer.text[i];
  if (state.solved) {
    snprintf(title, sizeof(title), "Solved in %d/%d", state.guessCount, inkle::MAX_GUESSES);
  } else {
    snprintf(title, sizeof(title), "The word was %s", upper);
  }
  if (practice) {
    snprintf(headline, sizeof(headline), "Practice round (not counted)");
  } else {
    snprintf(headline, sizeof(headline), "Played %u  Won %u  Streak %u  Best %u", state.played, state.won, state.streak,
             state.maxStreak);
  }
  resultShown = true;
  popup.show(title, headline, options, 1, 0, [this](int) { requestUpdate(); });
}

void InkleActivity::loop() {
  if (popup.isActive()) {
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    for (int k = 0; k < KEY_COUNT; ++k) {
      if (keys[k].contains(tx, ty)) {
        haptic_feedback::touchAction();
        cursorKey = k;
        pressKey(k);
        requestUpdate();
        return;
      }
    }
    return;
  }

  // Buttons walk the keyboard: Left/Right along a row, Up/Down between rows.
  int next = cursorKey;
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    next = (cursorKey + 1) % KEY_COUNT;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    next = (cursorKey + KEY_COUNT - 1) % KEY_COUNT;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    const int dir = mappedInput.wasPressed(MappedInputManager::Button::Down) ? 1 : -1;
    const int row = (keyRow(cursorKey) + 3 + dir) % 3;
    // Land on the key whose centre is nearest horizontally in the target row.
    const int cx = keys[cursorKey].x + keys[cursorKey].w / 2;
    int best = cursorKey, bestDist = 1 << 30;
    for (int k = 0; k < KEY_COUNT; ++k) {
      if (keyRow(k) != row) continue;
      const int dist = abs(keys[k].x + keys[k].w / 2 - cx);
      if (dist < bestDist) {
        bestDist = dist;
        best = k;
      }
    }
    next = best;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    pressKey(cursorKey);
    requestUpdate();
    return;
  }
  if (next != cursorKey) {
    cursorKey = next;
    requestUpdate();
  }
}

void InkleActivity::drawTile(const Box& box, const char letter, const Tile tile, const bool pending) const {
  switch (tile) {
    case Tile::Correct:
      renderer.fillRect(box.x, box.y, box.w, box.h, true);
      break;
    case Tile::Present:
      renderer.drawRect(box.x, box.y, box.w, box.h, TILE_BORDER_PRESENT, true);
      break;
    case Tile::Absent:
      renderer.fillRectDither(box.x, box.y, box.w, box.h, Color::LightGray);
      break;
    case Tile::Empty:
      renderer.drawRect(box.x, box.y, box.w, box.h, pending ? TILE_BORDER_PENDING : 1, true);
      break;
  }
  if (!letter) return;
  const char text[2] = {static_cast<char>(letter - 32), '\0'};  // uppercase for display
  const int fontId = NOTOSANS_18_FONT_ID;
  const int textWidth = renderer.getTextWidth(fontId, text, EpdFontFamily::BOLD);
  const int lineHeight = renderer.getLineHeight(fontId);
  renderer.drawText(fontId, box.x + (box.w - textWidth) / 2, box.y + (box.h - lineHeight) / 2, text,
                    tile != Tile::Correct, EpdFontFamily::BOLD);
}

void InkleActivity::drawKey(const int key, const bool highlighted) const {
  const Box& box = keys[key];
  const char letter = keyLetter(key);
  const Hint hint = letter ? hints[letter - 'a'] : Hint::Unknown;
  switch (hint) {
    case Hint::Correct:
      renderer.fillRect(box.x, box.y, box.w, box.h, true);
      break;
    case Hint::Present:
      renderer.drawRect(box.x, box.y, box.w, box.h, 3, true);
      break;
    case Hint::Absent:
      renderer.fillRectDither(box.x, box.y, box.w, box.h, Color::LightGray);
      break;
    case Hint::Unknown:
      renderer.drawRect(box.x, box.y, box.w, box.h, 1, true);
      break;
  }
  if (highlighted) renderer.drawRect(box.x - 3, box.y - 3, box.w + 6, box.h + 6, 2, true);

  char text[6];
  if (key == KEY_ENTER) {
    strcpy(text, "ENTER");
  } else if (key == KEY_DEL) {
    strcpy(text, "DEL");
  } else {
    text[0] = static_cast<char>(letter - 32);
    text[1] = '\0';
  }
  const int fontId = key == KEY_ENTER || key == KEY_DEL ? SMALL_FONT_ID : UI_12_FONT_ID;
  const int textWidth = renderer.getTextWidth(fontId, text);
  const int lineHeight = renderer.getLineHeight(fontId);
  renderer.drawText(fontId, box.x + (box.w - textWidth) / 2, box.y + (box.h - lineHeight) / 2, text,
                    hint != Hint::Correct);
}

void InkleActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), "Inkle", subtitle, true);

  for (int r = 0; r < inkle::MAX_GUESSES; ++r) {
    const bool isCurrent = r == state.guessCount && !state.finished();
    for (int c = 0; c < 5; ++c) {
      char letter = '\0';
      if (r < state.guessCount) {
        letter = state.guesses[r].text[c];
      } else if (isCurrent && c < currentLen) {
        letter = current[c];
      }
      drawTile(tiles[r][c], letter, r < state.guessCount ? rows[r][c] : Tile::Empty, isCurrent && letter);
    }
  }

  if (!status.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusY + (STATUS_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                              status.c_str());
  }

  for (int k = 0; k < KEY_COUNT; ++k) drawKey(k, k == cursorKey && !mappedInput.hasTouch());

  const auto labels = mappedInput.mapLabels("Back", "Key", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (popup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(fullRefreshNext ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefreshNext = false;
}
