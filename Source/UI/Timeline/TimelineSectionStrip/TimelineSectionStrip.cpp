// TimelineSectionStrip.cpp
//
// The Sections strip's state, block list, painting and the edge lines the panel draws through the lanes. The
// pointer gestures, menus, rename editor and accessibility are the sibling TimelineSectionStrip*.cpp files;
// TimelineSectionStrip is declared in TimelineSectionStrip.h.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/SectionVariationLetter.h"
#include "Transport/TransportService.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/SectionColours.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include "UI/Timeline/TimelineSectionStrip/SectionStripSettings.h"
#include <map>

namespace synth::ui {

namespace {
constexpr float kBlockFontHeight = 11.0f;
constexpr float kCountFontHeight = 10.0f;
constexpr float kBlockFillAlpha = 1.0f;
constexpr float kEdgeLineAlpha = 0.40f; // the same as the marker stems the panel draws through the lanes
constexpr float kEdgeLineWidth = 2.0f;
constexpr float kHintAlpha = 0.85f;
constexpr float kMovedOriginalAlpha = 0.35f;
constexpr double kFallbackBpm = 120.0;
const char* const kHintText = "Drag across bars to make a section";
} // namespace

//==============================================================================

void TimelineSectionStrip::setTimelineDoc(synth::TimelineDoc* doc) {
    finishRename(false); // an in-flight rename names a placement in the OLD doc
    endMakeDrag(false);
    pressedBlock_ = {};
    blockDragging_ = false;
    motion_.stopAll();
    motionPrimed_ = false;
    selected_ = {};
    hovered_ = {};
    doc_ = doc;
    refresh();
}

void TimelineSectionStrip::refresh() {
    if (doc_ != nullptr && selected_.isValid() && doc_->getPlacement(selected_) == nullptr)
        selected_ = {};
    if (doc_ != nullptr && hovered_.isValid() && doc_->getPlacement(hovered_) == nullptr)
        hovered_ = {};
    if (renamingPlacement_.isValid() && (doc_ == nullptr || doc_->getPlacement(renamingPlacement_) == nullptr))
        finishRename(false);
    trackChangesForMotion();
    rebuildProxies();
    repaint();
}

//==============================================================================
double TimelineSectionStrip::beatsPerBar() const { return beatsPerBarFor(transport_); }

double TimelineSectionStrip::secondsPerBeat() const {
    double bpm = kFallbackBpm;
    if (transport_ != nullptr) {
        const double live = transport_->getPositionSnapshot().bpm;
        if (std::isfinite(live) && live > 0.0)
            bpm = live;
    }
    return 60.0 / bpm;
}

void TimelineSectionStrip::performEdit(const std::function<void()>& mutation) {
    if (doc_ == nullptr || !mutation)
        return;
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutation);
    else
        mutation();
}

synth::SectionDropMode TimelineSectionStrip::dropMode(bool flip) const {
    const bool cover = sectionsCoverOnDrop(propertiesFile_) != flip;
    return cover ? synth::SectionDropMode::Cover : synth::SectionDropMode::MakeRoom;
}

//==============================================================================
std::vector<TimelineSectionStrip::Block> TimelineSectionStrip::buildBlocks() const {
    std::vector<Block> blocks;
    if (doc_ == nullptr || getWidth() <= 0 || getHeight() <= 0)
        return blocks;

    // Counts and ordinals across EVERY placement (an off-screen copy still counts as one of the "3").
    std::map<std::int64_t, int> total;
    for (const auto& placement : doc_->getPlacements())
        ++total[placement.section.value];
    std::map<std::int64_t, int> seen;

    const double widthPx = (double)getWidth();
    for (const auto& placement : doc_->getPlacements()) {
        const int ordinal = ++seen[placement.section.value];
        const auto* section = doc_->getSection(placement.section);
        if (section == nullptr)
            continue;
        const double x0 = xAtBeat(placement.startBeat);
        const double x1 = xAtBeat(placement.startBeat + section->lengthBeats);
        if (x1 < 0.0 || x0 > widthPx)
            continue;
        Block block;
        block.id = placement.id;
        block.section = placement.section;
        block.name = section->name;
        block.colour = juce::Colour(section->colourArgb);
        block.startBeat = placement.startBeat;
        block.lengthBeats = section->lengthBeats;
        block.ordinal = ordinal;
        block.count = total[placement.section.value];
        block.variation = placement.variation;
        block.variationCount = (int)section->variations.size();
        block.bounds = sectionstrip::blockRect((float)x0, (float)x1, getHeight());
        blocks.push_back(std::move(block));
    }
    return blocks;
}

