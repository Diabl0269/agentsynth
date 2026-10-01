// AutomationLanesModulatorRowTests.cpp -- a lane's modulator rows against a stub host: where they land in the
// shared row layout (under their lane, counted in the track's extra height), what a click at each y hits, the
// row's controls (names, Tab stops, the edit phases a drag sends) and the read-only row of a non-LFO source.
// The MainComponent side -- adding, removing and undoing real modulators -- is in
// AutomationLanesModulatorMainTests.cpp.

#include "AutomationLanesTestFixture.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <map>

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::ModulatorInfo;
using synth::ui::ParameterEditPhase;

namespace {

ModulatorInfo lfoInfo(const juce::String& n) {
    ModulatorInfo info;
    info.sourceUuid = "lfo-" + n;
    info.sourceTitle = "LFO " + n;
    info.isLfo = true;
    info.attenuverterUuid = "atten-" + n;
    info.paramId = "cutoff";
    info.targetChannel = 2;
    return info;
}

// Answers every lane with the same routings, and records what the rows write.
struct ModulatorHost : synth::ui::TrackHeaderHost {
    std::vector<ModulatorInfo> modulators;
    std::map<juce::String, float> values;
    std::vector<ParameterEditPhase> phases;
    bool modulatable = true;

    std::vector<ModulatorInfo> getModulators(const juce::String&, const juce::String&) override { return modulators; }
    bool canModulate(const juce::String&, const juce::String&) override { return modulatable; }
    float getNodeParameter(const juce::String& uuid, const juce::String& id) override {
        return values[uuid + "." + id];
    }
    void setNodeParameter(const juce::String& uuid, const juce::String& id, float v, ParameterEditPhase p) override {
        values[uuid + "." + id] = v;
        phases.push_back(p);
    }
    juce::String getParameterDisplayName(const juce::String&, const juce::String&) override { return "Cutoff"; }
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return "Filter"; }
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
};

// The host outlives the panel (it is declared first and so destroyed last).
struct HostedLanes {
    ModulatorHost host;
    LanesPanel f;
    HostedLanes() { f.panel.setTrackHeaderHost(&host); }
    ~HostedLanes() { f.panel.setTrackHeaderHost(nullptr); }
};

} // namespace

TEST(AutomationLanesModulatorRowTest, ModulatorRowsSitUnderTheirLaneAndCountInTheTracksExtraHeight) {
    HostedLanes h;
    h.host.modulators = {lfoInfo("1"), lfoInfo("2")};
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto lead = f.doc.addTrack(TrackKind::Midi, "Lead");
    const auto cutoff = f.addLane(bass, "cutoff");
    const auto res = f.addLane(bass, "resonance");
    f.panel.setTrackAutomationExpanded(bass, true);

    const int laneHeight = 40;
    const int modHeight = synth::ui::ModulatorRow::kBaseHeight;
    const int addRowHeight = 24;
    const auto layout = f.panel.getClipLaneArea().getRowLayout();
    EXPECT_EQ(layout.trackExtraHeight(0), 2 * (laneHeight + 2 * modHeight) + addRowHeight);

    const auto cutoffRow = f.panel.laneRowBoundsForTest(cutoff);
    const auto firstMod = f.panel.modulatorRowBoundsForTest(cutoff, 0);
    const auto secondMod = f.panel.modulatorRowBoundsForTest(cutoff, 1);
    EXPECT_EQ(firstMod.getY(), cutoffRow.getBottom()) << "directly under its lane";
    EXPECT_EQ(firstMod.getHeight(), modHeight);
    EXPECT_EQ(secondMod.getY(), firstMod.getBottom());
    EXPECT_EQ(f.panel.laneRowBoundsForTest(res).getY(), secondMod.getBottom()) << "the next lane follows";

    // The header-column half sits at the same y as the band over the lanes region.
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    auto* band = f.panel.modulatorBandForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    ASSERT_NE(band, nullptr);
    const int lanesTop = f.panel.getClipLaneArea().getY();
    EXPECT_EQ(row->getY(), firstMod.getY() - lanesTop) << "header column content y matches the lanes region's";
    EXPECT_EQ(f.panel.getLocalArea(band, band->getLocalBounds()), firstMod);

    // A click at Lead's y still hits Lead's row, below the modulator rows.
    const int x = f.panel.getClipLaneArea().getX() + 200;
    // The band takes no click and the clip lanes refuse the extra area, so nothing under it does.
    EXPECT_EQ(f.componentAt({x, firstMod.getCentreY()}), &f.panel);
    const int leadY = lanesTop + layout.trackTop(1) + layout.trackRowHeight(1) / 2;
    EXPECT_EQ(f.componentAt({x, leadY}), &f.panel.getClipLaneArea());
    auto& clips = f.panel.getClipLaneArea();
    const auto inClips = clips.getLocalPoint(&f.panel, juce::Point<int>(x, leadY)).toFloat();
    clips.mouseDoubleClick(makeClickEvent(clips, inClips, leftButton()));
    EXPECT_EQ(f.doc.getTrack(lead)->clips.size(), 1u) << "the clip lands on Lead, not on Bass";
    EXPECT_TRUE(f.doc.getTrack(bass)->clips.empty());
}

