#include "ArticlesNav.h"

#include <Logging.h>
#include <Memory.h>

#include "ArticleSyncActivity.h"
#include "ArticlesActivity.h"
#include "activities/ActivityManager.h"

namespace articles {

void openShelf(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  auto activity = makeUniqueNoThrow<ArticlesActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ART", "OOM: articles activity");
    return;
  }
  activityManager.replaceActivity(std::move(activity));
}

void openSync(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  auto activity = makeUniqueNoThrow<ArticleSyncActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ART", "OOM: article sync activity");
    return;
  }
  activityManager.replaceActivity(std::move(activity));
}

}  // namespace articles