synth::PlacementId TimelineSectionStrip::blockAt(juce::Point<float> pos) const {
    const auto blocks = buildBlocks();
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
        if (it->bounds.contains(pos))
            return it->id;
    return {};
}

synth::PlacementId TimelineSectionStrip::chipAt(juce::Point<float> pos) const {
    const auto blocks = buildBlocks();
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
        if (it->bounds.contains(pos))
            return sectionstrip::chipBounds(it->bounds).contains(pos) ? it->id : synth::PlacementId{};
    return {};
}

void TimelineSectionStrip::selectPlacement(synth::PlacementId id) {
    if (id == selected_)
        return;
    selected_ = id;
    repaint();
}

//==============================================================================
void TimelineSectionStrip::resized() {
    if (renameEditor_ != nullptr)
        beginRenameSection(renamingPlacement_);
    layoutProxies();
}

// One block: the section's colour, a darker border, its name, and "2/3" when it plays more than once. `alpha` scales
// all of it (a growing, moved or leaving block).
void TimelineSectionStrip::paintBlock(juce::Graphics& g, const Block& block, juce::Rectangle<float> rect, float alpha,
                                      const juce::Font& nameFont, const juce::Font& countFont, bool chipHot) const {
    g.setColour(block.colour.withAlpha(kBlockFillAlpha * alpha));
    g.fillRect(rect);
    g.setColour(block.colour.darker(0.5f).withAlpha(alpha));
    g.drawRect(rect, 1.0f);

    auto text = rect.reduced(sectionstrip::kBlockPadX, 0.0f);
    if (text.getWidth() <= 4.0f)
        return;
    const auto ink = juce::Colours::black; // fixed black, like a clip's name: it sits on a section hue
    // The variation chip: a pill at the right end holding the letter, on the block's colour darkened.
    const auto chip = block.id.isValid() ? sectionstrip::chipBounds(rect) : juce::Rectangle<float>();
    if (!chip.isEmpty()) {
        g.setColour(block.colour.darker(chipHot ? 0.1f : 0.25f).withAlpha(alpha));
        g.fillRoundedRectangle(chip, chip.getWidth() * 0.5f);
        g.setColour(ink.withAlpha(alpha));
        g.setFont(synth::theme::AppLookAndFeel::uiSemiBoldFont(kCountFontHeight));
        g.drawText(synth::variationLetter(block.variation), chip.toNearestInt(), juce::Justification::centred, false);
        text.setRight(chip.getX() - 2.0f);
        if (text.getWidth() <= 4.0f)
            return;
    }
    if (block.count > 1 && text.getWidth() > sectionstrip::kCountReserve * 1.6f) {
        g.setColour(ink.withAlpha(0.75f * alpha));
        g.setFont(countFont);
        const auto countArea = text.removeFromRight(sectionstrip::kCountReserve);
        g.drawText(juce::String(block.ordinal) + "/" + juce::String(block.count), countArea.toNearestInt(),
                   juce::Justification::centredRight, false);
    }
    g.setColour(ink.withAlpha(alpha));
    g.setFont(nameFont);
    g.drawText(block.name, text.toNearestInt(), juce::Justification::centredLeft, true);
}

