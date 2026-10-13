// AutomationLanesLaneHeaderTests.cpp -- the lane header's marker icon and name button, on an automation lane and on a
// mod lane (a modulator row): a click anywhere that is not a control focuses the row and opens nothing, a click on the
// name opens the lane's picker (Change parameter / Change source), Return and Space press the focused name button, the
// marker's tooltip follows "Show info tooltips", and the mod row lines up with the lane header (same inset, normal
// surface). The picker's own rows are in AutomationLanesChangeSourceTests.cpp; "New LFO" / "New envelope" against a
// real MainComponent is at the end of this file.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AutomationLanesAmountFixture.h"
#include "AutomationLanesMenuFixture.h"
#include "Modules/ADSRModule.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Theme/Theme.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

using namespace amount_test;
using namespace lane_menu_test;
using synth::ui::AutomationLaneHeaderComponent;
using synth::ui::LaneMarkerIcon;
using synth::ui::ModulatorInfo;
using synth::ui::ModulatorRow;

namespace {

// A host that lets a row re-point its routing and records the picks.
struct RowHost : synth::ui::TrackHeaderHost {
    bool canChange = true;
    int pickedNew = 0;
    juce::String pickedType;

    bool canChangeModulatorSource(const ModulatorInfo&) override { return canChange; }
    bool changeModulatorSourceToNew(const ModulatorInfo&, const juce::String& typeName, int) override {
        ++pickedNew;
        pickedType = typeName;
        return true;
    }
    std::vector<LfoChoice> getChangeSourceChoices(const juce::String&, const juce::String&) override {
        LfoChoice current;
        current.uuid = "lfo-1";
        current.name = "LFO 1";
        return {current};
    }
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

ModulatorInfo lfoRouting() {
    ModulatorInfo info;
    info.sourceUuid = "lfo-1";
    info.sourceTitle = "LFO 1";
    info.isLfo = true;
    info.attenuverterUuid = "atten-1";
    info.targetUuid = "filter";
    info.paramId = "cutoff";
    return info;
}

// A row at the lane header's own size, laid out.
struct RowFixture {
    RowHost host;
    ModulatorRow row;
    explicit RowFixture(ModulatorInfo info = lfoRouting())
        : row(info, &host, "Cutoff") {
        row.setBounds(0, 0, 200, ModulatorRow::kBaseHeight);
    }
    ~RowFixture() { synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr; }
    juce::Point<float> nameCentre() { return row.getNameAreaForTest().getCentre().toFloat(); }
    juce::Point<float> emptySpot() { return {(float)ModulatorRow::kIndent + 2.0f, (float)row.getHeight() - 4.0f}; }
    void click(juce::Point<float> at) {
        row.mouseDown(makeClickEvent(row, at, leftButton()));
        row.mouseUp(makeClickEvent(row, at, leftButton()));
    }
};

struct SourcePicker {
    std::unique_ptr<synth::ui::ModMatrixPicker> picker;
    SourcePicker() {
        synth::ui::test_hooks::changeSourcePickerHookForTest() = [this](auto p) { picker = std::move(p); };
    }
    ~SourcePicker() { synth::ui::test_hooks::changeSourcePickerHookForTest() = nullptr; }
};

juce::KeyPress plain(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }
} // namespace

// ---- Automation lane header ----

TEST(AutomationLanesLaneHeaderTest, AClickAwayFromTheNameFocusesTheHeaderAndOpensNoPickerOrMenu) {
    MenuPanel f;
    PickerCapture picker;
    MenuCapture menu;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    const juce::Point<float> away{(float)AutomationLaneHeaderComponent::kIndent + 2.0f, 3.0f};
    ASSERT_FALSE(header->getNameAreaForTest().contains(away.roundToInt()));

    header->mouseDown(makeClickEvent(*header, away, leftButton()));
    EXPECT_EQ(header->getClickFocusRequestsForTest(), 0) << "focus waits for the release: a press may become a drag";
    header->mouseUp(makeClickEvent(*header, away, leftButton()));

    EXPECT_EQ(header->getClickFocusRequestsForTest(), 1);
    EXPECT_EQ(picker.picker, nullptr);
    EXPECT_EQ(menu.count, 0);
}

TEST(AutomationLanesLaneHeaderTest, AClickOnTheMarkerFocusesTheHeaderToo) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    auto& marker = header->getMarkerIcon();
    // The marker hands its events to the header, with positions relative to the marker.
    const auto centre = marker.getLocalBounds().getCentre().toFloat();
    marker.mouseDown(makeClickEvent(marker, centre, leftButton()));
    header->mouseDown(makeClickEvent(*header, marker.getBounds().getCentre().toFloat(), leftButton()));
    header->mouseUp(makeClickEvent(*header, marker.getBounds().getCentre().toFloat(), leftButton()));
    EXPECT_EQ(header->getClickFocusRequestsForTest(), 1);
}

