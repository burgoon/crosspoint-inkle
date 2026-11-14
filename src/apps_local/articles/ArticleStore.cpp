#include "ArticleStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace articles {

namespace {
constexpr const char* LOG_PATH = "/.crosspoint/articles.txt";
}  // namespace

bool loadLog(ArticleLog& log) {
  log.parse({});
  if (!Storage.exists(LOG_PATH)) return true;
  size_t bytes = 0;
  {
    HalFile file;
    if (!Storage.openFileForRead("ART", LOG_PATH, file)) return false;
    bytes = file.fileSize();
  }
  if (bytes == 0) return true;  // an empty file is an empty history
  std::string text;
  if (!Storage.readFileToString("ART", LOG_PATH, ArticleLog::MAX_FILE_BYTES + 1, text)) return false;
  log.parse(text);
  return true;
}

bool saveLog(const ArticleLog& log) {
  Storage.ensureDirectoryExists("/.crosspoint");
  const std::string text = log.serialize();
  const String content(text.c_str());
  // A failed Arduino String allocation yields an empty string; never write
  // that over a good history.
  if (content.length() != text.size() || !Storage.writeFile(LOG_PATH, content)) {
    LOG_ERR("ART", "cannot write %s", LOG_PATH);
    return false;
  }
  return true;
}

}  // namespace articles
