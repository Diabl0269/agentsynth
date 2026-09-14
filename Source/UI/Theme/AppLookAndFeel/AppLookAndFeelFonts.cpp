#include "AppLookAndFeel.h"
#include "AppLookAndFeelInternal.h"

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
