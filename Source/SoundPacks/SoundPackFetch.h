#pragma once

// How the installer gets bytes: one function type, so tests swap the network for a fake that writes a file.

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>

namespace synth::soundpacks {

struct FetchResult {
    bool ok = false;
    bool cancelled = false;
    juce::String error; ///< Plain words, set when !ok and not cancelled.
};

/** Called as bytes arrive; `total` is -1 when the server did not say. */
using FetchProgress = std::function<void(juce::int64 done, juce::int64 total)>;

/** Downloads `url` into `destination` on the calling (worker) thread, reporting progress and stopping when `cancel`
 *  turns true. Leaves no partial file behind on failure or cancel. */
using Fetcher = std::function<FetchResult(const juce::String& url, const juce::File& destination,
                                          const FetchProgress& progress, const std::atomic<bool>& cancel)>;

/** The real transport: juce::URL with redirects followed. A download is restarted from the beginning after an
 *  interruption (no HTTP Range resume): packs are tens of megabytes, and a restart keeps the checksum check simple. */
Fetcher makeHttpFetcher();

/** Test-only: while blocked the real fetcher answers "offline" without touching the network (the test binary sets it).
 */
void setHttpBlockedForTesting(bool blocked);

} // namespace synth::soundpacks
