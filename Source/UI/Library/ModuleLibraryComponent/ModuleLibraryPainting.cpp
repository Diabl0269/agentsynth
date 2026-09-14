// ModuleLibraryPainting.cpp -- paint(): rows, pinned chrome and the search-highlight /
// chevron / category-icon drawing helpers it calls.
#include "ModuleLibraryComponent.h"

void ModuleLibraryComponent::paint(juce::Graphics& g) {
    // Resolve theme tokens from the active LnF; fall back to plain colors when our LnF
    // isn't installed (e.g. headless tests).
    juce::Colour bgColour = juce::Colours::darkgrey.darker();
    juce::Colour headerColour = juce::Colours::grey;
    juce::Colour itemColour = juce::Colours::white;
    juce::Colour accentColour = juce::Colours::lightblue;
    juce::Colour mutedColour = juce::Colours::grey;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf != nullptr) {
        const auto& c = lf->getTheme().colors;
        bgColour = c.bg0;
        headerColour = c.textMuted;
        itemColour = c.textPrimary;
        accentColour = c.accent;
        mutedColour = c.textMuted;
    }

    g.fillAll(bgColour);

    // Rows stop short of the scrollbar when it is on screen, so text never runs under the thumb.
    const int contentWidth = getRowContentWidth();
    const auto rows = buildRows();
    const juce::String query = normalisedSearchQuery(searchQuery);

    // ---- Rows: clipped below the pinned chrome and shifted by the scroll offset ----
    // The clip is what keeps a scrolled row from painting over the search field or the strip;
    // setOrigin then moves the content-space row.y values into component space.
    {
        juce::Graphics::ScopedSaveState scrolled(g);
        g.reduceClipRegion(0, kPinnedChromeHeight, getWidth(), juce::jmax(0, getHeight() - kPinnedChromeHeight));
        g.setOrigin(0, -scrollOffset);

        if (rows.empty() && isSearchActive()) {
            g.setColour(mutedColour);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText("No matching modules", 20, kPinnedChromeHeight + 12, contentWidth - 40, 24,
                       juce::Justification::centredLeft);
        }

        for (const auto& row : rows) {
            const auto& entry = entries[(size_t)row.entryIndex];

            if (entry.kind == RowKind::Header) {
                // Disclosure chevron drawn as a path — glyph coverage for ▾/▸ is not guaranteed
                // across the embedded typefaces (see the theming font limitation). It rotates on
                // the same progress value as the fold, so the two read as one motion. Search
                // forces matching sections open, so the chevron matches that layout.
                const float chevronProgress = isSearchActive() ? 0.0f : getSectionProgress(entry.text);
                drawChevron(g, juce::Rectangle<float>(8.0f, (float)row.y + 6.0f, 8.0f, 8.0f), chevronProgress,
                            headerColour);

                // Category icon at x=20 (null-guarded — no-op when LnF absent).
                synth::theme::Icon catIcon = categoryIconForHeader(entry.text);
                const juce::Drawable* icon = (lf != nullptr) ? lf->peekIcon(catIcon) : nullptr;

                const juce::Font headerFont(juce::FontOptions(12.0f));
                const juce::String headerLabel = entry.text.toUpperCase();
                if (icon != nullptr) {
                    icon->drawWithin(g, juce::Rectangle<float>(20.0f, (float)row.y + 2.0f, 16.0f, 16.0f),
                                     juce::RectanglePlacement::centred, 1.0f);
                    drawHighlightedText(g, headerLabel, query, {40, row.y, contentWidth - 50, 20}, headerFont,
                                        headerColour, accentColour.withAlpha(0.28f), accentColour);
                } else {
                    drawHighlightedText(g, headerLabel, query, {20, row.y, contentWidth - 30, 20}, headerFont,
                                        headerColour, accentColour.withAlpha(0.28f), accentColour);
                }
                continue;
            }

            // A row mid-fold is truncated, not resized: clip to the visible slice and keep
            // drawing the text at its natural height, so it is cut off rather than squashed or
            // re-centred as the section closes. juce::Graphics::drawText does not clip on its
            // own, hence the explicit region.
            std::optional<juce::Graphics::ScopedSaveState> rowClip;
            if (row.height < kItemHeight) {
                rowClip.emplace(g);
                g.reduceClipRegion(0, row.y, getWidth(), row.height);
            }

            if (entry.kind == RowKind::EmptyHint) {
                const juce::Font hintFont(juce::FontOptions(13.0f));
                drawHighlightedText(g, entry.text, query, {20, row.y, contentWidth - 40, kItemHeight - 4}, hintFont,
                                    mutedColour.withAlpha(0.7f), accentColour.withAlpha(0.28f), accentColour);
                continue;
            }

            // Sub-group label inside a section (e.g. "VST3" / "AudioUnit" under Plugins) — painted
            // in the same muted style as a Header but smaller and indented, so it reads as a
            // sub-level without competing with the section title. Clicking it folds just that
            // format group, via the same chevron-plus-fold affordance as a Header (scaled down
            // since it is a lesser hierarchy level), independently of the section's own fold.
            if (entry.kind == RowKind::SubHeader) {
                const float chevronProgress =
                    isSearchActive() ? 0.0f : getSectionProgress(subsectionKey(entry.section, entry.text));
                drawChevron(g, juce::Rectangle<float>(20.0f, (float)row.y + 8.0f, 6.0f, 6.0f), chevronProgress,
                            mutedColour.withAlpha(0.6f));
                g.setColour(mutedColour.withAlpha(0.6f));
                g.setFont(juce::Font(juce::FontOptions(10.5f)));
                g.drawText(entry.text.toUpperCase(), 32, row.y, contentWidth - 44, kItemHeight - 4,
                           juce::Justification::centredLeft);
                continue;
            }

            // Command row — reads like a hint until hovered, so an empty Plugins section looks
            // like a prompt rather than like a broken module row.
            if (entry.kind == RowKind::Action) {
                const bool hot = row.entryIndex == hoveredIndex;
                if (hot) {
                    g.setColour(accentColour.withAlpha(0.12f));
                    g.fillRect(0, row.y, contentWidth, row.height);
                }
                g.setColour(hot ? accentColour : mutedColour.withAlpha(0.85f));
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText(entry.text, 20, row.y, contentWidth - 40, kItemHeight - 4, juce::Justification::centredLeft);
                continue;
            }

            // Draggable row (module, snippet or plugin).
            const bool enabled = isEntryEnabled(row.entryIndex);

            if (row.entryIndex == hoveredIndex && enabled) {
                g.setColour(accentColour.withAlpha(0.12f));
                g.fillRect(0, row.y, contentWidth, row.height);
            }

            // Greyed out = already in the patch and not addable again.
            const juce::Colour labelColour = enabled ? itemColour : mutedColour.withAlpha(0.5f);
            const juce::Font itemFont(juce::FontOptions(16.0f));
            drawHighlightedText(g, entry.text, query, {20, row.y, contentWidth - 60, kItemHeight - 4}, itemFont,
                                labelColour, accentColour.withAlpha(0.28f), accentColour);

            if (entry.kind == RowKind::Snippet) {
                g.setColour(mutedColour);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawText("(" + juce::String(entry.moduleCount) + ")", contentWidth - 44, row.y, 34, kItemHeight - 4,
                           juce::Justification::centredRight);
            } else if (entry.kind == RowKind::Plugin) {
                // The format tag is load-bearing, not decoration: the same plugin often ships as
                // both VST3 and AU, and the two are different entries with different state.
                g.setColour(mutedColour);
                g.setFont(juce::Font(juce::FontOptions(11.0f)));
                g.drawText(entry.detail, contentWidth - 74, row.y, 64, kItemHeight - 4,
                           juce::Justification::centredRight);
            }
        }

        // T160: keyboard-focus outline — a distinct treatment from the hover fill above (an
        // outline rather than a translucent fill, so the two never read as the same state when a
        // mouse hover and a keyboard focus land on different rows at once). A second pass over
        // `rows` rather than folding into the switch above, so it applies uniformly to every row
        // kind (including Header/SubHeader, which `continue` out of that switch early) without
        // threading an extra branch through each one.
        if (keyboardFocusedIndex >= 0) {
            for (const auto& row : rows) {
                if (row.entryIndex != keyboardFocusedIndex)
                    continue;
                g.setColour(accentColour.withAlpha(0.9f));
                g.drawRect(0, row.y, contentWidth, row.height, 1);
                break;
            }
        }
    }

    // ---- Pinned chrome: the search field is a child TextEditor in the top 32 px; the
    // collapse-all strip (plus the "?" help button sharing its row) is drawn here so scrolled
    // rows cannot show through it. ----
    {
        const bool allCollapsed = areAllSectionsCollapsed();
        g.setColour(bgColour);
        g.fillRect(0, kSearchHeight, getWidth(), kTopStripHeight);

        // "?" help button, left of the COLLAPSE ALL / EXPAND ALL label — paint() and the mouse
        // handlers share getHelpButtonBounds() so the drawn button and the clickable button can
        // never drift apart (the same reason cable paint/hit-test share one enumeration
        // elsewhere in this app — see GraphEditor::buildVisibleCables).
        const auto helpBoundsInt = getHelpButtonBounds();
        const auto helpBoundsF = helpBoundsInt.toFloat();
        if (helpButtonHovered) {
            g.setColour(accentColour.withAlpha(0.16f));
            g.fillEllipse(helpBoundsF);
        }
        g.setColour(helpButtonHovered ? accentColour : mutedColour);
        g.drawEllipse(helpBoundsF.reduced(0.5f), 1.0f);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("?", helpBoundsInt, juce::Justification::centred);

        g.setColour(topStripHovered ? accentColour : mutedColour);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(allCollapsed ? "EXPAND ALL" : "COLLAPSE ALL", 10 + kHelpButtonSize + kHelpButtonMargin,
                   kSearchHeight + 2, contentWidth - 20 - kHelpButtonSize - kHelpButtonMargin, kTopStripHeight - 4,
                   juce::Justification::centredRight);
    }
}

