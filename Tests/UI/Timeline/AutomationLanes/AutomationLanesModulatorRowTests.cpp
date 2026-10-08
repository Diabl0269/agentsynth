// AutomationLanesModulatorRowTests.cpp -- a lane's modulator rows against a stub host: where they land in the
// shared row layout (under their lane, counted in the track's extra height), what a click at each y hits, the
// row's controls (names, Tab stops, the edit phases a drag sends), its amount readout and the band's knob drag,
// and the read-only row of a non-LFO source.
// The MainComponent side -- adding, removing and undoing real modulators -- is in
// AutomationLanesModulatorMainTests.cpp.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
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

// Answers every lane with the same routings (an Attenuverter's own amount lane with none), and records what the
// rows write.
struct ModulatorHost : synth::ui::TrackHeaderHost {
    std::vector<ModulatorInfo> modulators;
    std::map<juce::String, float> values;
    std::vector<ParameterEditPhase> phases;
    bool modulatable = true;

    std::vector<ModulatorInfo> getModulators(const juce::String& uuid, const juce::String&) override {
        return uuid.startsWith("atten-") ? std::vector<ModulatorInfo>{} : modulators;
    }
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
    const int addRowHeight = 0; // the "+" button sits in the last lane header's gutter
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
    // An LFO's band takes the click (it edits the routing's amount), and only inside its own row: the clip lanes refuse
    // the extra area, and a y just outside the band falls through to the panel as before.
    EXPECT_EQ(f.componentAt({x, firstMod.getCentreY()}), band);
    EXPECT_NE(f.componentAt({x, secondMod.getBottom() + 1}), band);
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
    EXPECT_EQ(row->getSyncToggle().getTitle(), "Cutoff LFO sync");
    EXPECT_EQ(row->getShapeIcon().getTitle(), "Sine shape");
    EXPECT_EQ(row->getShapeIcon().getTooltip(), "Sine shape");
    for (juce::Component* c : std::initializer_list<juce::Component*>{&row->getShapeCombo(), &row->getSyncRateCombo(),
                                                                      &row->getSyncToggle()})
        EXPECT_TRUE(c->getWantsKeyboardFocus()) << c->getTitle();
    EXPECT_TRUE(row->getSyncRateCombo().isVisible()) << "synced: the 1/4-style rate";
    EXPECT_FALSE(row->getRateSlider().isVisible());
    EXPECT_EQ(row->getAmountText(), "+50%") << "the amount reads as a signed percentage";

    // The amount is the band's: a real downward drag on its flat line is Begin, then Change per move, then End.
    auto* band = f.panel.modulatorBandForTest(cutoff, 0);
    ASSERT_NE(band, nullptr);
    EXPECT_EQ(band->getTitle(), "Cutoff LFO 1 amount");
    EXPECT_TRUE(band->getWantsKeyboardFocus());
    EXPECT_TRUE(band->getTooltip().startsWith("Drag to set how much LFO 1 moves Cutoff; draw to change it over time"));
    const auto mid = band->getLocalBounds().getCentre().toFloat();
    dragAcross(*band, mid, mid.translated(0.0f, (float)band->getHeight() / 4.0f), 4);
    ASSERT_GE(h.host.phases.size(), 3u);
    EXPECT_EQ(h.host.phases.front(), ParameterEditPhase::Begin);
    EXPECT_EQ(h.host.phases.back(), ParameterEditPhase::End);
    for (size_t i = 1; i + 1 < h.host.phases.size(); ++i)
        EXPECT_EQ(h.host.phases[i], ParameterEditPhase::Change);
    EXPECT_NEAR(h.host.values["atten-1.amount"], 0.0f, 0.03f) << "a quarter of the band's height is 50% down";

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

TEST(AutomationLanesModulatorRowTest, ANonLfoSourceGetsAReadOnlyRowWithItsTitleAndAmountOnly) {
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
    EXPECT_EQ(row->getAmountText(), "0%");
    auto* band = f.panel.modulatorBandForTest(cutoff, 0);
    ASSERT_NE(band, nullptr);
    EXPECT_TRUE(band->isEditable()) << "its amount is edited on the band, like an LFO's";
    EXPECT_EQ(band->getTitle(), "Cutoff Filter Env amount");
    EXPECT_FALSE(row->getShapeCombo().isVisible());
    EXPECT_FALSE(row->getSyncToggle().isVisible());
    EXPECT_FALSE(row->getShapeIcon().isVisible()) << "a source that is not an LFO has no shape";
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
            if (it.getItem().itemID == synth::ui::AutomationLaneHeaderComponent::kAddModulatorMenuId)
                return &it.getItem();
        return nullptr;
    };
    auto menu = header->buildMenu();
    const auto* item = findAdd(menu);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled);
    EXPECT_EQ(item->text, "Add modulator...");

    h.host.modulatable = false;
    menu = header->buildMenu();
    item = findAdd(menu);
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    EXPECT_EQ(item->text, "Add modulator... (no CV input)") << "the reason is in the item itself";
}

