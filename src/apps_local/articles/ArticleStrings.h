#pragma once

// Articles fork: user-facing text for the article shelf. Kept out of the
// upstream translation tables so upstream translation updates never conflict;
// English only for now.
namespace articles {

constexpr const char* TEXT_ARTICLES = "Articles";
constexpr const char* TEXT_SYNC = "Sync articles";
constexpr const char* TEXT_SYNC_DONE = "Sync finished";
constexpr const char* TEXT_NO_FEED = "No article feed set";
constexpr const char* TEXT_HISTORY_UNREADABLE = "Cannot read the download history";
constexpr const char* TEXT_COUNT_FORMAT = "%u on this device";
constexpr const char* TEXT_ARTICLE_FEED = "Article feed";
constexpr const char* TEXT_BACK_TO_ARTICLES = "Back to Articles";

}  // namespace articles
