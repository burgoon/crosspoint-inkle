#pragma once

#include <OpdsClient.h>

#include <string>
#include <vector>

#include "ArticleLog.h"
#include "OpdsServerStore.h"
#include "activities/CatalogActivity.h"
#include "network/OpdsHttpTransport.h"

// Articles fork: downloads every new entry of the OPDS servers marked as
// article feeds into the articles folder, then returns to the shelf. The whole
// sync runs inside startBrowse(), blocking the main loop the way a single
// OPDS browser download does; Back cancels during a transfer. Wi-Fi,
// status screens, and the transfer loop come from CatalogActivity; the feed
// protocol comes from the SDK OPDS client, as in the OPDS browser.
class ArticleSyncActivity final : public CatalogActivity {
 public:
  ArticleSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  struct Pending {
    std::string id;
    std::string title;
    std::string source;
    std::string url;
  };

  int listCount() const override { return 0; }
  void activateIndex(int) override {}
  void buildScreen(UiScreen& screen) override;
  void startBrowse() override;
  bool hasSearch() const override { return false; }
  void performSearch(const std::string&) override {}
  void downloadFinished(bool) override {}
  void onBackButton() override;
  bool handleCustomInput() override;

  // Fetches one feed and lists its entries that are not in the log yet.
  // False when the feed could not be read.
  bool collectNew(const OpdsServer& server, const articles::ArticleLog& log, std::vector<Pending>& out);
  enum class Fetch { Ok, Failed, Cancelled };
  // Downloads one article into the folder under fileName.
  Fetch fetchArticle(const Pending& article, const std::string& fileName);
  void showError(const char* message);

  OpdsHttpTransport opdsTransport;
  freeink::opds::OpdsClient opdsClient{opdsTransport};
};
