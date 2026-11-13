// TimelineTrackHeaderRowFitTests.cpp -- the layout-overlap guard for the track header row: every child of the header
// stays inside the header and no two visible children overlap, at every row height a user can reach (the zoomed-out
// shortest row to the tallest) and at the narrowest and widest header column, in every combination of bottom-row
// controls (voice toggle, channel chip, show-module, drum machine) and for every track kind. A fixed-height child in a
// resizable row fails here by name.
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackChannelLinkSurface.h"
#include <gtest/gtest.h>
#include <set>

using synth::TimelineDoc;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackPolyState;

namespace {

struct LinkSurface : synth::ui::TrackChannelLinkSurface {
    ChannelInfo getChannelInfo(synth::TrackId) const override {
        ChannelInfo info;
        info.hasChannel = true;
        info.channelName = "Channel 1";
        return info;
    }
    float getChannelMeterPeak(synth::TrackId) const override { return 0.0f; }
    bool renameLinkedTrackAndChannel(synth::TrackId, const juce::String&) override { return false; }
    std::unique_ptr<synth::ui::ColourPickerPopup> buildOwnedMacroColourPicker(synth::TrackId,
                                                                              juce::PropertiesFile*) override {
        return nullptr;
    }
    bool toggleLinkedChannelMuted(synth::TrackId) override { return false; }
    bool toggleLinkedChannelSoloed(synth::TrackId) override { return false; }
    void revealChannelForTrack(synth::TrackId) override {}
};

// A host that shows every optional bottom-row control a track can have.
struct EverythingHost : synth::ui::TrackHeaderHost {
    LinkSurface link;
    bool withVoice = true;
    bool withChannel = true;
    bool withDrum = true;
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Track In"; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    TrackPolyState getTrackPolyState(synth::TrackId) override {
        return withVoice ? TrackPolyState::poly : TrackPolyState::none;
    }
    bool drumKitRows(synth::TrackId, synth::ui::KitRows*) const override { return withDrum; }
    synth::ui::TrackChannelLinkSurface* getChannelLinkSurface() override { return withChannel ? &link : nullptr; }
};

// The row heights a user can reach: the shared zoom (0.5 to 3, here 0.5 / 1 / 2) times a row's own height scale (0.5
// to 4), from a 14 px header to a very tall one, in steps.
std::set<int> reachableHeights() {
    std::set<int> heights;
    for (const double zoom : {0.5, 1.0, 2.0})
        for (const double own : {0.5, 0.625, 0.75, 1.0, 1.25, 1.5625, 2.0, 4.0})
            heights.insert(juce::roundToInt(TimelineTrackHeaderComponent::kRowHeight * zoom * own));
    for (int h = 14; h <= 40; ++h) // every pixel in the range where the two rows get too short for their controls
        heights.insert(h);
    return heights;
}

// Empty when the header is laid out cleanly, else what is wrong.
juce::String layoutProblems(TimelineTrackHeaderComponent& header) {
    juce::String problems;
    std::vector<juce::Component*> shown;
    for (auto* child : header.getChildren())
        // The height handle is a thin strip laid over the row's bottom edge on purpose.
        if (child->isVisible() && !child->getBounds().isEmpty() && child->getComponentID() != "trackHeightHandle")
            shown.push_back(child);
    for (auto* child : shown)
        if (!header.getLocalBounds().contains(child->getBounds()))
            problems << child->getComponentID() << " leaves the header; ";
    for (size_t i = 0; i < shown.size(); ++i)
        for (size_t j = i + 1; j < shown.size(); ++j)
            if (shown[i]->getBounds().intersects(shown[j]->getBounds()))
                problems << shown[i]->getComponentID() << " overlaps " << shown[j]->getComponentID() << "; ";
    return problems;
}

} // namespace

