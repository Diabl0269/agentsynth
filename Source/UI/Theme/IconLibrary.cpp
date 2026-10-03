#include "IconLibrary.h"

#include <optional>

#ifdef HAS_FONT_ASSETS
#include "BinaryData.h"
#endif

namespace synth::theme {

//==============================================================================
IconLibrary::IconLibrary() {
    for (int i = 0; i < (int)Icon::kCount; ++i) {
        const auto [data, size] = binaryDataForIcon(static_cast<Icon>(i));
        if (data == nullptr || size == 0)
            continue; // headless / missing asset → leave both arrays null at this index

        originals_[(size_t)i] = loadSVG(data, size); // pure-white master copy
        if (originals_[(size_t)i] != nullptr)
            drawables_[(size_t)i] = originals_[(size_t)i]->createCopy(); // white until first tint
    }
}

//==============================================================================
void IconLibrary::setTintColour(Icon id, juce::Colour c) {
    const auto idx = static_cast<size_t>(id);
    if (originals_[idx] == nullptr)
        return;

    // Always clone the UNTINTED original, then tint the clone. This ensures the 2nd, 3rd, ...
    // theme switch produces the correct colour (not a re-tint of an already-tinted drawable).
    auto clone = originals_[idx]->createCopy();
    clone->replaceColour(juce::Colours::white, c);
    // A multi-role icon tinted with one colour: the colour roles take it, the details are cut out.
    recolourRoles(*clone, {c, c.withMultipliedAlpha(kRoleSoftAlpha), juce::Colours::transparentBlack,
                           juce::Colours::transparentBlack});
    drawables_[idx] = std::move(clone);
}

std::unique_ptr<juce::Drawable> IconLibrary::createRecoloured(Icon id, const IconRoleColours& roles) const {
    const auto& original = originals_[static_cast<size_t>(id)];
    if (original == nullptr)
        return nullptr;
    auto clone = original->createCopy();
    recolourRoles(*clone, roles);
    return clone;
}

namespace {
// The role colour for one placeholder-painted colour, carrying over any opacity the SVG added on top
// of the role's own (Save's shutter is paper at 70 percent). Non-placeholder colours are left alone.
std::optional<juce::Colour> roleColourFor(juce::Colour colour, const IconRoleColours& roles) {
    const auto rgb = colour.getARGB() | 0xff000000u;
    const float alpha = colour.getFloatAlpha();
    if (rgb == kRoleInk)
        return roles.ink.withMultipliedAlpha(alpha);
    if (rgb == kRolePaper)
        return roles.paper.withMultipliedAlpha(alpha);
    if (rgb != kRoleHue)
        return std::nullopt;
    // A hue shape well under full opacity is the soft role (the SVG writes it at kRoleSoftAlpha).
    constexpr float kSoftThreshold = 0.6f;
    if (alpha < kSoftThreshold)
        return roles.soft.withMultipliedAlpha(juce::jmin(1.0f, alpha / kRoleSoftAlpha));
    return roles.hue.withMultipliedAlpha(alpha);
}

void recolourFill(juce::DrawableShape& shape, bool stroke, const IconRoleColours& roles) {
    const auto fill = stroke ? shape.getStrokeFill() : shape.getFill();
    if (!fill.isColour())
        return;
    if (const auto mapped = roleColourFor(fill.colour, roles)) {
        if (stroke)
            shape.setStrokeFill(*mapped);
        else
            shape.setFill(*mapped);
    }
}
} // namespace

void IconLibrary::recolourRoles(juce::Drawable& drawable, const IconRoleColours& roles) {
    if (auto* shape = dynamic_cast<juce::DrawableShape*>(&drawable)) {
        recolourFill(*shape, false, roles);
        recolourFill(*shape, true, roles);
    }
    for (auto* child : drawable.getChildren())
        if (auto* childDrawable = dynamic_cast<juce::Drawable*>(child))
            recolourRoles(*childDrawable, roles);
}

std::unique_ptr<juce::Drawable> IconLibrary::getDrawable(Icon id) const {
    const auto idx = static_cast<size_t>(id);
    if (drawables_[idx] == nullptr)
        return nullptr;
    return drawables_[idx]->createCopy();
}

const juce::Drawable* IconLibrary::peekDrawable(Icon id) const noexcept {
    return drawables_[static_cast<size_t>(id)].get();
}

//==============================================================================
std::pair<const void*, int> IconLibrary::binaryDataForIcon(Icon id) {
#ifdef HAS_FONT_ASSETS
    // Exhaustive lookup table — if Icon::kCount changes, this array must be updated.
    // The static_assert below guards the count.
    //
    // NOTE: JUCE's binary-data name mangler STRIPS hyphens (and concatenates the resulting
    // tokens) rather than converting them to underscores, so 'transport-play.svg' becomes
    // BinaryData::transportplay_svg (verified against the generated BinaryData.h). The dot
    // before the extension is the only separator that becomes '_'.
    static const std::pair<const void*, int> kTable[(size_t)Icon::kCount] = {
        {BinaryData::transportplay_svg, BinaryData::transportplay_svgSize},
        {BinaryData::transportstop_svg, BinaryData::transportstop_svgSize},
        {BinaryData::actionundo_svg, BinaryData::actionundo_svgSize},
        {BinaryData::actionredo_svg, BinaryData::actionredo_svgSize},
        {BinaryData::actionsave_svg, BinaryData::actionsave_svgSize},
        {BinaryData::actionload_svg, BinaryData::actionload_svgSize},
        {BinaryData::actionnew_svg, BinaryData::actionnew_svgSize},
        {BinaryData::actionsettings_svg, BinaryData::actionsettings_svgSize},
        {BinaryData::actionautoarrange_svg, BinaryData::actionautoarrange_svgSize},
        {BinaryData::actionfeedback_svg, BinaryData::actionfeedback_svgSize},
        {BinaryData::toggleai_svg, BinaryData::toggleai_svgSize},
        {BinaryData::togglematrix_svg, BinaryData::togglematrix_svgSize},
        {BinaryData::togglelibrary_svg, BinaryData::togglelibrary_svgSize},
        {BinaryData::themetoggle_svg, BinaryData::themetoggle_svgSize},
        {BinaryData::modulebypass_svg, BinaryData::modulebypass_svgSize},
        {BinaryData::modulemute_svg, BinaryData::modulemute_svgSize},
        {BinaryData::moduledelete_svg, BinaryData::moduledelete_svgSize},
        {BinaryData::catsources_svg, BinaryData::catsources_svgSize},
        {BinaryData::catsequencing_svg, BinaryData::catsequencing_svgSize},
        {BinaryData::catenvelopes_svg, BinaryData::catenvelopes_svgSize},
        {BinaryData::catfilters_svg, BinaryData::catfilters_svgSize},
        {BinaryData::catmodulationfx_svg, BinaryData::catmodulationfx_svgSize},
        {BinaryData::cattimefx_svg, BinaryData::cattimefx_svgSize},
        {BinaryData::catdynamics_svg, BinaryData::catdynamics_svgSize},
        {BinaryData::catutility_svg, BinaryData::catutility_svgSize},
        // Waveform glyphs (Phase 4).
        {BinaryData::waveformsine_svg, BinaryData::waveformsine_svgSize},
        {BinaryData::waveformsaw_svg, BinaryData::waveformsaw_svgSize},
        {BinaryData::waveformsquare_svg, BinaryData::waveformsquare_svgSize},
        {BinaryData::waveformtriangle_svg, BinaryData::waveformtriangle_svgSize},
        // Minimap toggle.
        {BinaryData::toggleminimap_svg, BinaryData::toggleminimap_svgSize},
        {BinaryData::moduledualio_svg, BinaryData::moduledualio_svgSize},
        // Timeline edit-tool strip (Cubase-style tools; see Source/UI/Timeline/EditTool.h).
        {BinaryData::toolselect_svg, BinaryData::toolselect_svgSize},
        {BinaryData::toolsplit_svg, BinaryData::toolsplit_svgSize},
        {BinaryData::toolglue_svg, BinaryData::toolglue_svgSize},
        {BinaryData::toolerase_svg, BinaryData::toolerase_svgSize},
        {BinaryData::toolmute_svg, BinaryData::toolmute_svgSize},
        {BinaryData::tooldraw_svg, BinaryData::tooldraw_svgSize},
        // Timeline track-header kind glyphs + the panel's follow-playhead toggle.
        {BinaryData::trackmidi_svg, BinaryData::trackmidi_svgSize},
        {BinaryData::trackaudio_svg, BinaryData::trackaudio_svgSize},
        {BinaryData::trackautomation_svg, BinaryData::trackautomation_svgSize},
        {BinaryData::followplayhead_svg, BinaryData::followplayhead_svgSize},
        // I/O category icon (Audio Input/Output library rows + Audio Output card chrome).
        {BinaryData::catio_svg, BinaryData::catio_svgSize},
        // DetachablePanelHost's icon-only detach/dock-back control.
        {BinaryData::actiondetachwindow_svg, BinaryData::actiondetachwindow_svgSize},
        // The Range edit tool (appended after the rest of the Tool* glyphs; see Icon::ToolRange).
        {BinaryData::toolrange_svg, BinaryData::toolrange_svgSize},
        // The mixer header's sources badge (appended after Icon::ToolRange).
        {BinaryData::mixersources_svg, BinaryData::mixersources_svgSize},
        // The toolbar's Hide/Show panel glyph (appended after Icon::MixerSources).
        {BinaryData::togglepanel_svg, BinaryData::togglepanel_svgSize},
    };
    static_assert(std::size(kTable) == (size_t)Icon::kCount,
                  "kTable size does not match Icon::kCount - update binaryDataForIcon lookup table");

    return kTable[static_cast<size_t>(id)];
#else
    juce::ignoreUnused(id);
    return {nullptr, 0}; // headless fallback (no asset library linked)
#endif
}

std::unique_ptr<juce::Drawable> IconLibrary::loadSVG(const void* data, int size) {
    if (data == nullptr || size == 0)
        return nullptr;

    auto xml = juce::parseXML(juce::String::createStringFromData(data, size));
    if (xml == nullptr)
        return nullptr;

    return juce::Drawable::createFromSVG(*xml);
}

} // namespace synth::theme
