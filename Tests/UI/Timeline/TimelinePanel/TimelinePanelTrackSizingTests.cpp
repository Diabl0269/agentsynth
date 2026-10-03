// TimelinePanelTrackSizingTests.cpp -- sizing the timeline's rows and columns by hand: one track's
// height (the drag on its header's bottom edge, its keys and menu items, saved on the track) and the
// track-header column's width (the drag on the seam, its keys, remembered in the user settings).
// Real mouse events on the real handles of a bare panel, against a host whose edits are undoable.

#include "../AutomationLanes/AutomationLanesTestFixture.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/EdgeResizeHandle.h"

using namespace automation_lanes_test;
using synth::TrackKind;
using synth::ui::EdgeResizeHandle;
using synth::ui::TimelinePanelComponent;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

// Every track edit is one undo step, the way MainComponent::performTrackEdit records it.
struct UndoHost : TrackHeaderHost {
    synth::TimelineDoc& doc;
    AppUndoManager& undo;
    UndoHost(synth::TimelineDoc& d, AppUndoManager& u)
        : doc(d)
        , undo(u) {}
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(synth::TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(synth::TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(synth::TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override { undo.recordTimelineChange(doc, mutation); }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
};

struct SizingPanel : LanesPanel {
    UndoHost host{doc, undo};
    synth::TrackId bass, lead, pad;
    SizingPanel() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        lead = doc.addTrack(TrackKind::Midi, "Lead");
        pad = doc.addTrack(TrackKind::Midi, "Pad");
        panel.setTrackHeaderHost(&host);
    }
    ~SizingPanel() { panel.setTrackHeaderHost(nullptr); }

    synth::ui::TimelineRowLayout layout() { return panel.getClipLaneArea().getRowLayout(); }
    TimelineTrackHeaderComponent& header(int i) { return *panel.getTrackHeaderAt(i); }

    // Drags a handle by `delta` px along its axis the way a real pointer does: fixed on SCREEN while
    // the handle moves under it, so each event's local position is re-derived from the handle's
    // current place (JUCE delivers drag positions relative to where the component is now).
    static void dragHandle(EdgeResizeHandle& handle, int delta, int steps = 4) {
        const auto start = handle.localPointToGlobal(handle.getLocalBounds().getCentre().toFloat());
        const auto along = handle.getAxis() == EdgeResizeHandle::Axis::Vertical ? juce::Point<float>(0.0f, 1.0f)
                                                                                : juce::Point<float>(1.0f, 0.0f);
        const auto local = [&](float t) { return handle.getLocalPoint(nullptr, start + along * ((float)delta * t)); };
        handle.mouseDown(makeClickEvent(handle, local(0.0f), leftButton()));
        for (int i = 1; i <= steps; ++i)
            handle.mouseDrag(makeDragEvent(handle, local((float)i / (float)steps), local(0.0f), leftButton()));
        handle.mouseUp(makeDragEvent(handle, local(1.0f), local(0.0f), leftButton()));
    }
};

} // namespace

// ---- One track's height ------------------------------------------------------------------

TEST(TimelineTrackHeightTest, DraggingARowsBottomEdgeResizesOnlyThatTrackInOneUndoStep) {
    SizingPanel f;
    const auto before = f.layout();
    const int base = before.trackRowHeight(1);
    auto& handle = f.header(1).getHeightHandle();
    ASSERT_TRUE(handle.isVisible());
    // A real press at the row's bottom edge lands on the handle, not the row (whose drag reorders).
    const auto edge = f.panel.getLocalPoint(&handle, handle.getLocalBounds().getCentre());
    EXPECT_EQ(f.componentAt(edge), &handle);

    SizingPanel::dragHandle(handle, 40);

    const auto after = f.layout();
    EXPECT_EQ(after.trackRowHeight(1), base + 40) << "Lead grew by the drag";
    EXPECT_EQ(after.trackRowHeight(0), base) << "Bass untouched";
    EXPECT_EQ(after.trackRowHeight(2), base) << "Pad untouched";
    EXPECT_EQ(f.header(2).getY(), before.trackTop(2) + 40) << "Pad moved down with it";
    EXPECT_EQ(f.header(1).getHeight(), base + 40) << "the header row follows the layout";
    EXPECT_NEAR(f.doc.getTrack(f.lead)->heightScale, (double)(base + 40) / base, 1e-9) << "saved on the track";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(f.lead)->heightScale, 1.0) << "ONE undo step for the whole drag";
    EXPECT_EQ(f.layout().trackRowHeight(1), base);
}