TEST(TimelineTrackHeaderRowFit, EveryChildStaysInsideTheRowAndNothingOverlapsAtEveryHeightAndWidth) {
    const std::vector<std::pair<TrackKind, juce::String>> kinds = {
        {TrackKind::Midi, "Lead"}, {TrackKind::Audio, "Vocal"}, {TrackKind::Automation, "Unassigned"}};
    for (const auto& [kind, name] : kinds)
        for (const bool everything : {true, false}) {
            TimelineDoc doc;
            const auto id = doc.addTrack(kind, name);
            EverythingHost host;
            host.withVoice = host.withChannel = host.withDrum = everything;
            TimelineTrackHeaderComponent header(doc, id, &host);
            for (const int width : {synth::ui::TimelinePanelComponent::kMinTrackHeaderWidth, 240,
                                    synth::ui::TimelinePanelComponent::kMaxTrackHeaderWidth})
                for (const int height : reachableHeights()) {
                    header.setSize(width, height);
                    header.refreshFromDoc();
                    EXPECT_EQ(layoutProblems(header), juce::String())
                        << name << (everything ? " with every control" : "") << " at " << width << " x " << height;
                }
        }
}

TEST(TimelineTrackHeaderRowFit, TheVoiceToggleKeepsItsSquareAndShrinksOnlyWithTheRow) {
    TimelineDoc doc;
    const auto id = doc.addTrack(TrackKind::Midi, "Lead");
    EverythingHost host;
    host.withDrum = false;
    TimelineTrackHeaderComponent header(doc, id, &host);
    header.setSize(240, TimelineTrackHeaderComponent::kRowHeight);
    header.refreshFromDoc();
    auto& toggle = header.getVoiceToggle();
    ASSERT_TRUE(toggle.isVisible());
    EXPECT_EQ(toggle.getWidth(), toggle.getHeight());
    EXPECT_EQ(toggle.getHeight(), synth::ui::TrackShowModuleButton::kSize);

    header.setSize(240, 18);
    EXPECT_LE(toggle.getHeight(), header.getHeight());
    EXPECT_EQ(toggle.getWidth(), synth::ui::TrackShowModuleButton::kSize) << "the width is the icon's, not the row's";
}

// Regression test for FRO941: the Mono | Poly text switch was 54 px wide in a row that could not hold it, so "Mono" was
// cut off. Every header part with fixed words (the M/S/R letters, any other text button) must show them whole at the
// narrowest header width, for every track kind and every control set. Parts that show user text (the name, the
// binding chip, the channel chip) shrink and elide on purpose, so they must keep the full text in a tooltip instead.
TEST(TimelineTrackHeaderRowFit, FixedWordsAreWholeAndElidedTextKeepsATooltipAtTheNarrowestHeaderWidth) {
    const std::vector<std::pair<TrackKind, juce::String>> kinds = {
        {TrackKind::Midi, "Lead"}, {TrackKind::Audio, "Vocal"}, {TrackKind::Automation, "Unassigned"}};
    for (const auto& [kind, name] : kinds)
        for (const bool everything : {true, false}) {
            TimelineDoc doc;
            const auto id = doc.addTrack(kind, name);
            EverythingHost host;
            host.withVoice = host.withChannel = host.withDrum = everything;
            TimelineTrackHeaderComponent header(doc, id, &host);
            header.setSize(synth::ui::TimelinePanelComponent::kMinTrackHeaderWidth,
                           TimelineTrackHeaderComponent::kRowHeight);
            header.refreshFromDoc();
            const juce::String where = name + (everything ? " with every control" : "");
            for (auto* child : header.getChildren()) {
                if (!child->isVisible() || child->getBounds().isEmpty())
                    continue;
                const auto id = child->getComponentID();
                const bool elides = id == "trackNameLabel" || id == "trackBindingChip" || id == "trackChannelChip";
                if (elides) {
                    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child);
                    EXPECT_TRUE((tip != nullptr && tip->getTooltip().isNotEmpty()) || header.getTooltip().isNotEmpty())
                        << where << ": " << id << " elides its text and has no tooltip";
                    continue;
                }
                auto* button = dynamic_cast<juce::TextButton*>(child);
                if (button == nullptr || button->getButtonText().isEmpty())
                    continue;
                const auto font = button->getLookAndFeel().getTextButtonFont(*button, button->getHeight());
                juce::GlyphArrangement glyphs;
                glyphs.addLineOfText(font, button->getButtonText(), 0.0f, 0.0f);
                EXPECT_LE(glyphs.getBoundingBox(0, -1, true).getWidth(), static_cast<float>(button->getWidth()) - 4.0f)
                    << where << ": '" << button->getButtonText() << "' in " << id << " is clipped";
            }
        }
}
