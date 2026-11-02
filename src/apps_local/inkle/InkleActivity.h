#pragma once
#include <cstdint>
#include <string>

#include "InkleCore.h"
#include "InkleState.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

// One daily five-letter word puzzle. The board and keyboard are hand-drawn
// through GfxRenderer; chrome (header, hints, popup) comes from the theme.
class InkleActivity final : public Activity {
 public:
  InkleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int KEY_COUNT = 28;  // 26 letters + ENTER + DEL
  static constexpr int KEY_ENTER = 19;
  static constexpr int KEY_DEL = 27;

  struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  };

  inkle::State state;
  inkle::Word answer;
  inkle::Tile rows[inkle::MAX_GUESSES][5] = {};
  inkle::Hint hints[inkle::LETTERS] = {};
  char current[6] = {0};
  int currentLen = 0;
  int cursorKey = 0;
  bool practice = false;
  bool fullRefreshNext = true;
  bool resultShown = false;
  std::string status;
  char subtitle[32] = {0};
  OptionPopup popup;

  Box tiles[inkle::MAX_GUESSES][5];
  Box keys[KEY_COUNT];
  int statusY = 0;

  void computeLayout();
  void rebuildBoard();
  void pressKey(int key);
  void submitGuess();
  void showResult();
  static char keyLetter(int key);
  static int keyRow(int key);
  void drawTile(const Box& box, char letter, inkle::Tile tile, bool pending) const;
  void drawKey(int key, bool highlighted) const;
};
