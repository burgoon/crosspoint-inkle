#include "ArticleLog.h"

#include <algorithm>
#include <cstdint>

namespace articles {

namespace {

// Tabs and line breaks are the file's separators; ids come from a remote feed.
std::string cleanField(std::string_view value, const size_t maxLength) {
  if (value.size() > maxLength) value = value.substr(0, maxLength);
  std::string out(value);
  for (char& c : out) {
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  }
  return out;
}

// A long id keeps its start and ends in a hash of the whole id, so two ids
// that only differ past the cut stay different.
std::string boundedId(const std::string_view id) {
  if (id.size() <= ArticleLog::MAX_ID_LENGTH) return cleanField(id, ArticleLog::MAX_ID_LENGTH);
  uint32_t hash = 2166136261u;  // FNV-1a
  for (const char c : id) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  std::string out = cleanField(id, ArticleLog::MAX_ID_LENGTH - 9);
  out += '#';
  for (int shift = 28; shift >= 0; shift -= 4) out += HEX_DIGITS[(hash >> shift) & 0xF];
  return out;
}

}  // namespace

bool isArticlePath(const std::string_view path) {
  const std::string_view folder = ARTICLES_FOLDER;
  return path.size() > folder.size() + 1 && path.substr(0, folder.size()) == folder && path[folder.size()] == '/';
}

void splitDisplayName(const std::string_view fileName, std::string& title, std::string& source) {
  std::string_view name = fileName;
  const auto dot = name.rfind('.');
  if (dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
  const auto separator = name.rfind(" - ");
  if (separator == std::string_view::npos || separator == 0) {
    title.assign(name);
    source.clear();
    return;
  }
  title.assign(name.substr(0, separator));
  source.assign(name.substr(separator + 3));
}

std::string numberedName(const std::string_view fileName, const int copy, const size_t maxStemBytes) {
  std::string_view stem = fileName;
  std::string_view extension;
  const auto dot = fileName.rfind('.');
  if (dot != std::string_view::npos && dot > 0) {
    stem = fileName.substr(0, dot);
    extension = fileName.substr(dot);
  }
  const std::string suffix = " (" + std::to_string(copy) + ")";
  if (stem.size() + suffix.size() > maxStemBytes) {
    size_t keep = maxStemBytes > suffix.size() ? maxStemBytes - suffix.size() : 0;
    // Never cut inside a UTF-8 character.
    while (keep > 0 && (static_cast<uint8_t>(stem[keep]) & 0xC0) == 0x80) keep--;
    stem = stem.substr(0, keep);
  }
  std::string out(stem);
  out += suffix;
  out += extension;
  return out;
}

void ArticleLog::parse(const std::string_view text) {
  entries.clear();
  if (!text.empty()) entries.reserve(MAX_ENTRIES);  // one block up front, no growth while loading
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto tab = line.find('\t');
    if (tab != std::string_view::npos && tab > 0) {
      entries.push_back({boundedId(line.substr(0, tab)), cleanField(line.substr(tab + 1), MAX_FILE_NAME_LENGTH)});
    }
    start = end + 1;
  }
  if (entries.size() > MAX_ENTRIES) {
    entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(MAX_ENTRIES));
  }
}

std::string ArticleLog::serialize() const {
  std::string out;
  size_t bytes = 0;
  for (const auto& entry : entries) bytes += entry.id.size() + entry.fileName.size() + 2;
  out.reserve(bytes);
  for (const auto& entry : entries) {
    out += entry.id;
    out += '\t';
    out += entry.fileName;
    out += '\n';
  }
  return out;
}

bool ArticleLog::hasId(const std::string_view id) const {
  const std::string clean = boundedId(id);
  return std::any_of(entries.begin(), entries.end(), [&clean](const Entry& entry) { return entry.id == clean; });
}

int ArticleLog::rankOf(const std::string_view fileName) const {
  // Newest match wins: a re-downloaded file ranks by its latest arrival.
  for (size_t i = entries.size(); i > 0; --i) {
    if (entries[i - 1].fileName == fileName) return static_cast<int>(i - 1);
  }
  return -1;
}

void ArticleLog::add(const std::string_view id, const std::string_view fileName) {
  if (id.empty()) return;
  entries.push_back({boundedId(id), cleanField(fileName, MAX_FILE_NAME_LENGTH)});
  if (entries.size() > MAX_ENTRIES) entries.erase(entries.begin());
}

void sortShelf(std::vector<ShelfRow>& rows, const bool newestFirst) {
  std::sort(rows.begin(), rows.end(), [newestFirst](const ShelfRow& a, const ShelfRow& b) {
    const bool aLogged = a.rank >= 0;
    const bool bLogged = b.rank >= 0;
    if (aLogged != bLogged) return aLogged;
    if (aLogged && a.rank != b.rank) return newestFirst ? a.rank > b.rank : a.rank < b.rank;
    return a.fileName < b.fileName;
  });
}

}  // namespace articles