TEST(TimelineTrackHeightTest, TheDragPreviewsWithoutTouchingTheDocUntilRelease) {
    SizingPanel f;
    const int base = f.layout().trackRowHeight(0);
    auto& handle = f.header(0).getHeightHandle();
    const auto centre = handle.getLocalBounds().getCentre().toFloat();
    handle.mouseDown(makeClickEvent(handle, centre, leftButton()));
    handle.mouseDrag(makeDragEvent(handle, centre + juce::Point<float>(0.0f, 30.0f), centre, leftButton()));
    EXPECT_EQ(f.layout().trackRowHeight(0), base + 30) << "the row follows the pointer";
    EXPECT_EQ(f.doc.getTrack(f.bass)->heightScale, 1.0) << "no doc write mid-drag";
    handle.mouseUp(makeDragEvent(handle, centre + juce::Point<float>(0.0f, 30.0f), centre, leftButton()));
    EXPECT_GT(f.doc.getTrack(f.bass)->heightScale, 1.0);
}

TEST(TimelineTrackHeightTest, ADragIsClampedAndADoubleClickResets) {
    SizingPanel f;
    const int base = f.layout().trackRowHeight(0);
    auto& handle = f.header(0).getHeightHandle();
    SizingPanel::dragHandle(handle, -500);
    EXPECT_EQ(f.doc.getTrack(f.bass)->heightScale, synth::Track::kMinHeightScale);
    EXPECT_EQ(f.layout().trackRowHeight(0), (int)std::llround(base * synth::Track::kMinHeightScale));

    handle.mouseDoubleClick(makeClickEvent(handle, handle.getLocalBounds().getCentre().toFloat(), leftButton()));
    EXPECT_EQ(f.doc.getTrack(f.bass)->heightScale, 1.0);
    EXPECT_EQ(f.layout().trackRowHeight(0), base);
}

TEST(TimelineTrackHeightTest, TheHeightRidesOnTopOfTheVerticalZoomAndClipsFillTheTallerRow) {
    SizingPanel f;
    ASSERT_TRUE(f.doc.setTrackHeightScale(f.bass, 2.0));
    const int base = f.layout().trackRowHeight(1);
    EXPECT_EQ(f.layout().trackRowHeight(0), 2 * base);

    const auto rect = synth::ui::TimelineClipLaneArea::computeClipRect(f.panel.getViewState(), f.layout(), 0, 0.0, 4.0);
    EXPECT_EQ(rect.getHeight(), f.layout().trackRowHeight(0)) << "a clip fills its own track's row";
}

TEST(TimelineTrackHeightTest, ZoomingTheTracksGivesEveryTrackTheSameHeight) {
    for (const double factor : {1.5, 0.5}) {
        SizingPanel f;
        ASSERT_TRUE(f.doc.setTrackHeightScale(f.lead, 2.5));
        ASSERT_TRUE(f.doc.setTrackHeightScale(f.pad, 0.6));
        ASSERT_NE(f.layout().trackRowHeight(0), f.layout().trackRowHeight(1));

        f.panel.zoomTimelineVertical(factor);

        const int height = f.layout().trackRowHeight(0);
        EXPECT_EQ(f.layout().trackRowHeight(1), height) << "factor " << factor;
        EXPECT_EQ(f.layout().trackRowHeight(2), height) << "factor " << factor;
        EXPECT_EQ(f.header(1).getHeight(), height) << "the header rows follow";
        for (const auto id : {f.bass, f.lead, f.pad})
            EXPECT_EQ(f.doc.getTrack(id)->heightScale, 1.0) << "no per-track height is left";
    }
}

TEST(TimelineTrackHeightTest, TheZoomStaysEqualAtTheClampsAndWhenRepeated) {
    using View = synth::ui::TimelineViewState;
    SizingPanel f;
    auto& state = f.panel.getViewState();
    for (const double factor : {10.0, 0.01}) {
        ASSERT_TRUE(f.doc.setTrackHeightScale(f.lead, 3.0));
        for (int i = 0; i < 20; ++i)
            f.panel.zoomTimelineVertical(factor);
        EXPECT_DOUBLE_EQ(state.rowHeightScale, factor > 1.0 ? View::kMaxRowHeightScale : View::kMinRowHeightScale);
        EXPECT_EQ(f.layout().trackRowHeight(0), f.layout().trackRowHeight(1));
        EXPECT_EQ(f.layout().trackRowHeight(2), f.layout().trackRowHeight(1));
    }

    // Already at the clamp: a mixed height is still made equal, though the zoom itself moves nothing.
    ASSERT_TRUE(f.doc.setTrackHeightScale(f.lead, 3.0));
    f.panel.zoomTimelineVertical(0.5);
    EXPECT_EQ(f.layout().trackRowHeight(1), f.layout().trackRowHeight(0));
}