// In the app's default ~190 px header column the first layout clipped the shape and rate combos to
// "..." and drew a value under a fader's cap. Every control must sit inside the row,
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
    EXPECT_LT(row.getSyncToggle().getRight(), inside.getRight() - 60) << "room beside Sync for the amount";
    row.setLookAndFeel(nullptr);
}

// The readout is the amount the routing plays: a signed percentage, a real minus sign below zero, and the
// lane's value at the playhead once an amount lane exists. Tag, stripe and readout take the track's colour.
TEST(AutomationLanesModulatorRowTest, TheRowReadsTheAmountSignedAndFromTheLaneAtThePlayhead) {
    HostedLanes h;
    h.host.modulators = {lfoInfo("1")};
    h.host.values["atten-1.amount"] = -0.3f;
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->getAmountText(), juce::String::fromUTF8("\xE2\x88\x92") + "30%");

    h.host.values["atten-1.amount"] = 0.0f;
    f.panel.updateFromTransport(synth::TransportService::PositionSnapshot{}, 0.0);
    EXPECT_EQ(row->getAmountText(), "0%");

    // An amount lane wins over the knob: 72% from beat 4.
    constexpr int kHold = static_cast<int>(synth::BreakpointCurve::Hold);
    f.undo.recordTimelineChange(f.doc, [&] {
        synth::ui::writeAmountLane(f.doc, bass, "atten-1", {{0.0, 0.0, 0.0f, kHold}, {4.0, 0.72, 0.0f, kHold}});
    });
    synth::TransportService::PositionSnapshot at;
    at.ppq = 6.0;
    f.panel.updateFromTransport(at, 0.0);
    row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->getAmountText(), "+72%");
    EXPECT_EQ(f.panel.laneHeaderForTest(f.doc.getLaneForParam("atten-1", "amount")->id), nullptr)
        << "the amount lane is the band, not a lane row";
}

// A direct cable has no Attenuverter and so no amount: no readout, and its band is a plain decoration that takes
// no clicks and is not a Tab stop.
TEST(AutomationLanesModulatorRowTest, ADirectCableHasNoAmountAndADecorationBand) {
    HostedLanes h;
    auto direct = lfoInfo("1");
    direct.attenuverterUuid = {};
    h.host.modulators = {direct};
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    auto* band = f.panel.modulatorBandForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    ASSERT_NE(band, nullptr);
    EXPECT_TRUE(row->getAmountText().isEmpty());
    EXPECT_FALSE(band->isEditable());
    EXPECT_EQ(band->getEditor(), nullptr);
    EXPECT_FALSE(band->getWantsKeyboardFocus());
    bool clicks = true, childClicks = true;
    band->getInterceptsMouseClicks(clicks, childClicks);
    EXPECT_FALSE(clicks);
    EXPECT_FALSE(band->isAccessible());
}

namespace {
// What the icon paints at its own size: every pixel, so two shapes can be told apart.
juce::Image renderShape(synth::ui::ModulatorShapeIcon& icon) {
    juce::Image image(juce::Image::ARGB, 40, 24, true, juce::SoftwareImageType());
    icon.setBounds(0, 0, 40, 24);
    juce::Graphics g(image);
    icon.paintEntireComponent(g, true);
    return image;
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}
} // namespace

