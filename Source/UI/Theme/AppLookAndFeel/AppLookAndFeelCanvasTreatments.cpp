#include "AppLookAndFeel.h"
#include "UI/Graph/CableColour.h"

namespace synth::theme {

// Concern: public treatment helpers the bespoke painters (ModuleComponent, GraphEditor) call —
// module-card panels, connection wires, modulation rings, and the themed/canvas background.

namespace {
juce::Image makeGridTile(juce::Colour dot) {
    constexpr int kTile = 22;
    juce::Image img(juce::Image::ARGB, kTile, kTile, true);
    juce::Graphics g(img);
    g.setColour(dot);
    g.fillEllipse(0.0f, 0.0f, 1.5f, 1.5f);
    return img;
}
} // namespace

//==============================================================================
// Public treatment helpers
//==============================================================================
void AppLookAndFeel::drawModulePanel(juce::Graphics& g, juce::Rectangle<float> bounds, int headerHeight,
                                     const juce::String& title, bool selected, bool bypassed) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    const float radius = m.cornerRadius;
    auto body = bounds.reduced(2.0f);

    // Soft drop shadow. We intentionally AVOID juce::DropShadow here: its per-paint gaussian
    // blur re-rasterizes every time a (buffered) card is re-rendered at a new zoom scale, which
    // was the dominant cost behind zoom-in/out lag. Instead, approximate a soft shadow with a
    // few translucent, downward-offset, expanding rounded rects — visually close, but a fraction
    // of the cost (plain fills, no blur), so zooming stays smooth.
    if (tr.shadow > 0.0f) {
        for (int i = 3; i >= 1; --i) {
            const float grow = (float)i * 1.5f;
            g.setColour(juce::Colours::black.withAlpha(tr.shadow * 0.13f / (float)i));
            g.fillRoundedRectangle(body.expanded(grow).translated(0.0f, 2.0f + (float)i), radius + grow);
        }
    }

    // Effective body fill colour (bypass desaturates/dims).
    auto surfaceCol = c.surface;
    auto surfaceHiCol = c.surfaceHi;
    if (bypassed) {
        const auto grey = juce::Colour(0xff808080);
        surfaceCol = surfaceCol.interpolatedWith(grey, 0.4f).withAlpha(0.5f);
        surfaceHiCol = surfaceHiCol.interpolatedWith(grey, 0.4f).withAlpha(0.5f);
    }

    // ---- Body fill per style ----
    {
        // Hoist path to member (avoids stack allocation on every paint)
        bodyPath.emplace();
        bodyPath->addRoundedRectangle(body, radius);
        g.saveState();
        g.reduceClipRegion(*bodyPath);

        if (tr.style == ThemeStyle::Glass) {
            g.setColour(surfaceCol);
            g.fillRect(body);
            // Top highlight gradient over the top ~40%.
            const float hAlpha = 0.05f * tr.blur + 0.05f;
            glassTopHiGradient.emplace(juce::Colours::white.withAlpha(hAlpha), body.getX(), body.getY(),
                                       juce::Colours::transparentWhite, body.getX(),
                                       body.getY() + body.getHeight() * 0.4f, false);
            g.setGradientFill(*glassTopHiGradient);
            g.fillRect(body);
        } else if (tr.style == ThemeStyle::Textured) {
            texturedGradient.emplace(surfaceHiCol, body.getX(), body.getY(), surfaceCol.darker(0.1f), body.getX(),
                                     body.getBottom(), false);
            texturedGradient->addColour(0.5, surfaceCol);
            g.setGradientFill(*texturedGradient);
            g.fillRect(body);
            // Brushed striations: O(width) vertical hairlines.
            if (tr.texture > 0.0f) {
                const int step = 3;
                for (int xi = (int)body.getX(); xi < (int)body.getRight(); xi += step) {
                    g.setColour(((xi / step) % 2 == 0) ? juce::Colours::white.withAlpha(tr.texture * 0.025f)
                                                       : juce::Colours::black.withAlpha(tr.texture * 0.03f));
                    g.drawVerticalLine(xi, body.getY(), body.getBottom());
                }
            }
        } else { // Flat
            flatGradient.emplace(surfaceHiCol, body.getX(), body.getY(), surfaceCol, body.getX(), body.getBottom(),
                                 false);
            g.setGradientFill(*flatGradient);
            g.fillRect(body);
        }

        g.restoreState();
    }