TEST(AutomationLanesModulatorRowTest, RefreshAddsAndDropsRowsWhenTheGraphChangesAndKeepsAnUnchangedRow) {
    HostedLanes h;
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0), nullptr);
    const int extraBefore = f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0);

    h.host.modulators = {lfoInfo("1")};
    f.panel.refreshModulators();
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0),
              extraBefore + synth::ui::ModulatorRow::kBaseHeight);

    h.host.modulators[0].sourceTitle = "Wobble";
    f.panel.refreshModulators();
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0), row) << "the same routing keeps its row (and focus)";

    h.host.modulators.clear();
    f.panel.refreshModulators();
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0), nullptr);
    EXPECT_EQ(f.panel.getClipLaneArea().getRowLayout().trackExtraHeight(0), extraBefore);

    // Folded, a track shows none.
    h.host.modulators = {lfoInfo("1")};
    f.panel.setTrackAutomationExpanded(bass, false);
    f.panel.refreshModulators();
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0), nullptr);
}

TEST(AutomationLanesModulatorRowTest, EveryControlIsANamedTabStopAndADragIsOneGesture) {
    HostedLanes h;
    h.host.modulators = {lfoInfo("1")};
    h.host.values["lfo-1.mode"] = 1.0f;
    h.host.values["atten-1.amount"] = 0.5f;
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);

    EXPECT_EQ(row->getShapeCombo().getTitle(), "Cutoff LFO shape");
    EXPECT_EQ(row->getShapeCombo().getTooltip(), "Cutoff LFO shape");
    EXPECT_EQ(row->getDepthSlider().getTitle(), "Cutoff LFO depth");
    EXPECT_EQ(row->getSyncToggle().getTitle(), "Cutoff LFO sync");
    EXPECT_EQ(row->getMenuButton().getTooltip(), "Modulator menu for Cutoff LFO");
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&row->getShapeCombo(), &row->getSyncRateCombo(), &row->getSyncToggle(),
                                                 &row->getDepthSlider(), &row->getMenuButton()})
        EXPECT_TRUE(c->getWantsKeyboardFocus()) << c->getTitle();
    EXPECT_TRUE(row->getSyncRateCombo().isVisible()) << "synced: the 1/4-style rate";
    EXPECT_FALSE(row->getRateSlider().isVisible());
    EXPECT_DOUBLE_EQ(row->getDepthSlider().getValue(), 50.0) << "depth reads as a percentage";
    EXPECT_EQ(row->getDepthSlider().getTextFromValue(50.0), "50%");

    // A real drag across the depth bar: Begin, then Change per move, then End.
    auto& depth = row->getDepthSlider();
    const auto mid = depth.getLocalBounds().getCentre().toFloat();
    dragAcross(depth, mid, mid.translated(-(float)depth.getWidth() / 4.0f, 0.0f), 4);
    ASSERT_GE(h.host.phases.size(), 3u);
    EXPECT_EQ(h.host.phases.front(), ParameterEditPhase::Begin);
    EXPECT_EQ(h.host.phases.back(), ParameterEditPhase::End);
    for (size_t i = 1; i + 1 < h.host.phases.size(); ++i)
        EXPECT_EQ(h.host.phases[i], ParameterEditPhase::Change);
    EXPECT_LT(h.host.values["atten-1.amount"], 0.5f);

    // A toggle click is one complete edit, and swaps the rate control.
    h.host.phases.clear();
    clickButton(row->getSyncToggle());
    ASSERT_EQ(h.host.phases.size(), 1u);
    EXPECT_EQ(h.host.phases.front(), ParameterEditPhase::Once);
    EXPECT_FLOAT_EQ(h.host.values["lfo-1.mode"], 0.0f);
    EXPECT_TRUE(row->getRateSlider().isVisible()) << "free-running: the Hz bar";
    EXPECT_FALSE(row->getSyncRateCombo().isVisible());

    // A value changed elsewhere (the canvas card) reaches the row on the next refresh.
    h.host.values["lfo-1.shape"] = 3.0f;
    row->refreshValues();
    EXPECT_EQ(row->getShapeCombo().getSelectedId(), 4);
}

