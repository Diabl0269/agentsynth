#pragma once

#include "Theme.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace synth::theme {

// JSON <-> Theme. Pure functions, no I/O (callers read/write files). All parsing is
// tolerant of missing OPTIONAL keys (defaults from Theme.h apply) and STRICT on the few
// required keys + on malformed values.
class ThemeLoader {
public:
    // Parse a theme from a juce::var (already JSON-parsed). Returns nullopt if the document
    // is not an object, the schema version is unsupported, a REQUIRED key is missing, or any
    // present value is malformed (bad hex, unknown style string). On nullopt the caller logs
    // ONE message (filename + reason) — ThemeLoader itself does not log.
    //   suggestedId: used to seed Theme::id when JSON omits "id" (e.g. the filename slug).
    static std::optional<Theme> parseTheme(const juce::var& json, const juce::String& suggestedId = {});

    // Serialize a Theme to a juce::var (a DynamicObject tree) for export / docs / round-trip.
    // Emits all tokens (colors as "#AARRGGBB"), the full metrics/typography/treatment, plus
    // "schemaVersion". Round-trips exactly through parseTheme (within Colour exactness;
    // floats are emitted at full precision).
    static juce::var themeToJson(const Theme& theme);

    // Convenience: parse the last error reason from the most recent parseTheme failure on
    // this thread (for the caller's single log line). Empty if last parse succeeded.
    static juce::String getLastError();

    // ---- Schema constants (also the source of truth for docs) ----
    static constexpr int kSchemaVersion = 1;

    // Required top-level keys. Everything else is optional with defaults.
    //   "name"  : string (display name)
    //   "colors": object (must contain at least bg0, surface, accent, textPrimary,
    //             audioWire, modWire — the minimum for a legible UI; others default)
    // Optional: "id", "schemaVersion", "metrics", "typography", "treatment", and any color
    //           token not in the required minimum.

    // ---- Parsing helpers (exposed for unit tests) ----
    // Accepts "#RGB", "#RRGGBB", "#AARRGGBB" (case-insensitive). Returns nullopt on malformed.
    static std::optional<juce::Colour> parseHexColour(const juce::String& s);
    // "flat"/"glass"/"textured" (case-insensitive). nullopt otherwise.
    static std::optional<ThemeStyle> parseStyle(const juce::String& s);
    static juce::String styleToString(ThemeStyle s); // "flat"/"glass"/"textured"
    // Treatment floats are clamped to [0,1]; out-of-range is clamped, NOT rejected.

private:
    ThemeLoader() = delete;
};

} // namespace synth::theme
