#pragma once

#include "ArticleLog.h"

// Articles fork: SD persistence for the download history.
namespace articles {

// False when a history file exists but cannot be read; a missing or empty
// file is an empty history.
bool loadLog(ArticleLog& log);
bool saveLog(const ArticleLog& log);

}  // namespace articles
