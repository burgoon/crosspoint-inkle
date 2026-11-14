#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ArticleLog.h"

using namespace articles;

TEST(ArticlePath, MatchesOnlyFilesInsideTheFolder) {
  EXPECT_TRUE(isArticlePath("/Articles/a.epub"));
  EXPECT_FALSE(isArticlePath("/Articles"));
  EXPECT_FALSE(isArticlePath("/Articles/"));
  EXPECT_FALSE(isArticlePath("/ArticlesOld/a.epub"));
  EXPECT_FALSE(isArticlePath("/Books/Articles/a.epub"));
}

TEST(DisplayName, SplitsTitleAndSource) {
  std::string title, source;
  splitDisplayName("A Long Title - example.com.epub", title, source);
  EXPECT_EQ(title, "A Long Title");
  EXPECT_EQ(source, "example.com");
}

TEST(DisplayName, UsesTheLastSeparator) {
  std::string title, source;
  splitDisplayName("Part One - The Sequel - example.org.epub", title, source);
  EXPECT_EQ(title, "Part One - The Sequel");
  EXPECT_EQ(source, "example.org");
}

TEST(DisplayName, NoSeparatorMeansNoSource) {
  std::string title, source;
  splitDisplayName("Just a title.epub", title, source);
  EXPECT_EQ(title, "Just a title");
  EXPECT_EQ(source, "");
}

TEST(Log, RoundTrips) {
  ArticleLog log;
  log.add("urn:uuid:1", "One - a.com.epub");
  log.add("urn:uuid:2", "Two - b.com.epub");
  ArticleLog copy;
  copy.parse(log.serialize());
  EXPECT_EQ(copy.size(), 2u);
  EXPECT_TRUE(copy.hasId("urn:uuid:1"));
  EXPECT_FALSE(copy.hasId("urn:uuid:3"));
  EXPECT_EQ(copy.rankOf("Two - b.com.epub"), 1);
  EXPECT_EQ(copy.rankOf("missing.epub"), -1);
}

TEST(Log, IgnoresBrokenLinesAndCarriageReturns) {
  ArticleLog log;
  log.parse("no tab here\n\tno id\nid1\tfile1.epub\r\n\nid2\tfile2.epub");
  EXPECT_EQ(log.size(), 2u);
  EXPECT_TRUE(log.hasId("id1"));
  EXPECT_EQ(log.rankOf("file1.epub"), 0);
  EXPECT_EQ(log.rankOf("file2.epub"), 1);
}

TEST(Log, SeparatorsInsideAnIdCannotCorruptTheFile) {
  ArticleLog log;
  log.add("bad\tid\nwith breaks", "f.epub");
  ArticleLog copy;
  copy.parse(log.serialize());
  EXPECT_EQ(copy.size(), 1u);
  EXPECT_TRUE(copy.hasId("bad\tid\nwith breaks"));
  EXPECT_EQ(copy.rankOf("f.epub"), 0);
}

TEST(Log, DropsOldestPastTheCap) {
  ArticleLog log;
  for (size_t i = 0; i < ArticleLog::MAX_ENTRIES + 5; ++i) {
    log.add("id" + std::to_string(i), "f" + std::to_string(i));
  }
  EXPECT_EQ(log.size(), ArticleLog::MAX_ENTRIES);
  EXPECT_FALSE(log.hasId("id4"));
  EXPECT_TRUE(log.hasId("id5"));
}

TEST(Log, RedownloadRanksByLatestArrival) {
  ArticleLog log;
  log.add("id1", "same.epub");
  log.add("id2", "other.epub");
  log.add("id3", "same.epub");
  EXPECT_EQ(log.rankOf("same.epub"), 2);
}

TEST(Shelf, NewestFirstThenUnknownFilesByName) {
  std::vector<ShelfRow> rows = {{"z-manual.epub", -1}, {"old.epub", 0}, {"a-manual.epub", -1}, {"new.epub", 5}};
  sortShelf(rows, true);
  EXPECT_EQ(rows[0].fileName, "new.epub");
  EXPECT_EQ(rows[1].fileName, "old.epub");
  EXPECT_EQ(rows[2].fileName, "a-manual.epub");
  EXPECT_EQ(rows[3].fileName, "z-manual.epub");
}

TEST(Shelf, OldestFirstKeepsUnknownFilesLast) {
  std::vector<ShelfRow> rows = {{"new.epub", 5}, {"manual.epub", -1}, {"old.epub", 0}};
  sortShelf(rows, false);
  EXPECT_EQ(rows[0].fileName, "old.epub");
  EXPECT_EQ(rows[1].fileName, "new.epub");
  EXPECT_EQ(rows[2].fileName, "manual.epub");
}

TEST(Log, LongIdsThatDifferOnlyAtTheEndStayDifferent) {
  ArticleLog log;
  const std::string prefix(400, 'u');
  log.add(prefix + "?sig=aaa", "a.epub");
  EXPECT_TRUE(log.hasId(prefix + "?sig=aaa"));
  EXPECT_FALSE(log.hasId(prefix + "?sig=bbb"));
}

TEST(NumberedName, GoesBeforeTheExtension) {
  EXPECT_EQ(numberedName("Title - a.com.epub", 2), "Title - a.com (2).epub");
}

TEST(NumberedName, ShortensALongStemSoTheNumberSurvives) {
  const std::string stem(100, 'x');
  const std::string name = numberedName(stem + ".epub", 3);
  EXPECT_EQ(name, std::string(96, 'x') + " (3).epub");
  EXPECT_NE(name, numberedName(stem + ".epub", 4));
}

TEST(NumberedName, DoesNotCutInsideAUtf8Character) {
  // 34 three-byte characters = 102 bytes; the cut must land on a boundary.
  std::string stem;
  for (int i = 0; i < 34; ++i) stem += "\xE6\x97\xA5";
  const std::string name = numberedName(stem + ".epub", 2);
  const std::string kept = name.substr(0, name.size() - std::string(" (2).epub").size());
  EXPECT_EQ(kept.size() % 3, 0u);
  EXPECT_LE(kept.size() + 4, 100u);
}

TEST(Log, LongIdsAreCutSoAFullLogFitsItsFile) {
  ArticleLog log;
  const std::string longId(2000, 'x');
  for (size_t i = 0; i < ArticleLog::MAX_ENTRIES; ++i) {
    log.add(longId + std::to_string(i), std::string(300, 'f'));
  }
  EXPECT_LE(log.serialize().size(), ArticleLog::MAX_FILE_BYTES);
}

TEST(Log, ALongIdStillMatchesItself) {
  ArticleLog log;
  const std::string longId = std::string(500, 'a') + "tail";
  log.add(longId, "f.epub");
  EXPECT_TRUE(log.hasId(longId));
  ArticleLog copy;
  copy.parse(log.serialize());
  EXPECT_TRUE(copy.hasId(longId));
}

TEST(Log, AnEntryWithoutAFileIsRememberedButRanksNothing) {
  ArticleLog log;
  log.add("gone", "");
  ArticleLog copy;
  copy.parse(log.serialize());
  EXPECT_TRUE(copy.hasId("gone"));
  EXPECT_EQ(copy.rankOf("anything.epub"), -1);
}

TEST(Log, EmptyTextIsAnEmptyLog) {
  ArticleLog log;
  log.add("id", "f.epub");
  log.parse("");
  EXPECT_EQ(log.size(), 0u);
}