void ModuleLibraryComponent::drawHighlightedText(juce::Graphics& g, const juce::String& text, const juce::String& query,
                                                 juce::Rectangle<int> bounds, const juce::Font& font,
                                                 juce::Colour normal, juce::Colour highlightFill,
                                                 juce::Colour highlightText) {
    g.setFont(font);
    const auto spans = highlightSpansFor(text, query);
    if (spans.empty()) {
        g.setColour(normal);
        g.drawText(text, bounds, juce::Justification::centredLeft, true);
        return;
    }

    const float baseX = (float)bounds.getX();
    for (const auto& span : spans) {
        const float preW = font.getStringWidthFloat(text.substring(0, span.start));
        const float matchW = font.getStringWidthFloat(text.substring(span.start, span.start + span.length));
        g.setColour(highlightFill);
        g.fillRoundedRectangle(baseX + preW - 1.0f, (float)bounds.getY() + 4.0f, matchW + 2.0f,
                               juce::jmax(8.0f, (float)bounds.getHeight() - 8.0f), 2.0f);
    }

    juce::AttributedString as;
    as.setJustification(juce::Justification::centredLeft);
    as.setWordWrap(juce::AttributedString::none);
    int pos = 0;
    for (const auto& span : spans) {
        if (span.start > pos)
            as.append(text.substring(pos, span.start), font, normal);
        as.append(text.substring(span.start, span.start + span.length), font, highlightText);
        pos = span.start + span.length;
    }
    if (pos < text.length())
        as.append(text.substring(pos), font, normal);
    as.draw(g, bounds.toFloat());
}