TEST(AutomationLanesLaneHeaderTest, TheNameIsAFocusableButtonWithTheActionAsItsNameAndTooltip) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    auto& button = header->getNameButton();
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(button.getTitle(), "Change parameter of cutoff");
    EXPECT_EQ(button.getTooltip(), "Pick a different parameter");
    EXPECT_FALSE(button.getBounds().isEmpty());
    EXPECT_EQ(button.getBounds(), header->getNameAreaForTest());
}

TEST(AutomationLanesLaneHeaderTest, ReturnAndSpaceOnTheFocusedNameButtonOpenTheParameterPicker) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    for (const int code : {(int)juce::KeyPress::returnKey, (int)juce::KeyPress::spaceKey}) {
        PickerCapture capture;
        MenuCapture menu;
        EXPECT_TRUE(header->getNameButton().keyPressed(plain(code)));
        EXPECT_NE(capture.picker, nullptr) << "key " << code;
        EXPECT_EQ(menu.count, 0) << "the lane menu is the header's Return, not the name button's";
    }
}

TEST(AutomationLanesLaneHeaderTest, AClickOnTheNameOpensThePickerAndDoesNotFocusTheRow) {
    MenuPanel f;
    PickerCapture capture;
    auto* header = f.header(f.lane);
    const auto at = header->getNameAreaForTest().getCentre().toFloat();
    header->mouseDown(makeClickEvent(*header, at, leftButton()));
    header->mouseUp(makeClickEvent(*header, at, leftButton()));
    EXPECT_NE(capture.picker, nullptr);
    EXPECT_EQ(header->getClickFocusRequestsForTest(), 0);
}

TEST(AutomationLanesLaneHeaderTest, TheNameButtonLightsOnHoverAndIsNotATooltipTargetOfItsOwn) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    const auto inside = header->getNameAreaForTest().getCentre().toFloat();
    header->mouseMove(makeClickEvent(*header, inside, leftButton()));
    EXPECT_TRUE(header->getNameButton().isHovered());
    header->mouseExit(makeClickEvent(*header, inside, leftButton()));
    EXPECT_FALSE(header->getNameButton().isHovered());
}

TEST(AutomationLanesLaneHeaderTest, TheMarkerHasTheAutomationTooltipAndIsNeitherATabStopNorAnAccessibleElement) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    auto& marker = header->getMarkerIcon();
    EXPECT_EQ(marker.getTooltip(), "Automation lane: drawn changes over time");
    EXPECT_EQ(marker.getKind(), LaneMarkerIcon::Kind::Automation);
    EXPECT_FALSE(marker.getWantsKeyboardFocus());
    EXPECT_FALSE(marker.isAccessible());
    EXPECT_EQ(marker.getWidth(), 14);
    EXPECT_EQ(marker.getHeight(), 14);
    EXPECT_EQ(header->getTitle(), "cutoff automation lane") << "the meaning is in the header's own name";
    EXPECT_LT(marker.getRight(), header->getNameAreaForTest().getX());
}

TEST(AutomationLanesLaneHeaderTest, TheMarkerTooltipFollowsTheInfoTooltipsSettingOnBothLaneKinds) {
    juce::ApplicationProperties properties;
    juce::PropertiesFile::Options options;
    options.applicationName = "LaneHeaderMarkerTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    properties.setStorageParameters(options);
    properties.getUserSettings()->clear();
    juce::Component parent;
    parent.setSize(400, 300);
    synth::ui::AppTooltipWindow window(&parent, &properties);

    MenuPanel f;
    RowFixture mod;
    auto& automationMarker = f.header(f.lane)->getMarkerIcon();
    auto& modMarker = mod.row.getMarkerIcon();
    EXPECT_FALSE(window.suppresses(automationMarker));
    EXPECT_FALSE(window.suppresses(modMarker));

    properties.getUserSettings()->setValue(synth::ui::kShowInfoTooltipsKey, false);
    EXPECT_TRUE(window.suppresses(automationMarker)) << "Info tooltips off: the icon shows no tooltip";
    EXPECT_TRUE(window.suppresses(modMarker));
    properties.getUserSettings()->clear();
}

// ---- Mod lane (modulator row) ----

TEST(AutomationLanesLaneHeaderTest, AModRowsMarkerSaysWhichKindOfSourceMovesTheParameter) {
    RowFixture lfo;
    EXPECT_EQ(lfo.row.getMarkerIcon().getTooltip(), "Mod lane: an LFO moves this parameter");
    EXPECT_EQ(lfo.row.getMarkerIcon().getKind(), LaneMarkerIcon::Kind::Mod);

    auto env = lfoRouting();
    env.isLfo = false;
    env.isEnvelope = true;
    env.sourceTitle = "Envelope 1";
    RowFixture envelope(env);
    EXPECT_EQ(envelope.row.getMarkerIcon().getTooltip(), "Mod lane: an envelope moves this parameter");
    EXPECT_EQ(lfo.row.getTitle(), "LFO 1 mod lane on Cutoff");
    EXPECT_FALSE(lfo.row.getMarkerIcon().isAccessible());
    EXPECT_FALSE(lfo.row.getMarkerIcon().getWantsKeyboardFocus());
}