TEST(AutomationLanesModulatorRowTest, ANonLfoSourceGetsAReadOnlyRowWithItsTitleAndDepthOnly) {
    HostedLanes h;
    ModulatorInfo env;
    env.sourceUuid = "env";
    env.sourceTitle = "Filter Env";
    env.attenuverterUuid = "atten-env";
    h.host.modulators = {env};
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->getDepthSlider().isVisible());
    EXPECT_EQ(row->getDepthSlider().getTitle(), "Cutoff Filter Env depth");
    EXPECT_FALSE(row->getShapeCombo().isVisible());
    EXPECT_FALSE(row->getSyncToggle().isVisible());
    EXPECT_FALSE(row->getMenuButton().isVisible()) << "nothing to remove or retune from here";
}

TEST(AutomationLanesModulatorRowTest, TheLaneMenuOffersAnLfoModulatorOnlyWhenTheParameterHasACvJack) {
    HostedLanes h;
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* header = f.panel.laneHeaderForTest(cutoff);
    ASSERT_NE(header, nullptr);

    const auto findAdd = [](const juce::PopupMenu& menu) -> const juce::PopupMenu::Item* {
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            if (it.getItem().itemID == synth::ui::AutomationLaneHeaderComponent::kAddLfoModulatorMenuId)
                return &it.getItem();
        return nullptr;
    };
    auto menu = header->buildMenu();
    const auto* item = findAdd(menu);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    EXPECT_EQ(item->text, "Add LFO modulator");

    h.host.modulatable = false;
    menu = header->buildMenu();
    item = findAdd(menu);
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    EXPECT_EQ(item->text, "Add LFO modulator (no CV input)") << "the reason is in the item itself";
}

// In the app's default ~190 px header column the first layout clipped the shape and rate combos to
// "..." and drew the depth percentage under the fader's cap. Every control must sit inside the row,
// the combos must be as wide as their longest choice, and no bar may cover its value text.
TEST(AutomationLanesModulatorRowTest, EveryControlFitsTheDefaultHeaderColumnWithoutClipping) {
    synth::theme::AppLookAndFeel lookAndFeel;
    synth::ui::ModulatorRow row(lfoInfo("1"), nullptr, "Cutoff");
    row.setLookAndFeel(&lookAndFeel);
    row.setBounds(0, 0, synth::theme::Metrics{}.timelineTrackHeaderWidth, synth::ui::ModulatorRow::kBaseHeight);

    const auto inside = row.getLocalBounds();
    for (auto* child : row.getChildren())
        if (child->isVisible())
            EXPECT_TRUE(inside.contains(child->getBounds())) << child->getTitle() << " leaves the row";
    EXPECT_GE(row.getShapeCombo().getWidth(),
              synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(row.getShapeCombo()) - 2)
        << "\"Sawtooth\" shows in full";
    EXPECT_GE(row.getSyncRateCombo().getWidth(),
              synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(row.getSyncRateCombo()) - 2);
    EXPECT_FALSE(row.getShapeCombo().getBounds().intersects(row.getSyncRateCombo().getBounds()));
    EXPECT_GT(row.getDepthSlider().getWidth(), 30) << "a depth bar you can still drag";
    EXPECT_LT(row.getDepthSlider().getRight(), inside.getRight() - 30) << "room beside it for the percentage";
    row.setLookAndFeel(nullptr);
}
