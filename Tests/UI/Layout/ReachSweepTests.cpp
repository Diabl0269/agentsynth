// ReachSweepTests.cpp -- real keys reach every stop of every keyboard focus region, and every stop they land on shows
// that it has focus. A real MainComponent is put on the desktop and keys are delivered the way the operating system
// does (RealKeyDelivery.h: the window's peer, never a component's keyPressed). Each scene below (every main view with
// the dock open, the canvas with it closed, the library showing Sounds with a sound selected, the timeline's side pane,
// a piano roll, a drum machine, the mixer's side pane, the AI panel with the Mod Matrix) is walked region by region
// (ReachSweepWalk.h: Down, Up, Left, Right, Tab, Shift+Tab and Escape from every place focus lands, the toolbars' and
// panels' key rings walked to their end, then the global keys Cmd+1..4 and Cmd+Shift+Up each followed by the arrows).
//
// After every key it asserts that focus is in some region, that the stop it landed on draws a ring (or a ring source of
// its region shows), that no ring stays on after focus left its owner, and that Tab leaves a region for the next open
// one; after the walk, that every stop of the region was reached by keys. A failure prints
// "<scene> / <region id> / <control>: <what>" (the key and the previous stop are printed beside it, not in it); a
// control is named by its class and title, button text or component id, with digits written as '#'. A gap that is known
// and ticketed lives in kKnownGaps; the list may only shrink (docs/development/accessibility.md).
// AGENTSYNTH_REACH_VERBOSE=1 prints every step.
#include "../../TestSettingsHelpers.h"
#include "KeyboardSweepShell.h"
#include "ReachSweepWalk.h"
#include "UI/Library/LibrarySidebarViews.h"
#include <gtest/gtest.h>

using namespace synth::test::keyboard_sweep;
using namespace synth::test::real_keys;
using Tab = synth::ui::BottomDockComponent::Tab;
using synth::ui::SidebarView;

