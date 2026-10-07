#include "AppLookAndFeel.h"
#include "AppLookAndFeelInternal.h"
#include <map>
#include <tuple>

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

namespace synth::theme {

// Concern: embedded-typeface resolution + the per-instance typeface cache.

juce::Typeface::Ptr loadEmbeddedTypeface(const juce::String& family, bool bold, bool medium) {
#ifdef HAS_FONT_ASSETS
    const void* data = nullptr;
    int dataSize = 0;

    auto pick = [&](const void* d, int s) {
        data = d;
        dataSize = s;
    };

    if (family == "Inter") {
        if (bold)
            pick(BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
        else if (medium)
            pick(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        else
            pick(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    } else if (family == "JetBrains Mono") {
        if (bold || medium)
            pick(BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
        else
            pick(BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    } else if (family == "Manrope") {
        if (bold)
            pick(BinaryData::ManropeBold_ttf, BinaryData::ManropeBold_ttfSize);
        else if (medium)
            pick(BinaryData::ManropeSemiBold_ttf, BinaryData::ManropeSemiBold_ttfSize);
        else
            pick(BinaryData::ManropeRegular_ttf, BinaryData::ManropeRegular_ttfSize);
    } else if (family == "Space Mono") {
        if (bold || medium)
            pick(BinaryData::SpaceMonoBold_ttf, BinaryData::SpaceMonoBold_ttfSize);
        else
            pick(BinaryData::SpaceMonoRegular_ttf, BinaryData::SpaceMonoRegular_ttfSize);
    } else if (family == "IBM Plex Sans") {
        if (bold || medium)
            pick(BinaryData::IBMPlexSansSemiBold_ttf, BinaryData::IBMPlexSansSemiBold_ttfSize);
        else
            pick(BinaryData::IBMPlexSansRegular_ttf, BinaryData::IBMPlexSansRegular_ttfSize);
    } else if (family == "IBM Plex Mono") {
        if (bold || medium)
            pick(BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
        else
            pick(BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
    }

    if (data == nullptr || dataSize == 0)
        return nullptr;

    // No caching here — getTypefaceForFont caches results in the per-instance map.
    return juce::Typeface::createSystemTypefaceFor(data, (size_t)dataSize);
#else
    juce::ignoreUnused(family, bold, medium);
    return nullptr;
#endif
}

namespace {

// The embedded UI typeface that uiFont measures and paints with, loaded once. Creating a typeface from
// font data is expensive (and on Windows registers a font resource each time), so it must not happen per
// measure or per repaint. DeletedAtShutdown releases it before JUCE shuts down, which a plain static
// Typeface::Ptr would outlive (see typefaceCache in AppLookAndFeel.h).
class EmbeddedUiTypeface : private juce::DeletedAtShutdown {
public:
    /** Message thread only (fonts are measured and painted there). */
    static juce::Typeface::Ptr get() { return instance().face; }
    /** The semi-bold cut, for the toolbar's captions. Message thread only. */
    static juce::Typeface::Ptr getSemiBold() { return instance().semiBold; }

private:
    static EmbeddedUiTypeface& instance() {
        if (instance_ == nullptr)
            instance_ = new EmbeddedUiTypeface();
        return *instance_;
    }
    EmbeddedUiTypeface()
        : face(loadEmbeddedTypeface("Inter", false, false))
        , semiBold(loadEmbeddedTypeface("Inter", false, true)) {}
    ~EmbeddedUiTypeface() override { instance_ = nullptr; }

    static inline EmbeddedUiTypeface* instance_ = nullptr;
    juce::Typeface::Ptr face;
    juce::Typeface::Ptr semiBold;
};

} // namespace

juce::Font AppLookAndFeel::uiFont(float height) {
    if (auto face = EmbeddedUiTypeface::get())
        return juce::Font(juce::FontOptions(face).withHeight(height));
    return juce::Font(juce::FontOptions(height));
}

juce::Font AppLookAndFeel::uiSemiBoldFont(float height) {
    if (auto face = EmbeddedUiTypeface::getSemiBold())
        return juce::Font(juce::FontOptions(face).withHeight(height));
    return juce::Font(juce::FontOptions(height).withStyle("Bold"));
}

// Measuring lays the glyphs out, and every card measures its footer labels each time it is built: rebuilding all the
// cards after an undo spent most of its time here, on the same few labels at the same few heights. So each (face,
// height, text) is measured once. The face is part of the key, so a typeface that arrives later is measured afresh;
// the memo is cleared rather than grown past a bound. Message thread only, like the typeface it reads.
int AppLookAndFeel::uiTextWidth(const juce::String& text, float height) {
    using Key = std::tuple<const juce::Typeface*, float, juce::String>;
    static std::map<Key, int> widths;
    const auto face = EmbeddedUiTypeface::get();
    Key key{face.get(), height, text};
    if (const auto known = widths.find(key); known != widths.end())
        return known->second;
    if (widths.size() >= 4096)
        widths.clear();
    const int width = juce::GlyphArrangement::getStringWidthInt(uiFont(height), text);
    widths.emplace(std::move(key), width);
    return width;
}

void AppLookAndFeel::refreshTypefaces() {
    uiTypeface = loadEmbeddedTypeface(theme.type.uiFamily, false, false);
    monoTypeface = loadEmbeddedTypeface(theme.type.monoFamily, false, false);
}

juce::Typeface::Ptr AppLookAndFeel::getTypefaceForFont(const juce::Font& font) {
    const bool bold = font.isBold();
    const bool medium = false; // JUCE Font has no "medium" flag; bold/regular only.

    const juce::String family = font.getTypefaceName();

    // The mono family is requested explicitly by name; the UI family is the JUCE default
    // sans-serif name. Resolve both against the embedded set.
    juce::String resolved = family;
    if (family == juce::Font::getDefaultSansSerifFontName())
        resolved = theme.type.uiFamily;
    else if (family == juce::Font::getDefaultMonospacedFontName())
        resolved = theme.type.monoFamily;

    const juce::String key = resolved + (bold ? "|b" : "|r");
    {
        const juce::SpinLock::ScopedLockType sl(typefaceCacheLock);
        if (typefaceCache.contains(key))
            return typefaceCache[key];
    }

    if (auto face = loadEmbeddedTypeface(resolved, bold, medium)) {
        const juce::SpinLock::ScopedLockType sl(typefaceCacheLock);
        typefaceCache.set(key, face);
        return face;
    }

    return juce::LookAndFeel_V4::getTypefaceForFont(font);
}

} // namespace synth::theme
