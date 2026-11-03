#include "InkleActivity.h"

#include <HalDisplay.h>
#include <TrustedTime.h>

#include <cstdio>
#include <cstdlib>
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
constexpr int PICK_BUTTON_HEIGHT = 64;
constexpr unsigned long LONG_PRESS_MS = 700;
constexpr int32_t PRACTICE_BASE = 100000;  // keeps practice words away from the daily sequence
constexpr const char* PAST_PUZZLES_PROMPT = "Past puzzles";

// Local calendar date -> puzzle index; -1 when the clock is not trustworthy.
int32_t todayIndex() {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return -1;
  const time_t t = static_cast<time_t>(now);
  struct tm local {};
  localtime_r(&t, &local);
  return inkle::dayIndexFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

void formatDate(const int32_t dayIndex, char* out, const size_t size) {
  int y = 0, m = 0, d = 0;
  inkle::civilFromDayIndex(dayIndex, y, m, d);
  snprintf(out, size, "%04d-%02d-%02d", y, m, d);
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
  statusBox = {boardX, boardY + boardHeight + pad, boardWidth, STATUS_HEIGHT};

  const int keyboardY = statusBox.y + STATUS_HEIGHT + pad;
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

  // Picker: text block in the upper half, three buttons in a row below it.
  const int buttonWidth = (boardWidth - 2 * TILE_GAP) / 3;
  const int buttonY = top + (bottom - top) / 2;
  pickTextY = top + (bottom - top) / 5;
  pickOlder = {boardX, buttonY, buttonWidth, PICK_BUTTON_HEIGHT};
  pickPlay = {boardX + buttonWidth + TILE_GAP, buttonY, buttonWidth, PICK_BUTTON_HEIGHT};
  pickNewer = {boardX + 2 * (buttonWidth + TILE_GAP), buttonY, buttonWidth, PICK_BUTTON_HEIGHT};
}

void InkleActivity::loadBoard() {
  const inkle::Board& b = board();
  inkle::answerForDay(b.dayIndex, answer);
  memset(rows, 0, sizeof(rows));
  memset(hints, 0, sizeof(hints));
  for (int i = 0; i < b.guessCount; ++i) {
    inkle::evaluate(b.guesses[i].text, answer.text, rows[i]);
    inkle::mergeHints(b.guesses[i].text, rows[i], hints);
  }
  current[0] = '\0';
  currentLen = 0;
  status.clear();
  setSubtitle();
}

void InkleActivity::setSubtitle() {
  if (today < 0) {
    snprintf(subtitle, sizeof(subtitle), "Practice");
    return;
  }
  char date[12];
  formatDate(board().dayIndex, date, sizeof(date));
  snprintf(subtitle, sizeof(subtitle), "Puzzle %ld  %s%s", static_cast<long>(board().dayIndex), date,
           archiveMode ? "  (past)" : "");
}

void InkleActivity::onEnter() {
  Activity::onEnter();
  computeLayout();

  inkle::load(state);
  today = todayIndex();
  const int32_t puzzle = today < 0 ? PRACTICE_BASE + state.played : today;
  if (state.daily.dayIndex != puzzle) {
    // A practice board never counts toward stats, so it does not close the
    // streak the way a missed calendar day does.
    if (today < 0) {
      state.daily.start(puzzle);
    } else {
      state.startDaily(puzzle);
    }
  }
  // Already recorded for this day (a timezone change can revisit a day): show
  // it as done rather than counting it twice.
  if (today >= 0 && !state.daily.finished() && state.playedDays.get(puzzle)) {
    state.daily.solved = state.solvedDays.get(puzzle);
    if (!state.daily.solved) state.daily.guessCount = inkle::MAX_GUESSES;
  }
  archiveMode = false;
  screen = Screen::Game;
  loadBoard();
  if (today < 0) status = "Clock unset: connect to Wi-Fi once";

  cursorKey = 0;
  fullRefreshNext = true;
  if (state.daily.finished()) showResult();
  requestUpdate(true);
}

void InkleActivity::onExit() {
  popup.dismiss();
  Activity::onExit();
}

void InkleActivity::pressKey(const int key) {
  if (board().finished()) {
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
  inkle::Board& b = board();
  const int row = b.guessCount;
  strcpy(b.guesses[row].text, current);
  ++b.guessCount;
  inkle::evaluate(current, answer.text, rows[row]);
  inkle::mergeHints(current, rows[row], hints);
  b.solved = strcmp(current, answer.text) == 0;
  current[0] = '\0';
  currentLen = 0;
  fullRefreshNext = true;

  if (b.finished()) {
    if (archiveMode) {
      state.recordArchive();
    } else if (today >= 0) {
      state.recordDaily();
    }
    showResult();
  }
  inkle::save(state);
}

void InkleActivity::showResult() {
  static char title[48];
  static char headline[96];
  static const char* const options[] = {"Close", PAST_PUZZLES_PROMPT};
  const inkle::Board& b = board();
  char upper[6];
  for (int i = 0; i < 6; ++i) upper[i] = answer.text[i] >= 'a' ? answer.text[i] - 32 : answer.text[i];
  if (b.solved && b.guessCount == 0) {
    snprintf(title, sizeof(title), "Already solved (%s)", upper);
  } else if (b.solved) {
    snprintf(title, sizeof(title), "Solved in %d/%d", b.guessCount, inkle::MAX_GUESSES);
  } else {
    snprintf(title, sizeof(title), "The word was %s", upper);
  }
  if (today < 0) {
    snprintf(headline, sizeof(headline), "Practice round (not counted)");
  } else if (archiveMode) {
    snprintf(headline, sizeof(headline), "Past puzzles: played %u, won %u", state.archivePlayed, state.archiveWon);
  } else {
    snprintf(headline, sizeof(headline), "Played %u  Won %u  Streak %u  Best %u", state.played, state.won, state.streak,
             state.maxStreak);
  }
  const int optionCount = today < 0 ? 1 : 2;
  popup.show(title, headline, options, optionCount, 0, [this](const int index) {
    if (index == 1) {
      openPicker();
    }
    requestUpdate();
  });
}

void InkleActivity::openPicker() {
  if (today <= 0) return;  // no clock, or no earlier puzzle exists
  pickDay = state.archive.active() && state.archive.dayIndex < today ? state.archive.dayIndex : today - 1;
  screen = Screen::Picker;
  fullRefreshNext = true;
  requestUpdate();
}

void InkleActivity::stepPick(const int32_t delta) {
  int32_t next = pickDay + delta;
  if (next < 0) next = 0;
  if (next > today - 1) next = today - 1;
  if (next != pickDay) {
    pickDay = next;
    requestUpdate();
  }
}

void InkleActivity::playPicked() {
  if (!(state.archive.active() && state.archive.dayIndex == pickDay)) {
    state.startArchive(pickDay);
    inkle::save(state);
  }
  archiveMode = true;
  screen = Screen::Game;
  loadBoard();
  fullRefreshNext = true;
  if (state.archive.finished()) showResult();
  requestUpdate();
}

void InkleActivity::leaveArchive() {
  archiveMode = false;
  screen = Screen::Game;
  loadBoard();
  fullRefreshNext = true;
  requestUpdate();
}

void InkleActivity::loop() {
  if (popup.isActive()) {
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }
  if (screen == Screen::Picker) {
    loopPicker();
  } else {
    loopGame();
  }
}

void InkleActivity::loopGame() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (archiveMode) {
      leaveArchive();
    } else {
      finish();
    }
    return;
  }
  // Long-press Confirm opens the picker from the buttons; touch users tap the
  // status line once today's puzzle is done.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    openPicker();
    return;
  }

  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (!archiveMode && state.daily.finished() && today > 0 && statusBox.contains(tx, ty)) {
      haptic_feedback::touchAction();
      openPicker();
      return;
    }
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

