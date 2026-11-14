#pragma once

#include <string>
#include <vector>

#include "ArticleLog.h"
#include "activities/UiTabListActivity.h"

// Articles fork: the article shelf. It presents itself as the fourth Library
// tab (same tab strip, header, and rows), but lives in its own activity so the
// Library screen only needs a small hook. Row 0 starts a sync; the rest are
// the EPUBs in the articles folder, in download order.
class ArticlesActivity final : public UiTabListActivity {
 public:
  // Tab positions shared with the Library hook. The Library asserts these
  // against its own tabs, so an upstream tab change fails the build.
  static constexpr int LIBRARY_TABS = 3;
  static constexpr int ARTICLES_TAB = LIBRARY_TABS;

  ArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  int listCount() const override { return static_cast<int>(items.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
#if FREEINK_CAP_TOUCH
  bool canSwipeDelete(int index) const override { return index > 0 && index < listCount(); }
  void swipeDelete(int index) override;
#endif
  int tabCount() const override { return ARTICLES_TAB + 1; }
  int activeTab() const override { return ARTICLES_TAB; }
  const char* tabLabel(int index) const override;
  freeink::ui::TabIndicator tabIndicator(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;
  // The FreeInkUI header owns the title and its touch targets.
  void drawChrome() override {}
  void drawFooter() override;

 private:
  static constexpr freeink::ui::ActionId ACTION_SYNC = ACTION_TAB_USER;
  static constexpr freeink::ui::ActionId ACTION_BACK = ACTION_SYNC + 1;
  // A shelf is a reading queue, not an archive; older articles past this stay
  // on the card and are reachable from the file browser.
  static constexpr size_t MAX_ROWS = 200;
  // Upper bound on the folder scan itself.
  static constexpr size_t MAX_FILES = 600;

  static void syncActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  static void backActionTrampoline(const freeink::ui::ActionEvent& event, void* user);

  // Reads the folder and rebuilds the rows. Call under the render lock.
  void reload();
  void rebuildItems();
  void buildHeader(UiScreen& screen);
  void startSync();
  void handleBackAction();
  void toggleOrder();
  void promptDelete(int row);
  std::string pathFor(int row) const;
  bool tabsFocused() const { return ringPos() == 0; }

  // Parallel to `rows`; `items` holds one extra leading entry (the sync row)
  // and points into these strings, so they are rebuilt together.
  std::vector<articles::ShelfRow> rows;
  std::vector<std::string> titles;
  std::vector<std::string> sources;
  std::vector<freeink::ui::ListItem> items;
  std::string syncSubtitle;
  bool newestFirst = true;
  // Entered while a button is still held: ignore that button's release.
  bool lockNextConfirmRelease = false;
  bool lockNextBackRelease = false;
};
