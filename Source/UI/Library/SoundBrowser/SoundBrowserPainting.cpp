// SoundBrowserPainting.cpp -- paint(): the path bar, the painted rows (and the slide or fade between two lists), the
// footer. Every colour comes from the theme's tokens.
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/SearchMatch.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kSideMargin = 12;
constexpr int kChevronGap = 14;
constexpr int kStatusWidth = 78;

void strokeChevron(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
    juce::Path p;
    p.startNewSubPath(area.getX(), area.getY());
    p.lineTo(area.getRight(), area.getCentreY());
    p.lineTo(area.getX(), area.getBottom());
    g.setColour(colour);
    g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

void SoundBrowserComponent::layoutPathParts() {
    pathParts_ = getPathParts();
    if (showsFlatResults())
        return;
    const juce::Font font(juce::FontOptions(12.0f));
    const int available = juce::jmax(40, getWidth() - 2 * kSideMargin - kStatusWidth);
    const int n = (int)pathParts_.size();
    const int maxLabel = juce::jmax(36, (available - (n - 1) * kChevronGap) / juce::jmax(1, n));
    int x = kSideMargin;
    for (auto& part : pathParts_) {
        const int w = juce::jmin(maxLabel, (int)std::ceil(font.getStringWidthFloat(part.label)) + 2);
        part.bounds = {x, getListTop() - kPathHeight, w, kPathHeight};
        x += w + kChevronGap;
    }
}

void SoundBrowserComponent::paintPathBar(juce::Graphics& g, juce::Colour muted, juce::Colour primary,
                                         juce::Colour accent) {
    const juce::Font font(juce::FontOptions(12.0f));
    g.setFont(font);
    const auto snap = index_.snapshot();
    const int pathTop = getListTop() - kPathHeight;
    if (showsFlatResults()) {
        const int count = (int)rows_.size() - ((rows_.size() == 1 && rows_[0].kind == RowKind::Hint) ? 1 : 0);
        g.setColour(muted);
        g.drawText(juce::String(count) + (count == 1 ? " sound" : " sounds") + " in all places",
                   juce::Rectangle<int>(kSideMargin, pathTop, getWidth() - 2 * kSideMargin, kPathHeight),
                   juce::Justification::centredLeft, true);
    } else {
        for (int i = 0; i < (int)pathParts_.size(); ++i) {
            const auto& part = pathParts_[(size_t)i];
            const bool last = i == (int)pathParts_.size() - 1;
            const bool hot = i == hoveredPart_ && !last;
            g.setColour(hot ? accent : (last ? primary : muted));
            g.drawText(part.label, part.bounds, juce::Justification::centredLeft, true);
            if (!last)
                strokeChevron(
                    g, {(float)part.bounds.getRight() + 4.0f, (float)part.bounds.getCentreY() - 3.5f, 4.0f, 7.0f},
                    muted.withAlpha(0.7f));
        }
    }
    juce::String status;
    if (snap->scanning)
        status = "Scanning";
    else if (snap->truncated)
        status = "First 50,000";
    if (status.isNotEmpty()) {
        g.setColour(muted.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(status, getWidth() - kStatusWidth - 6, pathTop, kStatusWidth, kPathHeight,
                   juce::Justification::centredRight);
    }
}

void SoundBrowserComponent::paintRow(juce::Graphics& g, const Row& row, int index, juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const int width = area.getWidth();
    if (row.kind == RowKind::Hint) {
        g.setColour(c.textMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText(row.text, area.withTrimmedLeft(kSideMargin).withTrimmedRight(kSideMargin),
                   juce::Justification::centredLeft, true);
        return;
    }
    if (index == selected_) {
        g.setColour(c.accent.withAlpha(0.16f));
        g.fillRect(area);
    } else if (index == hovered_) {
        g.setColour(c.accent.withAlpha(0.10f));
        g.fillRect(area);
    }

    const juce::Font nameFont(juce::FontOptions(14.0f));
    const juce::Font detailFont(juce::FontOptions(12.0f));
    if (row.kind == RowKind::Sound) {
        const float detailW = detailFont.getStringWidthFloat(row.detail);
        const int detailRight = width - kSideMargin;
        const int detailLeft = detailRight - (int)std::ceil(detailW);
        const bool favourite = row.entry.has_value() && lists_.isFavourite(row.entry->path);
        int nameRight = detailLeft - 8;
        if (favourite) {
            juce::Path star;
            star.addStar({(float)detailLeft - 12.0f, (float)area.getCentreY()}, 5, 2.6f, 5.4f);
            g.setColour(c.accent);
            g.fillPath(star);
            nameRight -= 14;
        }
        const auto query = isSearchActive() ? query_.trim() : juce::String();
        drawSearchHighlightedText(
            g, row.text, query,
            {area.getX() + kSideMargin, area.getY(), juce::jmax(0, nameRight - kSideMargin), area.getHeight()},
            nameFont, c.textPrimary, c.accent.withAlpha(0.28f), c.accent);
        g.setColour(c.textMuted);
        g.setFont(detailFont);
        g.drawText(row.detail, detailLeft, area.getY(), detailRight - detailLeft, area.getHeight(),
                   juce::Justification::centredRight);
    } else {
        strokeChevron(g, {(float)width - kSideMargin - 5.0f, (float)area.getCentreY() - 4.0f, 5.0f, 8.0f}, c.textMuted);
        const int countRight = width - kSideMargin - 14;
        g.setColour(c.textMuted);
        g.setFont(detailFont);
        g.drawText(row.detail, countRight - 50, area.getY(), 50, area.getHeight(), juce::Justification::centredRight);
        g.setColour(c.textPrimary);
        g.setFont(nameFont);
        g.drawText(row.text, area.getX() + kSideMargin, area.getY(), juce::jmax(0, countRight - 56 - kSideMargin),
                   area.getHeight(), juce::Justification::centredLeft, true);
    }
    if (index == selected_ && hasKeyboardFocus(false)) {
        g.setColour(c.accent.withAlpha(0.9f));
        g.drawRect(area, 1);
    }
}

// Paints the rows with the list's top-left at the origin; the caller clips and positions.
void SoundBrowserComponent::paintRows(juce::Graphics& g, int width) const {
    const int viewport = listArea().getHeight();
    const int first = juce::jmax(0, scrollOffset_ / kRowHeight);
    for (int i = first; i < (int)rows_.size(); ++i) {
        const int y = i * kRowHeight - scrollOffset_;
        if (y >= viewport)
            break;
        paintRow(g, rows_[(size_t)i], i, {0, y, width, kRowHeight});
    }
}

void SoundBrowserComponent::paintFooter(juce::Graphics& g) {
    if (footerMode_ == FooterMode::Hidden)
        return;
    const auto& c = synth::theme::themeOf(*this).colors;
    const int height = footerHeight();
    const juce::Rectangle<int> whole(0, getHeight() - height, getWidth(), height);
    g.setColour(c.surface);
    g.fillRect(whole);
    g.setColour(c.border);
    g.fillRect(whole.withHeight(1));
    if (footerMode_ == FooterMode::Compact) {
        paintCompactName(g);
        return;
    }

    const auto area = whole.withHeight(kFooterInfoHeight);
    const auto inner = area.reduced(kSideMargin, 8);
    const auto nameArea = inner.withHeight(18);
    const auto subArea = inner.withTrimmedTop(20).withHeight(15);
    if (selected_ < 0 || selected_ >= (int)rows_.size() || !rows_[(size_t)selected_].navigable()) {
        g.setColour(c.textMuted);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText("Select a sound", nameArea, juce::Justification::centredLeft);
        return;
    }
    const auto& row = rows_[(size_t)selected_];
    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(row.text, nameArea, juce::Justification::centredLeft, true);
    g.setColour(c.textMuted);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    juce::String sub;
    if (row.kind == RowKind::Sound) {
        sub = row.entry->placeName;
        if (row.entry->folder.isNotEmpty())
            sub += " > " + row.entry->folder.replace("/", " > ");
    } else {
        sub = juce::String(row.soundCount) + (row.soundCount == 1 ? " sound" : " sounds");
    }
    g.drawText(sub, subArea, juce::Justification::centredLeft, true);
}

// The compact footer has room for the name only, ellipsised, between the Stop button and the volume.
void SoundBrowserComponent::paintCompactName(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto area = footer_.getCompactNameBounds().translated(footer_.getX(), footer_.getY());
    const bool have = selected_ >= 0 && selected_ < (int)rows_.size() && rows_[(size_t)selected_].navigable();
    g.setColour(have ? c.textPrimary : c.textMuted);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(have ? rows_[(size_t)selected_].text : juce::String("Select a sound"), area,
               juce::Justification::centredLeft, true);
}

void SoundBrowserComponent::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    g.fillAll(c.bg0);
    paintPathBar(g, c.textMuted, c.textPrimary, c.accent);

    const auto area = listArea();
    {
        juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(area);
        const int width = contentWidth();
        const bool moving = listMotion_ < 1.0f && !previousList_.isNull();
        const bool slide = moving && motionDirection_ != 0 && !prefersReducedMotion();
        if (!moving) {
            juce::Graphics::ScopedSaveState rows(g);
            g.setOrigin(area.getPosition());
            paintRows(g, width);
        } else if (slide) {
            const int w = area.getWidth();
            const int oldX = (int)std::lround(-(float)motionDirection_ * listMotion_ * (float)w);
            const int newX = (int)std::lround((float)motionDirection_ * (1.0f - listMotion_) * (float)w);
            g.drawImageAt(previousList_, area.getX() + oldX, area.getY());
            juce::Graphics::ScopedSaveState rows(g);
            g.setOrigin(area.getX() + newX, area.getY());
            paintRows(g, width);
        } else {
            g.setOpacity(1.0f - listMotion_);
            g.drawImageAt(previousList_, area.getX(), area.getY());
            g.beginTransparencyLayer(listMotion_);
            {
                juce::Graphics::ScopedSaveState rows(g);
                g.setOrigin(area.getPosition());
                paintRows(g, width);
            }
            g.endTransparencyLayer();
        }
    }
    paintFooter(g);
}

} // namespace synth::ui
