#include "ArticleSyncActivity.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <OpdsFeedParser.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "ArticleStore.h"
#include "ArticleStrings.h"
#include "ArticlesNav.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace {

// This many failed downloads in a row end the sync: the network or the
// server is in trouble, and each further attempt can block for a full timeout.
constexpr int MAX_CONSECUTIVE_FAILURES = 3;

// A misbehaving endpoint can answer 200 with an HTML page; an EPUB is a ZIP.
// Same check as OpdsBookBrowserActivity::verifyAndRegisterEpub.
bool looksLikeEpub(const std::string& path) {
  uint8_t magic[4] = {0};
  HalFile file;
  if (Storage.openFileForRead("ART", path.c_str(), file)) file.read(magic, sizeof(magic));
  return magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4;
}

std::string articlePath(const std::string& fileName) { return std::string(articles::ARTICLES_FOLDER) + "/" + fileName; }

// "Title - Author.epub", numbered while that name is taken, so two entries
// with the same title never share a file. Empty when no free name was found.
std::string freeFileName(const std::string& title, const std::string& author) {
  const std::string base = opdsBookFilename(author, title, OpdsFilenameFormat::TitleAuthor);
  if (!Storage.exists(articlePath(base).c_str())) return base;
  for (int copy = 2; copy <= 20; copy++) {
    std::string name = articles::numberedName(base, copy);
    if (!Storage.exists(articlePath(name).c_str())) return name;
  }
  return {};
}

}  // namespace

ArticleSyncActivity::ArticleSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : CatalogActivity("ArticleSync", renderer, mappedInput) {}

void ArticleSyncActivity::onEnter() {
  CatalogActivity::onEnter();
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  checkAndConnectWifi();
}

// Same Wi-Fi teardown as CatalogActivity::onExit, but the heap-clearing
// restart lands on the shelf instead of Home.
void ArticleSyncActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToArticles();
  }
}

void ArticleSyncActivity::onBackButton() { articles::openShelf(renderer, mappedInput); }

bool ArticleSyncActivity::handleCustomInput() {
  if (state == State::DONE) {
    // Nothing to read here; go back to the shelf on the next loop pass.
    articles::openShelf(renderer, mappedInput);
    return true;
  }
  return CatalogActivity::handleCustomInput();
}

void ArticleSyncActivity::buildScreen(UiScreen& screen) {
  screenHeader(screen, articles::TEXT_ARTICLES);
  if (buildStatusScreen(screen, /*boldError=*/false, /*showDownloadTotal=*/true)) return;
  screen.centeredText(articles::TEXT_SYNC_DONE, screen.theme().bodyText);
}

// The render task reads state and the two messages while it draws, and this
// loop changes them many times without returning to the main loop, so every
// change here goes through the lock.
void ArticleSyncActivity::showError(const char* message) {
  {
    RenderLock lock(*this);
    state = State::ERROR;
    errorMessage = message;
  }
  requestUpdate();
}

bool ArticleSyncActivity::collectNew(const OpdsServer& server, const articles::ArticleLog& log,
                                     std::vector<Pending>& out) {
  // Basic auth and custom headers only: a feed behind an interactive login
  // has to be opened in the OPDS browser, not synced.
  opdsClient.setServer(server.url, server.username, server.password, server.activeCustomHeaders());
  opdsClient.resetAuthState();

  const std::string feedUrl = UrlUtils::buildUrl(server.url, "");
  OpdsFeedParser parser;
  const auto status = opdsClient.fetchFeed(feedUrl, parser);
  if (status != freeink::opds::OpdsClient::FetchStatus::Ok) {
    LOG_ERR("ART", "feed fetch failed: %s (%d)", server.url.c_str(), static_cast<int>(status));
    return false;
  }

  // Bounded by the parser's own entry cap; only these small records stay
  // alive during the downloads, the feed itself is freed on return.
  std::vector<OpdsEntry> entries = parser.takeEntries();
  out.reserve(entries.size());
  for (auto& entry : entries) {
    // Only direct, free downloads: no purchases, publication-document links, or protected books.
    if (entry.type != OpdsEntryType::BOOK || entry.purchase || entry.indirect || entry.lcpLicense) continue;
    const std::string& id = entry.id.empty() ? entry.href : entry.id;
    if (id.empty() || entry.href.empty() || log.hasId(id)) continue;
    out.push_back({id, std::move(entry.title), std::move(entry.author), UrlUtils::buildUrl(feedUrl, entry.href)});
  }
  // Feeds list newest first; download oldest first so the newest article is
  // the last one logged and sorts to the top of the shelf.
  std::reverse(out.begin(), out.end());
  return true;
}