void ModuleLibraryComponent::drawChevron(juce::Graphics& g, juce::Rectangle<float> area, float progress,
                                         juce::Colour colour) {
    juce::Path p;
    p.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
    p.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi * progress, area.getCentreX(),
                                                     area.getCentreY()));
    g.setColour(colour);
    g.fillPath(p);
}

synth::theme::Icon ModuleLibraryComponent::categoryIconForHeader(const juce::String& header) {
    if (header.equalsIgnoreCase(kSnippetsHeader) || header.equalsIgnoreCase(kPluginsHeader))
        return synth::theme::Icon::CatUtility;
    if (header.equalsIgnoreCase("Sources"))
        return synth::theme::Icon::CatSources;
    if (header.equalsIgnoreCase("Sequencing"))
        return synth::theme::Icon::CatSequencing;
    if (header.startsWithIgnoreCase("Envelopes"))
        return synth::theme::Icon::CatEnvelopes;
    if (header.equalsIgnoreCase("Filters"))
        return synth::theme::Icon::CatFilters;
    if (header.startsWithIgnoreCase("Modulation"))
        return synth::theme::Icon::CatModulationFX;
    if (header.equalsIgnoreCase("Time FX"))
        return synth::theme::Icon::CatTimeFX;
    if (header.equalsIgnoreCase("Dynamics"))
        return synth::theme::Icon::CatDynamics;
    // Audio Input / Audio Output's singleton section — previously fell back to CatUtility,
    // which gave the graph's actual source/sink no visual identity of its own.
    if (header.equalsIgnoreCase("I/O"))
        return synth::theme::Icon::CatIO;
    // "Utility" and any unrecognised headers fall back to CatUtility.
    return synth::theme::Icon::CatUtility;
}