void TimelineSectionStrip::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto bounds = getLocalBounds();
    g.setColour(colors.surface);
    g.fillRect(bounds);
    g.setColour(colors.border);
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());
    if (getWidth() <= 0)
        return;

    layoutProxies(); // a scroll or zoom moved the blocks; the proxies follow, once

    const auto blocks = buildBlocks();
    if (doc_ != nullptr && doc_->getSections().empty() && !makeDragging_) {
        g.setColour(colors.textMuted.withAlpha(kHintAlpha));
        g.setFont(juce::Font(juce::FontOptions(kBlockFontHeight)));
        g.drawText(kHintText, getLocalBounds().withTrimmedLeft(8), juce::Justification::centredLeft, true);
    }

    const auto nameFont = synth::theme::AppLookAndFeel::uiSemiBoldFont(kBlockFontHeight);
    const juce::Font countFont(
        juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kCountFontHeight, juce::Font::plain));
    for (const auto& block : blocks) {
        auto rect = block.bounds;
        float alpha = motion_.growAlpha(block.id);
        const float scale = motion_.growScale(block.id);
        if (scale < 1.0f) {
            const float width = rect.getWidth() * scale;
            rect = motion_.growsFromRight() ? rect.withLeft(rect.getRight() - width) : rect.withWidth(width);
        }
        if (blockDragging_ && !dragCopy_ && block.id == pressedBlock_)
            alpha *= kMovedOriginalAlpha; // the block being moved stays as a faint mark of where it was
        paintBlock(g, block, rect, alpha, nameFont, countFont, hoveredChip_ && block.id == hovered_);
        if (block.id == selected_) {
            g.setColour(colors.accent);
            g.drawRect(rect.expanded(1.0f), 2.0f);
            paintFocusRing(g, rect.expanded(3.0f), *this);
        }
        if (motion_.isOutlined(block.id)) {
            g.setColour(colors.accent.withAlpha(motion_.outlineAlpha()));
            g.drawRect(rect, 1.0f);
        }
    }

    if (motion_.hasExitGhost()) {
        const auto& ghost = motion_.exitGhost();
        const float scale = motion_.exitScale();
        const auto rect =
            ghost.bounds.withSizeKeepingCentre(ghost.bounds.getWidth() * scale, ghost.bounds.getHeight() * scale);
        Block shell;
        shell.name = ghost.name;
        shell.colour = ghost.colour;
        paintBlock(g, shell, rect, motion_.exitAlpha(), nameFont, countFont);
    }

    if (makeDragging_) {
        const float x0 = (float)xAtBeat(makeDragSpan_.start);
        const float x1 = (float)xAtBeat(makeDragSpan_.end);
        const auto outline = sectionstrip::blockRect(x0, x1, getHeight());
        g.setColour(colors.accent.withAlpha(0.14f));
        g.fillRect(outline);
        g.setColour(colors.accent);
        const float dash[] = {4.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(outline.getTopLeft(), outline.getTopRight()), dash, 2, 1.5f);
        g.drawDashedLine(juce::Line<float>(outline.getBottomLeft(), outline.getBottomRight()), dash, 2, 1.5f);
        g.drawDashedLine(juce::Line<float>(outline.getTopLeft(), outline.getBottomLeft()), dash, 2, 1.5f);
        g.drawDashedLine(juce::Line<float>(outline.getTopRight(), outline.getBottomRight()), dash, 2, 1.5f);
    }
    paintBlockDrag(g);
}

// A thin vertical line in each section's colour at every placement start, through the lanes. The panel calls this
// from its own paint(), next to the marker stems; `lanes` is the lanes rect in the panel's coordinates.
void TimelineSectionStrip::paintLaneEdges(juce::Graphics& g, juce::Rectangle<int> lanes) const {
    if (doc_ == nullptr || lanes.isEmpty() || viewState_.pixelsPerBeat <= 0.0)
        return;
    const double widthPx = (double)lanes.getWidth();
    for (const auto& placement : doc_->getPlacements()) {
        const auto* section = doc_->getSection(placement.section);
        if (section == nullptr)
            continue;
        const double x = viewState_.beatToX(placement.startBeat);
        if (x < 0.0 || x > widthPx)
            continue;
        g.setColour(juce::Colour(section->colourArgb).withAlpha(kEdgeLineAlpha));
        g.fillRect((float)(lanes.getX() + (int)std::llround(x)), (float)lanes.getY(), kEdgeLineWidth,
                   (float)lanes.getHeight());
    }
}

} // namespace synth::ui
