#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Articles fork: bookkeeping for the article shelf. Pure C++ (no firmware
// headers) so the host test in test/articles_core can build it.
namespace articles {

// Where synced articles are stored on the SD card.
constexpr const char* ARTICLES_FOLDER = "/Articles";

// True when the path points inside the articles folder.
bool isArticlePath(std::string_view path);

// Splits "Title - source.epub" into its two parts. The source is empty when
// the name has no " - " separator.
void splitDisplayName(std::string_view fileName, std::string& title, std::string& source);

// "Name.epub" -> "Name (2).epub". The stem is shortened when needed so stem
// plus suffix stays within maxStemBytes, which keeps the number visible in a
// name that was already cut to the length limit.
std::string numberedName(std::string_view fileName, int copy, size_t maxStemBytes = 100);

// Download history: one "feed entry id <TAB> file name" line per article,
// oldest first. It answers two questions: was this feed entry already
// downloaded (so a deleted article does not come back on the next sync), and
// in what order did the files arrive (file times are not reliable on devices
// without a clock).
class ArticleLog {
 public:
  static constexpr size_t MAX_ENTRIES = 300;
  // Longer ids (a feed without ids falls back to the download link) are
  // shortened to this, ending in a hash of the full id, so a full log always
  // fits MAX_FILE_BYTES.
  static constexpr size_t MAX_ID_LENGTH = 160;
  static constexpr size_t MAX_FILE_NAME_LENGTH = 120;
  static constexpr size_t MAX_FILE_BYTES = MAX_ENTRIES * (MAX_ID_LENGTH + MAX_FILE_NAME_LENGTH + 2);

  void parse(std::string_view text);
  std::string serialize() const;

  bool hasId(std::string_view id) const;
  // Position in download order, or -1 for a file the log does not know.
  int rankOf(std::string_view fileName) const;
  // Appends an entry; the oldest ones drop off past MAX_ENTRIES.
  void add(std::string_view id, std::string_view fileName);
  size_t size() const { return entries.size(); }

 private:
  struct Entry {
    std::string id;
    std::string fileName;
  };
  std::vector<Entry> entries;
};

// One shelf row. `rank` comes from ArticleLog::rankOf.
struct ShelfRow {
  std::string fileName;
  int rank = -1;
};

// Orders rows by download order. Files the log does not know (copied onto the
// card by hand) sort after the logged ones, by name.
void sortShelf(std::vector<ShelfRow>& rows, bool newestFirst);

}  // namespace articles