ArticleSyncActivity::Fetch ArticleSyncActivity::fetchArticle(const Pending& article, const std::string& fileName) {
  const std::string dest = articlePath(fileName);
  const freeink::opds::HttpAuth auth = opdsClient.downloadAuth();
  std::vector<HttpDownloader::Header> headers = auth.headers;
  if (!auth.bearer.empty()) headers.push_back({"Authorization", "Bearer " + auth.bearer});
  // The downloader stages into a temp file and only replaces dest on success,
  // so a failed transfer leaves nothing here to clean up.
  const auto result = downloadFile(article.url, dest, auth.username, auth.password, headers);
  if (result == HttpDownloader::ABORTED) return Fetch::Cancelled;
  if (result != HttpDownloader::OK) {
    LOG_ERR("ART", "download failed (%d): %s", static_cast<int>(result), article.url.c_str());
    return Fetch::Failed;
  }
  if (!looksLikeEpub(dest)) {
    // Often a login or error page; retried on the next sync like any failure.
    LOG_ERR("ART", "not an EPUB: %s", article.url.c_str());
    Storage.remove(dest.c_str());  // freeFileName() chose an unused name, so this is our own file
    return Fetch::Failed;
  }
  clearBookCache(dest);
  return Fetch::Ok;
}

void ArticleSyncActivity::startBrowse() {
  beginLoading();

  articles::ArticleLog log;
  // An unreadable history must stop the sync: treating it as empty would
  // download every article again and then overwrite the history.
  if (!articles::loadLog(log)) {
    showError(articles::TEXT_HISTORY_UNREADABLE);
    return;
  }
  if (!Storage.ensureDirectoryExists(articles::ARTICLES_FOLDER)) {
    showError(tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }

  int feeds = 0;
  int feedsRead = 0;
  int downloaded = 0;
  int failed = 0;
  int consecutiveFailures = 0;
  bool cancelled = false;
  bool saveFailed = false;
  bool stop = false;
  // Copy: the store must not change under a loop that blocks for minutes.
  const std::vector<OpdsServer> servers = OPDS_STORE.getServers();
  for (const auto& server : servers) {
    if (stop) break;
    if (!server.articleFeed || server.url.empty()) continue;
    feeds++;
    {
      RenderLock lock(*this);
      beginLoading();
    }
    std::vector<Pending> pending;
    if (!collectNew(server, log, pending)) continue;
    feedsRead++;

    for (size_t i = 0; i < pending.size() && !stop; i++) {
      const Pending& article = pending[i];
      char progress[24];
      snprintf(progress, sizeof(progress), "%u/%u  ", static_cast<unsigned>(i + 1),
               static_cast<unsigned>(pending.size()));
      {
        RenderLock lock(*this);
        beginDownload(progress + article.title);
      }

      const std::string fileName = freeFileName(article.title, article.source);
      const Fetch outcome = fileName.empty() ? Fetch::Failed : fetchArticle(article, fileName);
      if (outcome == Fetch::Cancelled) {
        cancelled = stop = true;
      } else if (outcome == Fetch::Failed) {
        // Not logged, so the entry is tried again on the next sync.
        failed++;
        if (++consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) stop = true;
      } else {
        consecutiveFailures = 0;
        downloaded++;
        log.add(article.id, fileName);
        // Saved after every article so a crash or power loss cannot cause a
        // second download. Without a saved history the next sync would fetch
        // everything again, so a failed save ends this one.
        if (!articles::saveLog(log)) saveFailed = stop = true;
      }
    }
  }
  if (downloaded > 0) library::markLibraryIndexDirty();
  LOG_INF("ART", "sync: %d new, %d failed, %d/%d feeds", downloaded, failed, feedsRead, feeds);

  if (feeds == 0) {
    showError(articles::TEXT_NO_FEED);
  } else if (saveFailed) {
    showError(tr(STR_ERROR_GENERAL_FAILURE));
  } else if (feedsRead == 0) {
    showError(tr(STR_FETCH_FEED_FAILED));
  } else if (failed > 0 && downloaded == 0 && !cancelled) {
    showError(tr(STR_DOWNLOAD_FAILED));
  } else {
    {
      RenderLock lock(*this);
      state = State::DONE;
    }
    requestUpdate();
  }
}
