#include "ArticlesActivity.h"

#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "ArticleStore.h"
#include "ArticleStrings.h"
#include "ArticlesNav.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"  // goToLibrary
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"
#include "components/icons/listIcons.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 256;
}  // namespace

ArticlesActivity::ArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Articles", renderer, mappedInput, true) {
  tabPillMaxPad = 16;  // same pill padding as the Library tab strip
}

void ArticlesActivity::onEnter() {
  // One lock across the base lifecycle and the folder read: the base onEnter
  // schedules a paint, and the render task must not see half-built rows.
  RenderLock lock(*this);
  UiTabListActivity::onEnter();
  app.on(ACTION_SYNC, &ArticlesActivity::syncActionTrampoline, this);
  app.on(ACTION_BACK, &ArticlesActivity::backActionTrampoline, this);
  reload();
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  requestUpdate(true);
}

void ArticlesActivity::reload() {
  rows.clear();
  articles::ArticleLog log;
  if (!articles::loadLog(log)) LOG_ERR("ART", "cannot read the download history");

  auto dir = Storage.open(articles::ARTICLES_FOLDER);
  if (dir && dir.isDirectory()) {
    dir.rewindDirectory();
    char name[NAME_BUFFER_SIZE];
    rows.reserve(64);
    for (auto file = dir.openNextFile(); file && rows.size() < MAX_FILES; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      if (name[0] == '.' || !FsHelpers::hasReflowableBookExtension(name)) continue;
      rows.push_back({name, log.rankOf(name)});
    }
  }
  // Sort before cutting, so a very full folder hides its oldest articles and
  // not whichever files the card happens to list last.
  articles::sortShelf(rows, true);
  if (rows.size() > MAX_ROWS) rows.resize(MAX_ROWS);
  rows.shrink_to_fit();
  if (!newestFirst) articles::sortShelf(rows, false);
  rebuildItems();
}

void ArticlesActivity::rebuildItems() {
  titles.assign(rows.size(), {});
  sources.assign(rows.size(), {});
  items.assign(rows.size() + 1, {});

  if (!OPDS_STORE.hasArticleFeeds()) {
    syncSubtitle = articles::TEXT_NO_FEED;
  } else {
    char count[32];
    snprintf(count, sizeof(count), articles::TEXT_COUNT_FORMAT, static_cast<unsigned>(rows.size()));
    syncSubtitle = count;
  }
  items[0].label = articles::TEXT_SYNC;
  items[0].subtitle = syncSubtitle.c_str();
  items[0].icon = listIconFor(UIIcon::Wifi, 32);
  items[0].actionValue = 0;

  for (size_t i = 0; i < rows.size(); i++) {
    articles::splitDisplayName(rows[i].fileName, titles[i], sources[i]);
    auto& item = items[i + 1];
    item.label = titles[i].c_str();
    if (!sources[i].empty()) item.subtitle = sources[i].c_str();
    item.icon = listIconFor(UIIcon::Text, 32);
    item.actionValue = static_cast<int16_t>(i + 1);
  }
}

std::string ArticlesActivity::pathFor(const int row) const {
  if (row < 0 || row >= static_cast<int>(rows.size())) return {};
  return std::string(articles::ARTICLES_FOLDER) + "/" + rows[static_cast<size_t>(row)].fileName;
}

// --- tabs --------------------------------------------------------------------

const char* ArticlesActivity::tabLabel(const int index) const {
  switch (index) {
    case 0:
      return tr(STR_LIBRARY_TAB_RECENT);
    case 1:
      return tr(STR_LIBRARY_TAB_TITLE);
    case 2:
      return tr(STR_LIBRARY_TAB_AUTHOR);
    default:
      return articles::TEXT_ARTICLES;
  }
}

fui::TabIndicator ArticlesActivity::tabIndicator(const int index) const {
  if (index != ARTICLES_TAB) return fui::TabIndicator::None;
  return newestFirst ? fui::TabIndicator::Down : fui::TabIndicator::Up;
}

// A Library tab hands the screen back to the Library on that tab; the
// Articles tab flips the order, as tapping an active Library tab does.
void ArticlesActivity::onTabAction(const int index) {
  if (index == ARTICLES_TAB) {
    toggleOrder();
    return;
  }
  app.clearTapFlash();
  activityManager.goToLibrary(index);
}

void ArticlesActivity::stepTab(const int direction) {
  activityManager.goToLibrary(direction > 0 ? 0 : LIBRARY_TABS - 1);
}

void ArticlesActivity::toggleOrder() {
  RenderLock lock(*this);
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  newestFirst = !newestFirst;
  articles::sortShelf(rows, newestFirst);
  rebuildItems();
  auto& tabNav = activeNav();
  tabNav.selected = 0;
  tabNav.top = 0;
  lock.unlock();
  requestUpdate();
}

// --- actions -----------------------------------------------------------------

void ArticlesActivity::syncActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<ArticlesActivity*>(user)->startSync();
}

void ArticlesActivity::backActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<ArticlesActivity*>(user)->handleBackAction();
}

void ArticlesActivity::startSync() {
  app.clearTapFlash();
  // Without a feed there is nothing to join Wi-Fi for; the row says why.
  if (!OPDS_STORE.hasArticleFeeds()) return;
  articles::openSync(renderer, mappedInput);
}

// Back returns focus to the tab strip first, then leaves for Home, as in the
// Library.
void ArticlesActivity::handleBackAction() {
  if (!tabsFocused()) {
    activeNav().selected = 0;
    requestUpdate();
  } else {
    onGoHome();
  }
}

void ArticlesActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    startSync();
    return;
  }
  const std::string path = pathFor(index - 1);
  if (!path.empty()) onSelectBook(path);
}

void ArticlesActivity::onRowLongPress(const int index) {
  if (index > 0) promptDelete(index - 1);
}

#if FREEINK_CAP_TOUCH
void ArticlesActivity::swipeDelete(const int index) {
  activeNav().selected = index + 1;
  promptDelete(index - 1);
}
#endif

void ArticlesActivity::promptDelete(const int row) {
  const std::string path = pathFor(row);
  if (path.empty()) return;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM),
                                                              titles[static_cast<size_t>(row)]);
  if (!confirmation) {
    LOG_ERR("ART", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, path](const ActivityResult& result) {
    lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    lockNextBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
    if (result.isCancelled) return;
    {
      RenderLock lock(*this);
      if (!removeBookFile(path)) LOG_ERR("ART", "cannot delete %s", path.c_str());
      if (RECENT_BOOKS.removeByPath(path)) RECENT_BOOKS.saveToFile();
      library::markLibraryIndexDirty();
      reload();
      // The deleted row may have been the last one.
      auto& tabNav = activeNav();
      if (tabNav.selected > listCount()) tabNav.selected = listCount();
    }
    closeRouting();
    requestUpdate();
  });
}

// --- input -------------------------------------------------------------------

bool ArticlesActivity::handleCustomInput() {
  if (lockNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lockNextConfirmRelease = false;
    return true;
  }
  if (lockNextBackRelease && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockNextBackRelease = false;
    return true;
  }
  return false;
}

bool ArticlesActivity::handleButtons() {
  // Hold actions fire at the threshold; ActivityManager swallows the release
  // that follows, so it cannot land in the confirmation dialog.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    if (tabsFocused()) {
      toggleOrder();
    } else if (ringPos() > 1) {
      promptDelete(ringPos() - 2);
    }
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handleBackAction();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabsFocused()) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
    }
    return true;
  }
  return false;
}

// Press walks the ring; a hold pages through the rows. Unlike the base, a
// hold never steps the tab: stepping leaves this screen for the Library, and
// a still-held button would step straight back.
void ArticlesActivity::navigateButtons() {
  const int count = listCount();
  buttonNavigator.onNextPress([this, count] { moveRingTo(ringPos() == count ? 0 : ringPos() + 1); });
  buttonNavigator.onPreviousPress([this, count] { moveRingTo(ringPos() == 0 ? count : ringPos() - 1); });
  buttonNavigator.onNextContinuous([this, count] {
    if (ringPos() == 0) return;
    moveRingTo(ButtonNavigator::nextPageIndex(ringPos() - 1, count, activeNav().pageRows()) + 1);
  });
  buttonNavigator.onPreviousContinuous([this, count] {
    if (ringPos() == 0) return;
    moveRingTo(ButtonNavigator::previousPageIndex(ringPos() - 1, count, activeNav().pageRows()) + 1);
  });
}

// --- screen ------------------------------------------------------------------

// Copy of LibraryListActivity::buildHeader (CrossPoint 2.0.0) without search,
// and with sync where the Library has rebuild. Re-check it against the
// original when the Library header changes upstream.
void ArticlesActivity::buildHeader(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  fui::HeaderProps header;
  header.title = tr(STR_LIBRARY);
  header.titleText = theme.titleText;
  header.titleText.align = theme.headerTitleAlign;
  header.sidePadding = theme.headerSidePadding;
  header.minTouchSize = theme.minTouchSize;
  header.styles = theme.popup;
  if (header.styles.normal.border.kind == fui::PaintKind::None && theme.headerUnderline > 0) {
    header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    header.styles.normal.borderWidth = theme.headerUnderline;
  }
  header.trailingStyles = fui::plainStyles(fui::Paint::solid(fui::Color::Black));
  header.borderEdges = fui::EdgeBottom;
  GUI.applyHeaderStatus(renderer, header);
  if (mappedInput.hasTouch()) {
    header.leadingIcon = fui::bitmapFromIcon(icon_header_back_32);
    header.leadingAction = ACTION_BACK;
    header.trailingIcon = fui::bitmapFromIcon(icon_refresh_cw_32);
    header.trailingAction = ACTION_SYNC;
  }
  const auto frameRect = screen.frame().screen();
  const fui::Rect band = BaseTheme::headerBand(renderer,
                                               fui::Rect{frameRect.x, static_cast<int16_t>(metrics.topPadding),
                                                         frameRect.width, static_cast<int16_t>(metrics.headerHeight)},
                                               header);
  fui::header(screen.frame(), band, header);
}

void ArticlesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect area = UITheme::getContentArea(renderer);
  const auto bottomInset = static_cast<int16_t>(renderer.getScreenHeight() - area.y - area.height);
  buildHeader(screen);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(bottomInset + metrics.buttonHintsHeight), 0});
  buildTabBar(screen);

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 1;
  props.rowGap = std::max<int16_t>(screen.theme().listRowGap, 6);
  props.headerUnderline = false;
  syncTabListViewport(screen, props);
#if FREEINK_CAP_TOUCH
  configureSwipeDelete(screen, props);
#endif
  screen.list(props);
}

void ArticlesActivity::drawFooter() {
  const char* confirmLabel = tabsFocused() ? tr(STR_TOGGLE) : (ringPos() == 1 ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(tabsFocused() ? tr(STR_HOME) : tr(STR_BACK), confirmLabel, tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