TEST(TimelineTrackHeightTest, ZoomingTheTracksIsOneUndoStepThatRestoresTheMixedHeights) {
    SizingPanel f;
    SizingPanel::dragHandle(f.header(1).getHeightHandle(), 40); // Lead taller, its own undo step
    const double lead = f.doc.getTrack(f.lead)->heightScale;
    ASSERT_GT(lead, 1.0);
    ASSERT_TRUE(f.doc.setTrackHeightScale(f.pad, 0.7)); // not recorded

    f.panel.zoomTimelineVertical(0.5);
    f.panel.zoomTimelineVertical(0.5); // heights already equal: no second write
    EXPECT_EQ(f.doc.getTrack(f.lead)->heightScale, 1.0);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.lead)->heightScale, lead) << "one undo brings the mixed heights back";
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.pad)->heightScale, 0.7);
    EXPECT_EQ(f.doc.getTrack(f.bass)->heightScale, 1.0);
}

TEST(TimelineTrackHeightTest, KeysOnTheFocusedRowAndTheMenuItemsStepTheHeight) {
    SizingPanel f;
    ShortcutManager shortcuts;
    f.panel.setShortcutManager(&shortcuts);
    auto& row = f.header(0);
    EXPECT_TRUE(row.keyPressed(shortcuts.getBinding("timelineIncreaseTrackHeight")));
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.bass)->heightScale, TimelinePanelComponent::kTrackHeightStepFactor);
    EXPECT_TRUE(row.keyPressed(shortcuts.getBinding("timelineDecreaseTrackHeight")));
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.bass)->heightScale, 1.0);
    EXPECT_TRUE(row.keyPressed(shortcuts.getBinding("timelineIncreaseTrackHeight")));
    EXPECT_TRUE(row.keyPressed(shortcuts.getBinding("timelineResetTrackHeight")));
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.bass)->heightScale, 1.0);

    const auto menu = row.buildContextMenu();
    const auto* taller = findMenuItem(menu, "Increase Track Height");
    ASSERT_NE(taller, nullptr);
    EXPECT_EQ(taller->shortcutKeyDescription,
              shortcuts.getBinding("timelineIncreaseTrackHeight").getTextDescriptionWithIcons())
        << "the menu names the shortcut";
    ASSERT_NE(findMenuItem(menu, "Decrease Track Height"), nullptr);
    ASSERT_NE(findMenuItem(menu, "Reset Track Height"), nullptr);
    row.applyContextMenuChoice(TimelineTrackHeaderComponent::kIncreaseHeightMenuId);
    EXPECT_DOUBLE_EQ(f.doc.getTrack(f.bass)->heightScale, TimelinePanelComponent::kTrackHeightStepFactor);
    f.panel.setShortcutManager(nullptr);
}

TEST(TimelineTrackHeightTest, TheHandleIsMouseOnlyButNamedWithItsShortcuts) {
    SizingPanel f;
    auto& handle = f.header(0).getHeightHandle();
    EXPECT_FALSE(handle.getWantsKeyboardFocus()) << "the row is the Tab stop, not its edge";
    EXPECT_EQ(handle.getTitle(), "Resize Bass");
    EXPECT_TRUE(handle.getTooltip().contains("double-click"));
    EXPECT_TRUE(handle.getTooltip().contains(
        juce::KeyPress('=', juce::ModifierKeys::altModifier, 0).getTextDescriptionWithIcons()));
    EXPECT_EQ(handle.getMouseCursor(), juce::MouseCursor(juce::MouseCursor::UpDownResizeCursor));
}

TEST(TimelineTrackHeightTest, TheUnassignedSectionRowHasNoHeightHandle) {
    SizingPanel f;
    const auto unassigned = f.doc.addTrack(TrackKind::Automation, "Automation");
    f.addLane(unassigned, "loose");
    auto* section = f.panel.getTrackHeaderAt(f.indexOf(unassigned));
    ASSERT_NE(section, nullptr);
    EXPECT_FALSE(section->getHeightHandle().isVisible());
    EXPECT_EQ(findMenuItem(section->buildContextMenu(), "Increase Track Height"), nullptr);
}

// ---- The track-header column's width ---------------------------------------------------