TEST(AutomationLanesLaneHeaderTest, AClickOnAModRowAwayFromTheNameFocusesItAndOpensNothing) {
    RowFixture f;
    SourcePicker picker;
    MenuCapture menu;
    f.click(f.emptySpot());
    EXPECT_EQ(f.row.getClickFocusRequestsForTest(), 1);
    EXPECT_EQ(picker.picker, nullptr);
    EXPECT_EQ(menu.count, 0);
}

TEST(AutomationLanesLaneHeaderTest, AClickOnAModRowsNameOpensTheSearchableChangeSourcePicker) {
    RowFixture f;
    SourcePicker picker;
    f.click(f.nameCentre());
    ASSERT_NE(picker.picker, nullptr);
    EXPECT_EQ(f.row.getClickFocusRequestsForTest(), 0);
    EXPECT_EQ(picker.picker->getSearchPlaceholderForTest(), "Search or create a source");
    EXPECT_EQ(picker.picker->getVisibleRowNamesForTest(),
              (std::vector<juce::String>{"LFOs", "LFO 1", "New", "New LFO", "New envelope"}));
}

TEST(AutomationLanesLaneHeaderTest, ADragStartedOnTheNameNeitherOpensThePickerNorFocuses) {
    RowFixture f;
    SourcePicker picker;
    f.row.onDragMove = [](int) { return true; }; // the pool says the pointer is really dragging
    const auto at = f.nameCentre();
    f.row.mouseDown(makeClickEvent(f.row, at, leftButton()));
    f.row.mouseDrag(makeDragEvent(f.row, at + juce::Point<float>(0.0f, 30.0f), at, leftButton()));
    f.row.mouseUp(makeDragEvent(f.row, at + juce::Point<float>(0.0f, 30.0f), at, leftButton()));
    EXPECT_EQ(picker.picker, nullptr);
    EXPECT_EQ(f.row.getClickFocusRequestsForTest(), 0);
}

TEST(AutomationLanesLaneHeaderTest, TheModRowsNameIsAButtonThatReturnAndSpaceOpenTheSamePicker) {
    RowFixture f;
    auto& button = f.row.getNameButton();
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_EQ(button.getTitle(), "Change source of LFO 1 on Cutoff");
    EXPECT_EQ(button.getTooltip(), "Pick a different source");
    for (const int code : {(int)juce::KeyPress::returnKey, (int)juce::KeyPress::spaceKey}) {
        SourcePicker picker;
        EXPECT_TRUE(button.keyPressed(plain(code)));
        EXPECT_NE(picker.picker, nullptr) << "key " << code;
    }
}

TEST(AutomationLanesLaneHeaderTest, AModRowWhoseSourceCannotBeSwappedHasADisabledNameButton) {
    RowFixture f;
    f.host.canChange = false;
    f.row.setInfo(lfoRouting(), "Cutoff");
    SourcePicker picker;
    EXPECT_FALSE(f.row.getNameButton().isEnabled());
    EXPECT_FALSE(f.row.getNameButton().getTooltip().isEmpty());
    f.click(f.nameCentre());
    EXPECT_EQ(picker.picker, nullptr);
}

TEST(AutomationLanesLaneHeaderTest, TheModRowsMarkerAndNameLineUpWithTheAutomationHeaders) {
    MenuPanel f;
    auto* header = f.header(f.lane);
    ASSERT_NE(header, nullptr);
    RowFixture mod;
    EXPECT_EQ(mod.row.getMarkerIcon().getX(), header->getMarkerIcon().getX());
    EXPECT_EQ(mod.row.getNameAreaForTest().getX(), header->getNameAreaForTest().getX());
    EXPECT_EQ(ModulatorRow::kIndent, AutomationLaneHeaderComponent::kIndent);
}

TEST(AutomationLanesLaneHeaderTest, TheModRowsBackgroundIsTheNormalSurfaceNotATint) {
    RowFixture f;
    juce::Image image(juce::Image::ARGB, f.row.getWidth(), f.row.getHeight(), true);
    {
        juce::Graphics g(image);
        f.row.paintEntireComponent(g, true);
    }
    const auto surface = synth::theme::themeOf(f.row).colors.surface;
    EXPECT_EQ(image.getPixelAt(4, f.row.getHeight() - 5), surface) << "left gutter, below the arrow";
    EXPECT_EQ(image.getPixelAt(f.row.getWidth() - 3, f.row.getHeight() / 2), surface);
}

