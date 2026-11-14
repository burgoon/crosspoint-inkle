#pragma once

// Articles fork: entry points to the article screens. Kept here, not in
// ActivityManager, so the upstream class needs no fork methods.
class GfxRenderer;
class MappedInputManager;

namespace articles {

// Replaces the current screen with the article shelf.
void openShelf(GfxRenderer& renderer, MappedInputManager& mappedInput);
// Replaces the current screen with a sync, which ends on the shelf.
void openSync(GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace articles