TEST(TimelineTrackColumnWidthTest, DraggingTheSeamWidensTheColumnAndTheHeadersWithIt) {
    SizingPanel f;
    const int before = f.panel.getTrackHeaderBounds().getWidth();
    auto& handle = f.panel.getTrackHeaderWidthHandle();
    const auto seam = f.panel.getLocalPoint(&handle, handle.getLocalBounds().getCentre());
    EXPECT_EQ(std::abs(seam.x - f.panel.getTrackHeaderBounds().getRight()), 0) << "straddles the seam";
    EXPECT_EQ(f.componentAt(seam), &handle) << "a real press on the seam lands on the handle";

    SizingPanel::dragHandle(handle, 60);

    EXPECT_EQ(f.panel.getTrackHeaderBounds().getWidth(), before + 60);
    EXPECT_EQ(f.panel.getClipLaneArea().getX(), before + 60) << "the clips start after the wider column";
    EXPECT_GE(f.header(0).getWidth(), before + 60 - 20) << "rows fill the wider column (minus the scrollbar)";
}

TEST(TimelineTrackColumnWidthTest, TheWidthIsClampedAndADoubleClickOrReturnResets) {
    SizingPanel f;
    const int defaultWidth = f.panel.defaultTrackHeaderWidth();
    auto& handle = f.panel.getTrackHeaderWidthHandle();
    SizingPanel::dragHandle(handle, -1000);
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), TimelinePanelComponent::kMinTrackHeaderWidth);
    SizingPanel::dragHandle(handle, 5000);
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), TimelinePanelComponent::kMaxTrackHeaderWidth);
    handle.mouseDoubleClick(makeClickEvent(handle, handle.getLocalBounds().getCentre().toFloat(), leftButton()));
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), defaultWidth);

    EXPECT_TRUE(handle.getWantsKeyboardFocus()) << "one Tab stop for the one seam";
    EXPECT_TRUE(handle.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), defaultWidth + TimelinePanelComponent::kTrackHeaderWidthKeyStep);
    EXPECT_TRUE(handle.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_TRUE(handle.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), defaultWidth - TimelinePanelComponent::kTrackHeaderWidthKeyStep);
    EXPECT_TRUE(handle.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.panel.getTrackHeaderWidth(), defaultWidth);
    EXPECT_EQ(handle.getTitle(), "Track column width");
    EXPECT_TRUE(handle.getTooltip().isNotEmpty());
}

TEST(TimelineTrackColumnWidthTest, TheWidthIsRememberedInTheUserSettings) {
    // An isolated settings file, removed before and after (the FollowPlayhead tests' idiom).
    juce::PropertiesFile::Options opts;
    opts.applicationName = opts.folderName = "Agent Synth Track Column Width Test";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    opts.storageFormat = juce::PropertiesFile::storeAsXML;
    juce::ApplicationProperties props;
    props.setStorageParameters(opts);
    props.getUserSettings()->getFile().deleteFile();
    props.setStorageParameters(opts);
    {
        SizingPanel f;
        f.panel.setApplicationProperties(&props);
        SizingPanel::dragHandle(f.panel.getTrackHeaderWidthHandle(), 50);
        EXPECT_EQ(props.getUserSettings()->getIntValue("timelineTrackHeaderWidth"), f.panel.getTrackHeaderWidth());
        f.panel.setApplicationProperties(nullptr);
    }
    {
        SizingPanel restored;
        restored.panel.setApplicationProperties(&props);
        EXPECT_EQ(restored.panel.getTrackHeaderWidth(), restored.panel.defaultTrackHeaderWidth() + 50);
        restored.panel.setApplicationProperties(nullptr);
    }
    props.getUserSettings()->getFile().deleteFile();
}

TEST(TimelineTrackColumnWidthTest, AtTheNarrowestWidthTheHeaderControlsStillFitWithoutOverlapping) {
    SizingPanel f;
    f.panel.setTrackHeaderWidth(TimelinePanelComponent::kMinTrackHeaderWidth, false);
    auto& row = f.header(0);
    const auto local = row.getLocalBounds();
    const auto arrow = row.getFoldArrow().getBounds();
    juce::Rectangle<int> toggles;
    for (auto* child : row.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (const auto text = button->getButtonText(); text == "M" || text == "S" || text == "R")
                toggles = toggles.isEmpty() ? button->getBounds() : toggles.getUnion(button->getBounds());
    ASSERT_FALSE(toggles.isEmpty());
    EXPECT_TRUE(local.contains(toggles)) << "M/S/R stay inside the row";
    EXPECT_TRUE(local.contains(arrow));
    EXPECT_LE(arrow.getRight(), toggles.getX()) << "the arrow never runs under the toggles";
}