void InkleActivity::loopPicker() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    screen = Screen::Game;
    fullRefreshNext = true;
    requestUpdate();
    return;
  }
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (pickOlder.contains(tx, ty)) {
      haptic_feedback::touchAction();
      stepPick(-1);
    } else if (pickNewer.contains(tx, ty)) {
      haptic_feedback::touchAction();
      stepPick(+1);
    } else if (pickPlay.contains(tx, ty)) {
      haptic_feedback::touchAction();
      playPicked();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    stepPick(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    stepPick(+1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    stepPick(-30);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    stepPick(+30);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    playPicked();
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

void InkleActivity::drawButton(const Box& box, const char* label, const bool highlighted) const {
  renderer.drawRect(box.x, box.y, box.w, box.h, highlighted ? 3 : 1, true);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, box.x + (box.w - textWidth) / 2, box.y + (box.h - lineHeight) / 2, label);
}

void InkleActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  if (screen == Screen::Picker) {
    renderPicker();
  } else {
    renderGame();
  }
  if (popup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer(fullRefreshNext ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  fullRefreshNext = false;
}

void InkleActivity::renderGame() {
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), "Inkle", subtitle, true);
  const inkle::Board& b = board();

  for (int r = 0; r < inkle::MAX_GUESSES; ++r) {
    const bool isCurrent = r == b.guessCount && !b.finished();
    for (int c = 0; c < 5; ++c) {
      char letter = '\0';
      if (r < b.guessCount) {
        letter = b.guesses[r].text[c];
      } else if (isCurrent && c < currentLen) {
        letter = current[c];
      }
      drawTile(tiles[r][c], letter, r < b.guessCount ? rows[r][c] : Tile::Empty, isCurrent && letter);
    }
  }

  const char* line = status.c_str();
  if (status.empty() && !archiveMode && b.finished() && today > 0) line = PAST_PUZZLES_PROMPT;
  if (*line) {
    renderer.drawCenteredText(UI_12_FONT_ID, statusBox.y + (STATUS_HEIGHT - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                              line);
  }

  for (int k = 0; k < KEY_COUNT; ++k) drawKey(k, k == cursorKey && !mappedInput.hasTouch());

  const auto labels = mappedInput.mapLabels(archiveMode ? "Today" : "Back", "Key", "<", ">");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void InkleActivity::renderPicker() {
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), PAST_PUZZLES_PROMPT, nullptr,
                 true);

  char text[48];
  char date[12];
  formatDate(pickDay, date, sizeof(date));
  int y = pickTextY;
  snprintf(text, sizeof(text), "Puzzle %ld", static_cast<long>(pickDay));
  renderer.drawCenteredText(NOTOSANS_18_FONT_ID, y, text, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 6;
  renderer.drawCenteredText(UI_12_FONT_ID, y, date);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 18;

  const char* outcome = "Not played";
  if (state.archive.active() && state.archive.dayIndex == pickDay && !state.archive.finished()) {
    outcome = "In progress";
  } else if (state.solvedDays.get(pickDay)) {
    outcome = "Solved";
  } else if (state.playedDays.get(pickDay)) {
    outcome = "Played, not solved";
  }
  renderer.drawCenteredText(UI_12_FONT_ID, y, outcome);

  drawButton(pickOlder, "< Older", false);
  drawButton(pickPlay, "Play", true);
  drawButton(pickNewer, "Newer >", false);

  snprintf(text, sizeof(text), "Past puzzles: played %u, won %u", state.archivePlayed, state.archiveWon);
  renderer.drawCenteredText(SMALL_FONT_ID, pickPlay.y + PICK_BUTTON_HEIGHT + 24, text);

  const auto labels = mappedInput.mapLabels("Back", "Play", "Older", "Newer");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