TEST(AutomationLanesModulatorRowTest, RightClickReturnAndShiftF10OnAModulatorRowOpenItsMenu) {
    HostedLanes h;
    h.host.modulators = {lfoInfo("1")};
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    lane_menu_test::MenuCapture capture;

    const auto click = makeClickEvent(*row, {60.0f, 4.0f}, lane_menu_test::rightButton());
    row->mouseDown(click);
    ASSERT_EQ(capture.count, 1);
    const auto fromRightClick = capture.itemTexts();
    EXPECT_TRUE(fromRightClick.contains("Show on canvas"));
    EXPECT_TRUE(fromRightClick.contains("Remove modulator"));

    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(capture.count, 2);
    EXPECT_EQ(capture.itemTexts(), fromRightClick) << "Return opens the same menu";

    EXPECT_TRUE(synth::ui::openContextMenuForFocusedComponent(row)) << "what Shift+F10 resolves to";
    EXPECT_EQ(capture.count, 3);
    EXPECT_EQ(capture.itemTexts(), fromRightClick);

    // A right-click on the shape picture is a right-click on the row.
    auto& icon = row->getShapeIcon();
    row->mouseDown(makeClickEvent(icon, {5.0f, 5.0f}, lane_menu_test::rightButton()).getEventRelativeTo(row));
    EXPECT_EQ(capture.count, 4);

    // No "..." button is left on the row.
    for (auto* child : row->getChildren())
        EXPECT_FALSE(dynamic_cast<juce::Button*>(child) != nullptr && child->getTitle().containsIgnoreCase("menu"))
            << child->getTitle();
}

TEST(AutomationLanesModulatorRowTest, TheShapePictureFollowsTheShapeAndIsNamedForIt) {
    HostedLanes h;
    h.host.modulators = {lfoInfo("1")};
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    auto& icon = row->getShapeIcon();
    EXPECT_TRUE(icon.isVisible());
    EXPECT_FALSE(icon.getWantsKeyboardFocus()) << "the combo beside it is the control";

    const juce::StringArray names{"Sine", "Triangle", "Sawtooth", "Square", "Sample and hold", "Custom"};
    std::vector<juce::Image> pictures;
    for (int shape = 0; shape < names.size(); ++shape) {
        row->getShapeCombo().setSelectedId(shape + 1, juce::sendNotificationSync); // the user's pick
        EXPECT_EQ(icon.getShape(), shape);
        EXPECT_EQ(icon.getTitle(), names[shape] + " shape");
        EXPECT_EQ(icon.getTooltip(), names[shape] + " shape");
        EXPECT_EQ(icon.getGlyph(), synth::ui::ModulatorShapeIcon::glyphForShape(shape));
        pictures.push_back(renderShape(icon));
    }
    for (size_t a = 0; a < pictures.size(); ++a)
        for (size_t b = a + 1; b < pictures.size(); ++b)
            EXPECT_FALSE(sameImage(pictures[a], pictures[b])) << "shapes " << a << " and " << b << " look the same";

    // The graph changing the shape under the row (undo, the card, a preset) moves the picture too.
    h.host.values["lfo-1.shape"] = 3.0f;
    row->refreshValues();
    EXPECT_EQ(icon.getTitle(), "Square shape");
}

TEST(AutomationLanesModulatorRowTest, ANewModulatorRowAndItsBandFadeInTogether) {
    FadeAnimateGuard guard;
    HostedLanes h;
    auto& f = h.f;
    const auto bass = f.doc.addTrack(TrackKind::Midi, "Bass");
    const auto cutoff = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    f.panel.refreshModulators();
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0), nullptr);

    h.host.modulators = {lfoInfo("1")}; // an LFO is patched into the lane
    f.panel.refreshModulators();
    auto* row = f.panel.modulatorRowForTest(cutoff, 0);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->isVisible());
    EXPECT_EQ(row->getAlpha(), 0.0f) << "the row fades in";
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(row->getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_EQ(row->getAlpha(), 1.0f);

    h.host.modulators = {lfoInfo("1"), lfoInfo("2")}; // a second one: only the new routing fades in
    f.panel.refreshModulators();
    auto* second = f.panel.modulatorRowForTest(cutoff, 1);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->getAlpha(), 0.0f);
    EXPECT_EQ(f.panel.modulatorRowForTest(cutoff, 0)->getAlpha(), 1.0f) << "a routing that was there stays whole";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
}