    // ---- Header band ----
    {
        auto header = body.withHeight((float)headerHeight);
        // Hoist path to member (avoids stack allocation on every paint)
        headerPath.emplace();
        headerPath->addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius,
                                        radius, true, true, false, false);
        if (tr.style == ThemeStyle::Glass)
            g.setColour(juce::Colours::white.withAlpha(0.05f));
        else
            g.setColour(surfaceHiCol);
        g.fillPath(*headerPath);

        // Bottom hairline.
        g.setColour(c.border);
        g.drawHorizontalLine((int)header.getBottom(), header.getX(), header.getRight());

        // Title: uppercase, tracked.
        g.setColour(selected ? c.accent : (bypassed ? c.textDisabled : c.textPrimary));
        g.setFont(juce::Font(juce::FontOptions(theme.type.h2, juce::Font::bold)));
        juce::String tracked;
        for (auto ch : title.toUpperCase())
            tracked << juce::String::charToString(ch) << " ";
        // Asymmetric inset: 22px on the left clears the activity LED (fillEllipse(6,8,8,8) in
        // ModuleComponent::paint(), right edge 14) plus an 8px grid-aligned gap, regardless of
        // whether the LED is currently lit — so the title never shifts when RMS crosses the
        // lit/unlit threshold.
        g.drawText(tracked.trimEnd(), header.withTrimmedLeft(22.0f).withTrimmedRight(10.0f).toNearestInt(),
                   juce::Justification::centredLeft, true);
    }

    // ---- Border + selection ----
    if (selected) {
        if (tr.glow > 0.0f) {
            g.setColour(c.accent.withAlpha(tr.glow * 0.2f));
            g.drawRoundedRectangle(body.expanded(4.0f), radius + 4.0f, 6.0f);
        }
        g.setColour(c.accent);
        g.drawRoundedRectangle(body, radius, 1.5f);
        g.setColour(c.accent.withAlpha(0.35f));
        g.drawRoundedRectangle(body.reduced(1.5f), radius - 1.5f, 1.0f);
    } else {
        g.setColour(c.border);
        g.drawRoundedRectangle(body, radius, m.borderWidth);
    }

    // Glass inner highlight line just inside the top edge.
    if (tr.style == ThemeStyle::Glass) {
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine((int)body.getY() + 1, body.getX() + radius, body.getRight() - radius);
    }
}