namespace {

// A gap that is known and has a ticket. Key: "<scene> / <region id> / <control>: <what>" as a finding prints it.
const std::vector<KnownGap> kKnownGaps = {
    // Mixer zones pane and rows: the side pane is not reached by any key.
    {"mixer side pane / mixer / MixerZonesPane 'Zones and visibility': not reached by keys", "FRO1529"},
    {"mixer side pane / mixer / MixerZonesRow 'Audio #': not reached by keys", "FRO1529"},
    {"mixer side pane / mixer / MixerZonesRow 'Direct': not reached by keys", "FRO1529"},
    {"mixer side pane / mixer / MixerZonesRow 'Master': not reached by keys", "FRO1529"},
    // Library sidebar switch and search box: the region lands on the list, no key steps to them.
    {"canvas / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"canvas / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"canvas, AI panel and Mod Matrix / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"canvas, AI panel and Mod Matrix / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"canvas, dock closed / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"canvas, dock closed / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"drum machine open / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"drum machine open / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"library sounds / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"midiRemote / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"midiRemote / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"mixer / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"mixer / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"mixer side pane / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"mixer side pane / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"piano roll open / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"piano roll open / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"timeline / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"timeline / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    {"timeline side pane / library / LibrarySidebarSwitch 'Sidebar view': not reached by keys", "FRO1541"},
    {"timeline side pane / library / TextEditor 'Search library': not reached by keys", "FRO1541"},
    // Drum machine header controls: no key steps to them.
    {"drum machine open / timeline / CardKnobSlider 'Humanise': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / CardSegmentedSwitch 'View': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / CardStepper 'Swing': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / ComboBox 'Steps per bar': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / LaneChipRow 'Lanes': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Clap row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Closed hat row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Cowbell row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Cymbal row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Kick row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Open hat row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Snare row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / NameButton 'Tom row': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Ideas': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Clap': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Closed hat': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Cowbell': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Cymbal': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Kick': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Open hat': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Snare': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Mute Tom': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Play clip': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Clap': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Closed hat': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Cowbell': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Cymbal': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Kick': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Open hat': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Snare': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / TextButton 'Solo Tom': not reached by keys", "FRO1549"},
    {"drum machine open / timeline / ToggleButton 'Show all drums': not reached by keys", "FRO1549"},
    // Drum machine grid, piano roll and velocity editor: the open clip editor takes no region key.
    {"drum machine open / timeline / DrumMachineComponent 'Drum machine grid': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollComponent 'Piano roll': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Back to clips': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Fit notes to key': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Humanize velocities': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Quantize note lengths': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Quantize note starts': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Scale assist': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Show only scale notes': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollHeaderChip 'Show velocity strip': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / PianoRollVelocityLane 'Velocity strip': not reached by keys", "FRO1550"},
    {"piano roll open / timeline / TextEditor 'Velocity of selected notes': not reached by keys", "FRO1550"},
    // AIChatComponent is a plain-container region with no Up/Down stepping, so none of its controls is reachable.
    {"canvas, AI panel and Mod Matrix / aiPanel / ComboBox 'AI model': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextButton 'History': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextButton 'New Chat': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextButton 'Send': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextButton 'Sign in': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextButton 'Upgrade to Pro': not reached by keys", "FRO1816"},
    {"canvas, AI panel and Mod Matrix / aiPanel / TextEditor 'Message to the AI assistant': not reached by keys",
     "FRO1816"},
    // Card header buttons (Bypass, Mute, Delete, Dual I/O) are DrawableButtons and drawDrawableButton paints no focus
    // ring.
    {"canvas / canvas / DrawableButton 'Delete': lands on a stop that draws no ring", "FRO1815"},
    {"canvas / canvas / DrawableButton> 'Bypass': lands on a stop that draws no ring", "FRO1815"},
    {"canvas / canvas / DrawableButton> 'Dual I/O': lands on a stop that draws no ring", "FRO1815"},
    {"canvas / canvas / DrawableButton> 'Mute': lands on a stop that draws no ring", "FRO1815"},
    {"canvas, AI panel and Mod Matrix / canvas / DrawableButton 'Delete': lands on a stop that draws no ring",
     "FRO1815"},
    {"canvas, AI panel and Mod Matrix / canvas / DrawableButton> 'Bypass': lands on a stop that draws no ring",
     "FRO1815"},
    {"canvas, AI panel and Mod Matrix / canvas / DrawableButton> 'Dual I/O': lands on a stop that draws no ring",
     "FRO1815"},
    {"canvas, AI panel and Mod Matrix / canvas / DrawableButton> 'Mute': lands on a stop that draws no ring",
     "FRO1815"},
    {"canvas, dock closed / canvas / DrawableButton 'Delete': lands on a stop that draws no ring", "FRO1815"},
    {"canvas, dock closed / canvas / DrawableButton> 'Bypass': lands on a stop that draws no ring", "FRO1815"},
    {"canvas, dock closed / canvas / DrawableButton> 'Dual I/O': lands on a stop that draws no ring", "FRO1815"},
    {"canvas, dock closed / canvas / DrawableButton> 'Mute': lands on a stop that draws no ring", "FRO1815"},
    {"drum machine open / canvas / DrawableButton 'Delete': lands on a stop that draws no ring", "FRO1815"},
    {"drum machine open / canvas / DrawableButton> 'Bypass': lands on a stop that draws no ring", "FRO1815"},
    {"drum machine open / canvas / DrawableButton> 'Mute': lands on a stop that draws no ring", "FRO1815"},
    {"timeline side pane / canvas / DrawableButton 'Delete': lands on a stop that draws no ring", "FRO1815"},
    {"timeline side pane / canvas / DrawableButton> 'Bypass': lands on a stop that draws no ring", "FRO1815"},
    {"timeline side pane / canvas / DrawableButton> 'Mute': lands on a stop that draws no ring", "FRO1815"},
    // Gain knob and Safety limiter, seen only in the scene with the Mod Matrix open over the canvas: no ring shows
    // where the Mod Matrix overlaps the card.
    {"canvas, AI panel and Mod Matrix / canvas / CardKnobSlider 'Gain': lands on a stop that draws no ring", "FRO1815"},
    {"canvas, AI panel and Mod Matrix / canvas / ToggleButton> 'Safety limiter': lands on a stop that draws no ring",
     "FRO1815"},
    // With the dock closed the tab strip has no height, so landing on it draws no ring.
    {"canvas, dock closed / dockTabs / TabStripFocus 'Panel tabs': lands on a stop that draws no ring", "FRO1819"},
    // The panel is a plain-container region with no Up/Down stepping (FocusStepWithin is not used), so its buttons and
    // the controller surface are unreachable.
    {"midiRemote / midiRemote / ControllerSurfaceComponent 'Controller surface': not reached by keys", "FRO1816"},
    {"midiRemote / midiRemote / TextButton 'addControllerButton': not reached by keys", "FRO1816"},
    {"midiRemote / midiRemote / TextButton 'moreButton': not reached by keys", "FRO1816"},
    // MidiRemotePanelComponent takes focus as the region landing and paints no ring.
    {"canvas, AI panel and Mod Matrix / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that "
     "draws no ring",
     "FRO1819"},
    {"drum machine open / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819"},
    {"library sounds / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819"},
    {"midiRemote / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring", "FRO1819"},
    {"mixer side pane / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819"},
    {"piano roll open / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819"},
    {"timeline side pane / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819"},
    // The Mod Matrix is a plain-container region with no Up/Down stepping, so flatSources and addModulation are
    // unreachable.
    {"library sounds / modMatrix / TextButton 'addModulation': not reached by keys", "FRO1816"},
    {"library sounds / modMatrix / ToggleButton 'flatSources': not reached by keys", "FRO1816"},
    {"mixer side pane / modMatrix / TextButton 'addModulation': not reached by keys", "FRO1816"},
    {"mixer side pane / modMatrix / ToggleButton 'flatSources': not reached by keys", "FRO1816"},
    {"piano roll open / modMatrix / TextButton 'addModulation': not reached by keys", "FRO1816"},
    {"piano roll open / modMatrix / ToggleButton 'flatSources': not reached by keys", "FRO1816"},
    {"timeline / modMatrix / TextButton 'addModulation': not reached by keys", "FRO1816"},
    {"timeline / modMatrix / ToggleButton 'flatSources': not reached by keys", "FRO1816"},
    // Resize handles are Tab stops (setKeyboardFocusable) but Tab is the region cycle and no region key steps to them.
    {"canvas / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"canvas, AI panel and Mod Matrix / timeline / EdgeResizeHandle 'Track column width': not reached by keys",
     "FRO1817"},
    {"drum machine open / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"library sounds / library / EdgeResizeHandle 'Resize Sounds column': not reached by keys", "FRO1817"},
    {"library sounds / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"midiRemote / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"mixer / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"mixer side pane / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"piano roll open / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"timeline / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    {"timeline side pane / timeline / EdgeResizeHandle 'Track column width': not reached by keys", "FRO1817"},
    // SoundBrowserComponent takes the landing and forwards focus to its list, which draws no ring at the landing.
    {"library sounds / library / SoundBrowserComponent 'Sounds': lands on a stop that draws no ring", "FRO1541"},
    // The section strip and clip lanes are documented Tab stops that the region cycle skips; Clips is entered only by
    // Return on a track holding a clip (enterTrackClips).
    {"canvas / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"canvas / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"canvas, AI panel and Mod Matrix / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"library sounds / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"library sounds / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"midiRemote / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"midiRemote / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"mixer / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"mixer / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"mixer side pane / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"timeline / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"timeline / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    {"timeline side pane / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818"},
    {"timeline side pane / timeline / TimelineSectionStrip 'Sections': not reached by keys", "FRO1818"},
    // Seen on the CI runner only, so marked `varies`: its smaller screen changes the piano roll's layout and the
    // timing of the mixer's column fade and the dock's closing.
    {"piano roll open / timeline / TimelineClipLaneArea 'Clips': not reached by keys", "FRO1818", true},
    {"canvas / mixer / MixerPanelComponent 'Mixer': stale ring (focused column)", "FRO1529", true},
    {"canvas, AI panel and Mod Matrix / mixer / MixerPanelComponent 'Mixer': stale ring (focused column)", "FRO1529",
     true},
    {"library sounds / mixer / MixerPanelComponent 'Mixer': stale ring (focused column)", "FRO1529", true},
    {"canvas, dock closed / midiRemote / MidiRemotePanelComponent 'Controllers': lands on a stop that draws no ring",
     "FRO1819", true},
};

class FocusHelper : public juce::Component {
public:
    FocusHelper() {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(false, false);
    }
};

struct Scene {
    const char* name;
    std::function<void()> setup;
};

class ReachSweepShell : public KeyboardDesktopShell {
protected:
    void SetUp() override {
        KeyboardDesktopShell::SetUp();
        if (mc_ == nullptr)
            return;
        mc_->addAndMakeVisible(helper_);
        helper_.setBounds(0, 0, 4, 4);
    }
    void TearDown() override {
        if (mc_ != nullptr)
            mc_->removeChildComponent(&helper_);
        KeyboardDesktopShell::TearDown();
    }

    struct Layout {
        Tab view = Tab::Canvas;
        bool dockOpen = true;
        bool timelineSidePane = false;
        bool mixerSidePane = false;
        SidebarView library = SidebarView::Modules;
    };

    synth::ClipId clip() {
        if (!clip_.has_value())
            clip_ = mc_->getTimelineDoc().addClip(mc_->getTimelineDoc().getTracks().front().id, 0.0, 8.0, "Clip");
        return *clip_;
    }

    void showDock(bool open) {
        if (open && !mc_->isBottomDockConfiguredVisible())
            mc_->ensureBottomDockOpen();
        if (!open && mc_->isBottomDockConfiguredVisible())
            mc_->toggleBottomPanelFromShortcut();
    }

    void selectFirstSound(synth::ui::SoundBrowserComponent& sounds) {
        // Walk down from the first place or folder until a sound row shows.
        for (int depth = 0; depth < 5; ++depth) {
            synth::test::waitUntil([&] { return !sounds.getRows().empty(); }, 2000, 20);
            const auto& rows = sounds.getRows();
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].kind == synth::ui::SoundBrowserComponent::RowKind::Sound) {
                    sounds.selectRow((int)i);
                    return;
                }
            const synth::ui::SoundBrowserComponent::Row* next = nullptr;
            for (const auto& row : rows)
                if (next == nullptr && (row.kind == synth::ui::SoundBrowserComponent::RowKind::Place ||
                                        row.kind == synth::ui::SoundBrowserComponent::RowKind::Folder))
                    next = &row;
            if (next == nullptr)
                return;
            const auto place = next->placeId.isNotEmpty() ? next->placeId : sounds.getPlaceId();
            const auto folder = next->folder;
            sounds.showLocation(place, folder);
            sounds.completeListAnimationForTest();
        }
    }

    void showSounds(bool shown) {
        auto& lib = mc_->getLibrarySidebarForTest();
        lib.setView(shown ? SidebarView::Sounds : SidebarView::Modules, false);
        lib.completeViewMotionForTest();
        mc_->resized();
        if (shown) {
            selectFirstSound(lib.getSounds());
            mc_->resized();
        }
    }

    void arrange(const Layout& l) {
        auto& timeline = mc_->getTimelinePanel();
        timeline.closePianoRoll();
        timeline.closeDrumMachine();
        const auto track = mc_->getTimelineDoc().getTracks().front().id;
        timeline.selectTracks({track}, track);
        mc_->setMainViewForTest(l.view);
        showDock(l.dockOpen);
        if (l.dockOpen) // a fixed dock tab, so a key that switched it is put back
            mc_->showBottomDockTab(l.view == Tab::Timeline ? Tab::Canvas : Tab::Timeline);
        if (timeline.getSidePane().isOpen() != l.timelineSidePane)
            timeline.toggleSidePane(l.timelineSidePane);
        synth::test::reach::setMixerSidePaneOpen(*mc_, l.mixerSidePane);
        showSounds(l.library == SidebarView::Sounds);
        mc_->resized();
    }

    std::vector<Scene> scenes() {
        std::vector<Scene> all;
        for (const auto& [name, view] : {std::pair{"canvas", Tab::Canvas}, std::pair{"timeline", Tab::Timeline},
                                         std::pair{"mixer", Tab::Mixer}, std::pair{"midiRemote", Tab::MidiRemote}})
            all.push_back({name, [this, view = view] { arrange({view}); }});
        all.push_back({"canvas, dock closed", [this] { arrange({Tab::Canvas, false}); }});
        all.push_back({"library sounds", [this] { arrange({Tab::Canvas, true, false, false, SidebarView::Sounds}); }});
        all.push_back({"timeline side pane", [this] { arrange({Tab::Timeline, true, true}); }});
        all.push_back({"piano roll open", [this] {
                           arrange({Tab::Timeline});
                           mc_->getTimelinePanel().openPianoRoll(clip());
                           mc_->getTimelinePanel().getPianoRoll().toggleScalePanel();
                           mc_->resized();
                       }});
        all.push_back({"drum machine open", [this] {
                           arrange({Tab::Timeline});
                           mc_->getTimelinePanel().openDrumMachine(clip());
                           mc_->resized();
                       }});
        all.push_back({"mixer side pane", [this] { arrange({Tab::Mixer, true, false, true}); }});
        all.push_back({"canvas, AI panel and Mod Matrix", [this] {
                           arrange({Tab::Canvas});
                           for (const char* id : {"aiPanel", "modMatrix"}) {
                               const auto* region = mc_->getFocusRegionsForTest().findById(id);
                               if (region != nullptr && !region->isCurrentlyOpen()) {
                                   if (region->open)
                                       region->open();
                                   else
                                       mc_->simulateToggleModMatrixClick();
                               }
                           }
                           mc_->resized();
                       }});
        return all;
    }

    // The Sounds preview strip's Tab order in a scene: Tab from the list lands on the strip's first control, Tab from
    // any strip control leaves the sidebar for the region after it in the cycle (the one beside it on screen, never
    // the window's top toolbar), and Shift+Tab from a strip control comes back to the list.
    void checkPreviewStripTab(const Scene& scene) {
        scene.setup();
        showSounds(true);
        auto& sounds = mc_->getLibrarySidebarForTest().getSounds();
        auto& strip = sounds.getPreviewFooter();
        const auto stops = synth::ui::shownFocusableDescendants(strip);
        const auto& registry = mc_->getFocusRegionsForTest();
        ASSERT_TRUE(strip.isShowing() && !stops.empty()) << scene.name << ": the preview strip shows no control";
        const auto expected = registry.nextOpenRegionId("library", true);
        ASSERT_NE(expected, juce::String("toolbar")) << scene.name;
        for (auto* stop : stops) {
            juce::Component::SafePointer<juce::Component> control(stop);
            ASSERT_TRUE(takeFocus(*control)) << scene.name << " / " << describe(*control);
            const auto name = describe(*control);
            deliver(*control, kTabKey());
            const auto* landed = registry.regionContaining(juce::Component::getCurrentlyFocusedComponent());
            EXPECT_TRUE(landed != nullptr && landed->id == expected)
                << scene.name << ": Tab from the preview strip's " << name << " lands in "
                << (landed != nullptr ? landed->id.toStdString() : std::string("no region")) << ", not "
                << expected.toStdString();
            if (control == nullptr || !takeFocus(*control))
                continue;
            deliver(*control, kShiftTabKey());
            auto* back = juce::Component::getCurrentlyFocusedComponent();
            EXPECT_TRUE(back == &sounds || (back != nullptr && sounds.isParentOf(back) && !strip.isParentOf(back)))
                << scene.name << ": Shift+Tab from the preview strip's " << name << " does not return to the list";
        }
        sounds.focusList();
        synth::test::flushMessageQueue();
        if (auto* list = juce::Component::getCurrentlyFocusedComponent()) {
            deliver(*list, kTabKey());
            EXPECT_EQ(juce::Component::getCurrentlyFocusedComponent(), stops.front())
                << scene.name << ": Tab from the Sounds list does not land on the preview strip's first control";
        }
    }

    static const juce::KeyPress& kShiftTabKey() {
        static const juce::KeyPress key(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0);
        return key;
    }

    static const juce::KeyPress& kTabKey() {
        static const juce::KeyPress key(juce::KeyPress::tabKey);
        return key;
    }

    FocusHelper helper_;
    std::optional<synth::ClipId> clip_;
};

class ReachSweepFindings : public SweepFindings {
public:
    ReachSweepFindings()
        : SweepFindings("reach", kKnownGaps) {}
};

} // namespace

TEST_F(ReachSweepShell, RealKeysReachEveryStopOfEveryRegionAndEachLandingShowsItsFocus) {
#if !JUCE_MAC
    GTEST_SKIP() << "the known-gap list is measured on macOS; fonts, audio devices and menus differ on other platforms";
#endif
    ReachSweepFindings findings;
    for (const auto& scene : scenes()) {
        scene.setup();
        synth::test::flushMessageQueue();
        synth::test::reach::ReachWalker walker(*mc_, helper_, findings, scene.name, scene.setup);
        walker.walkAll();
    }
    findings.expectOnlyKnownGaps();
}

// Tab after the Sounds preview strip goes on to the region beside the sidebar in every main view, and comes back.
TEST_F(ReachSweepShell, TabFromThePreviewStripLeavesForTheRegionBesideTheSidebarAndShiftTabComesBack) {
#if !JUCE_MAC
    GTEST_SKIP() << "measured on macOS with the other sweeps";
#endif
    for (const auto& scene : scenes())
        checkPreviewStripTab(scene);
}