// ---- A real MainComponent: New LFO / New envelope ----

TEST_F(TimelinePanelIntegrationTest, NewLfoFromTheNamePickerReplacesTheSourceKeepsTheAmountLaneAndIsOneUndoStep) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    const std::vector<synth::AutomationLane::Breakpoint> drawn{{0.0, 0.25, 0.0f, kHold}, {4.0, -0.5, 0.0f, kHold}};
    s.undo().recordTimelineChange(s.doc(), [&] { synth::ui::writeAmountLane(s.doc(), s.track, s.attenUuid, drawn); });
    const auto laneId = s.amountLane()->id;
    const auto oldLfo = s.lfoUuid;
    const auto oldAtten = s.attenUuid;
    const auto depthBefore = s.depthOf(s.chainsInto(oldLfo, raw).front());
    const size_t lfosBefore = s.nodesOf<LFOModule>().size();

    SourcePicker picker;
    auto* row = s.row();
    ASSERT_NE(row, nullptr);
    row->mouseDown(makeClickEvent(*row, row->getNameAreaForTest().getCentre().toFloat(), leftButton()));
    row->mouseUp(makeClickEvent(*row, row->getNameAreaForTest().getCentre().toFloat(), leftButton()));
    ASSERT_NE(picker.picker, nullptr);
    const auto names = picker.picker->getVisibleItemTextsForTest();
    ASSERT_GE(names.size(), 2u);
    ASSERT_EQ(names[names.size() - 2], "New LFO");
    picker.picker->chooseVisibleItemForTest((int)names.size() - 2);
    s.flush();

    EXPECT_EQ(s.nodesOf<LFOModule>().size(), lfosBefore + 1) << "a fresh LFO card";
    EXPECT_TRUE(s.chainsInto(oldLfo, raw).empty()) << "the routing left the old source";
    ASSERT_NE(s.row(), nullptr);
    const auto newLfo = s.row()->getInfo().sourceUuid;
    EXPECT_NE(newLfo, oldLfo);
    const auto chains = s.chainsInto(newLfo, raw);
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_FLOAT_EQ(s.depthOf(chains.front()), depthBefore) << "the new routing keeps the old depth";
    EXPECT_NE(s.byUuid(oldLfo), nullptr) << "the old card stays";
    const auto* after = synth::ui::amountLaneFor(s.doc(), s.row()->getInfo().attenuverterUuid);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->id, laneId) << "the amount lane is the same lane, re-keyed";
    EXPECT_EQ(after->points.size(), drawn.size());
    EXPECT_EQ(synth::ui::amountLaneFor(s.doc(), oldAtten), nullptr);

    ASSERT_TRUE(s.undo().undo()) << "one step takes back the module, the cable and the lane key";
    EXPECT_EQ(s.nodesOf<LFOModule>().size(), lfosBefore);
    EXPECT_EQ(s.chainsInto(oldLfo, raw).size(), 1u);
    ASSERT_NE(s.row(), nullptr);
    EXPECT_EQ(s.row()->getInfo().sourceUuid, oldLfo);
    const auto* restored = synth::ui::amountLaneFor(s.doc(), s.row()->getInfo().attenuverterUuid);
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->points.size(), drawn.size());
}

TEST_F(TimelinePanelIntegrationTest, NewEnvelopeFromTheNamePickerCreatesAnEnvelopeReadFromTheCataloguesChannel) {
    AmountScene s;
    const int raw = s.channelFor("cutoff");
    const auto oldLfo = s.lfoUuid;
    SourcePicker picker;
    s.row()->getNameButton().keyPressed(plain(juce::KeyPress::returnKey));
    ASSERT_NE(picker.picker, nullptr);
    const auto names = picker.picker->getVisibleItemTextsForTest();
    ASSERT_EQ(names.back(), "New envelope");
    const size_t adsrBefore = s.nodesOf<ADSRModule>().size();
    picker.picker->chooseVisibleItemForTest((int)names.size() - 1);
    s.flush();

    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore + 1);
    EXPECT_TRUE(s.chainsInto(oldLfo, raw).empty());
    ASSERT_NE(s.row(), nullptr);
    EXPECT_FALSE(s.row()->getInfo().isLfo);
    EXPECT_TRUE(s.row()->getInfo().isEnvelope);
    EXPECT_EQ(s.row()->getMarkerIcon().getTooltip(), "Mod lane: an envelope moves this parameter");
    ASSERT_EQ(s.chainsInto(s.row()->getInfo().sourceUuid, raw).size(), 1u);

    ASSERT_TRUE(s.undo().undo());
    EXPECT_EQ(s.nodesOf<ADSRModule>().size(), adsrBefore);
    EXPECT_EQ(s.chainsInto(oldLfo, raw).size(), 1u);
}