void AppLookAndFeel::drawConnectionWire(juce::Graphics& g, juce::Point<float> p1, juce::Point<float> p2,
                                        const juce::Path& path, juce::Colour colour, bool isModulation, float activity,
                                        bool hovered) {
    const auto& m = theme.metrics;
    const auto& tr = theme.treatment;

    // Build the path if the caller didn't.
    juce::Path wire = path;
    if (wire.isEmpty()) {
        const float dx = p2.x - p1.x;
        wire.startNewSubPath(p1);
        wire.cubicTo(p1.x + dx * 0.5f, p1.y, p2.x - dx * 0.5f, p2.y, p2.x, p2.y);
    }

    // `activity` is a raw signal peak supplied by the caller, and not every CV source is normalised
    // (Poly MIDI's pitch fan carries Hz). Clamp before it scales any geometry — an unbounded value
    // turns the stroke into a screen-filling filled region rather than a wire.
    const float normalisedActivity = juce::jlimit(0.0f, 1.0f, activity);
    const float coreWidth = m.wireCoreWidth * (1.0f + normalisedActivity * 0.4f);

    // Casing (dark underlay).
    g.setColour(theme.colors.bg0.withAlpha(0.6f));
    g.strokePath(wire,
                 juce::PathStrokeType(m.wireCasingWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Neon glow behind the core (Glass only, cost-gated).
    if (tr.glow > 0.0f) {
        g.setColour(colour.withAlpha(tr.glow * 0.5f));
        g.strokePath(
            wire, juce::PathStrokeType(coreWidth * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Core. The activity/hover treatment lives in synth::ui::wireCoreColour (CableColour.h) so the
    // theme-aware law — dark themes dim an idle wire toward the canvas, light themes keep the token
    // colour's identity — is pinned by headless tests rather than only visible on screen.
    g.setColour(synth::ui::wireCoreColour(theme.isDark, colour, normalisedActivity, hovered));

    const float effWidth = hovered ? coreWidth + 1.0f : coreWidth;
    juce::PathStrokeType stroke(effWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (isModulation) {
        const float dashes[] = {6.0f, 4.0f};
        juce::Path dashed;
        stroke.createDashedStroke(dashed, wire, dashes, 2);
        g.fillPath(dashed);
    } else {
        g.strokePath(wire, stroke);
    }
}

void AppLookAndFeel::drawModulationRing(juce::Graphics& g, juce::Point<float> centre, float radius, float baseNorm,
                                        float modNorm, bool positive, bool hovered) {
    if (radius <= 0.0f)
        return;

    const auto& m = theme.metrics;
    auto ringColour = positive ? theme.colors.modRingPositive : theme.colors.modRingNegative;
    // FRO288: same brighter(0.3) treatment a hovered cable gets (docs/layout/cables.md#hover) --
    // a wider stroke on top of the brighten so the highlight reads even at a glance.
    float width = m.knobRingWidth;
    if (hovered) {
        ringColour = ringColour.brighter(0.3f);
        width += m.modRingHoverWidthBoost;
    }

    const float baseAngle = modRingAngleForNorm(baseNorm);
    const float modAngle = modRingAngleForNorm(modNorm);

    juce::Path ring;
    ring.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(baseAngle, modAngle),
                       juce::jmax(baseAngle, modAngle), true);

    g.setColour(ringColour);
    g.strokePath(ring, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// FRO287: the reachable-range band drawn UNDER the live ring above -- same arc geometry (shared
// via modRingAngleForNorm so the two can never drift apart), just wider alpha-blended stroke so it
// reads as a track rather than a second live indicator. Visible even at rest (modSignalValue 0),
// which is the whole point: it answers "how far COULD this knob move", not "where is it now".
void AppLookAndFeel::drawModulationDepthBand(juce::Graphics& g, juce::Point<float> centre, float radius,
                                             float startNorm, float endNorm, juce::Colour colour) {
    if (radius <= 0.0f)
        return;

    const auto& m = theme.metrics;
    const float startAngle = modRingAngleForNorm(startNorm);
    const float endAngle = modRingAngleForNorm(endNorm);

    juce::Path band;
    band.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(startAngle, endAngle),
                       juce::jmax(startAngle, endAngle), true);

    g.setColour(colour.withAlpha(m.modDepthBandAlpha));
    g.strokePath(band,
                 juce::PathStrokeType(m.knobRingWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AppLookAndFeel::fillThemedBackground(juce::Graphics& g, juce::Rectangle<float> bounds, bool isCanvas) {
    const auto& c = theme.colors;

    if (!isCanvas) {
        g.fillAll(c.bg0);
        return;
    }

    g.setColour(c.bg1);
    g.fillRect(bounds);

    // Dotted grid: tile a precomputed 22x22 image across only the visible clip bounds.
    static juce::Image tile;
    static juce::Colour tileColour;
    const auto dotColour = c.textMuted.withAlpha(0.06f);
    if (!tile.isValid() || tileColour != dotColour) {
        tile = makeGridTile(dotColour);
        tileColour = dotColour;
    }

    auto clip = g.getClipBounds().toFloat().getIntersection(bounds);
    if (!clip.isEmpty()) {
        g.saveState();
        g.reduceClipRegion(clip.toNearestInt());
        g.setTiledImageFill(tile, 0, 0, 1.0f);
        g.fillRect(clip);
        g.restoreState();
    }
}

} // namespace synth::theme
