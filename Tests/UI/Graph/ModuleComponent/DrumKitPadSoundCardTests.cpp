// DrumKitPadSoundCardTests.cpp
// A Drum Kit pad's sound on the card: the Sound row's menu and its groups, a built-in sound replacing a file in one
// undo step, an instrument pad (its pitch, Open, a deleted module), the pad menu from a right-click and Shift+F10,
// and a sound from the Sounds view dropped onto the pad under the pointer only.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/Pluck/PluckModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using namespace sampler_test;
using synth::drums::PadKind;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using synth::ui::DrumPadGrid;
using synth::ui::DrumPadSoundMenu;
using synth::ui::SoundDragState;

namespace {

struct Shown {
    juce::PopupMenu menu;
    int index = -1;
    bool padMenu = false;
    int count = 0;
};

struct Scene {
    TempDir dir;
    CardCanvas canvas;
    NodeID id, pluckId;
    juce::File wav = dir.file("hit.wav");
    Shown shown;

    Scene() {
        writeWav(wav, 4410, 1, 44100.0, [](int, int) { return 0.3f; });
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        pluckId = canvas.add(std::make_unique<PluckModule>(), 900, 0);
        canvas.editor.updateComponents();
        pads().showMenuForTest = [this](juce::PopupMenu& menu, int index, bool padMenu) {
            shown = {menu, index, padMenu, shown.count + 1};
        };
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }
    void poll() { card().timerCallback(); }

    // The id of the item called `text` in the menu last shown (searching submenus too), or 0.
    int itemCalled(const juce::String& text) const { return find(shown.menu, text); }
    static int find(const juce::PopupMenu& menu, const juce::String& text) {
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            const auto& item = it.getItem();
            if (item.text == text && item.itemID > 0)
                return item.itemID;
            if (item.subMenu != nullptr)
                if (const int found = find(*item.subMenu, text); found > 0)
                    return found;
        }
        return 0;
    }
    bool hasText(const juce::String& text) const {
        std::function<bool(const juce::PopupMenu&)> walk = [&](const juce::PopupMenu& menu) {
            for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
                if (it.getItem().text == text || (it.getItem().subMenu != nullptr && walk(*it.getItem().subMenu)))
                    return true;
            return false;
        };
        return walk(shown.menu);
    }
    void openSoundMenu(int pad) {
        pads().grid().pick(pad, false);
        pads().controls().showPad(pad, false);
        pads().controls().soundButton().onClick();
    }
    void choose(int pad, const juce::String& text) {
        openSoundMenu(pad);
        const int item = itemCalled(text);
        ASSERT_GT(item, 0) << text;
        pads().applyMenuResult(pad, item);
        poll();
    }
    juce::String pluckTitle() {
        auto* p = canvas.processor(pluckId);
        return p != nullptr ? p->getName() : juce::String();
    }
};

} // namespace

TEST(DrumKitPadSoundCard, TheSoundRowOpensTheGroupsForThePickedPad) {
    Scene s;
    s.openSoundMenu(1);
    EXPECT_EQ(s.shown.count, 1);
    EXPECT_EQ(s.shown.index, 1);
    EXPECT_FALSE(s.shown.padMenu);
    for (const char* text :
         {DrumPadSoundMenu::kFileGroup, "Choose a file...", "Recent files", DrumPadSoundMenu::kBuiltInGroup, "Kick",
          "Snare", "Clap", DrumPadSoundMenu::kMoreDrums, "Closed hat", "Low floor tom", "Ride",
          DrumPadSoundMenu::kInstrumentGroup, DrumPadSoundMenu::kKitGroup, "No kit loaded", "Reset to kit sound"})
        EXPECT_TRUE(s.hasText(text)) << text;
    EXPECT_GT(s.itemCalled(s.pluckTitle()), 0) << "the Pluck on the canvas is offered";
    EXPECT_FALSE(s.hasText(s.kit().getName())) << "a kit never plays itself";
    EXPECT_EQ(s.pads().controls().soundButton().getTitle(), "Sound for Snare, Snare, built-in");
}

TEST(DrumKitPadSoundCard, ABuiltInSoundReplacesAFilePadAndOneUndoBringsTheFileBack) {
    Scene s;
    ASSERT_TRUE(s.pads().setPadFileUndoably(0, s.wav));
    EXPECT_EQ(s.kit().pad(0).kind, PadKind::File);
    s.choose(0, "Snare");
    EXPECT_EQ(s.kit().pad(0).kind, PadKind::BuiltIn);
    EXPECT_EQ(s.kit().pad(0).sound, 38);
    EXPECT_EQ(s.kit().pad(0).name, "Snare");
    ASSERT_TRUE(s.canvas.undo.undo());
    s.poll();
    EXPECT_EQ(s.kit().pad(0).kind, PadKind::File);
    EXPECT_EQ(s.kit().pad(0).file, s.wav.getFullPathName());
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(s.kit().pad(0).kind, synth::drums::defaultPad(0).kind) << "the file pick was its own step";
}

TEST(DrumKitPadSoundCard, AnInstrumentPadLinksAndShowsItsPitchAndOpen) {
    Scene s;
    s.choose(2, s.pluckTitle());
    const auto& pad = s.kit().pad(2);
    EXPECT_EQ(pad.kind, PadKind::Instrument);
    EXPECT_EQ(pad.name, s.pluckTitle());
    EXPECT_TRUE(s.kit().padInstrumentLinked(2));
    auto& controls = s.pads().controls();
    EXPECT_TRUE(controls.openButton().isVisible());
    EXPECT_TRUE(controls.pitchStepper().isVisible());
    // Wide enough for "C3 (60)" between its two buttons, not squeezed into a knob column.
    EXPECT_GE(controls.pitchStepper().getWidth(), 90);
    EXPECT_FALSE(controls.knob(DrumKitModule::PadParam::Start).isVisible());
    EXPECT_TRUE((bool)controls.knob(DrumKitModule::PadParam::Decay)
                    .getProperties()[synth::theme::AppLookAndFeel::kDimmedProperty])
        << "an instrument shapes its own notes";
    EXPECT_TRUE(controls.soundButton().getTitle().contains("instrument"));
    EXPECT_TRUE(controls.openButton().getTitle().startsWith("Open "));
    EXPECT_TRUE(controls.openButton().getTooltip().isNotEmpty());

    controls.pitchStepper().onStep(+1);
    EXPECT_EQ(s.kit().pad(2).instrumentPitch, 61);
    ASSERT_TRUE(s.canvas.undo.undo());
    s.poll();
    EXPECT_EQ(s.kit().pad(2).instrumentPitch, 60) << "a pitch step is one undo step";
}

TEST(DrumKitPadSoundCard, OpenSelectsTheInstrumentOnTheCanvas) {
    Scene s;
    s.choose(2, s.pluckTitle());
    s.pads().controls().openButton().onClick();
    const auto selected = s.canvas.editor.getSelectedNodes();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0], s.pluckId);
}

TEST(DrumKitPadSoundCard, ADeletedInstrumentLeavesThePadSilentAndTheRowSaysSo) {
    Scene s;
    s.choose(2, s.pluckTitle());
    s.canvas.engine.getGraph().removeNode(s.pluckId);
    s.canvas.engine.refreshMidiWiring();
    s.poll();
    EXPECT_FALSE(s.kit().padInstrumentLinked(2));
    EXPECT_EQ(s.pads().describeSound(2).second, "removed, silent");
    EXPECT_TRUE(s.pads().controls().soundButton().getTitle().contains("removed"));
}

TEST(DrumKitPadSoundCard, ResetToKitSoundIsOneUndoStep) {
    Scene s;
    s.choose(1, "Clap");
    s.choose(1, "Reset to kit sound");
    EXPECT_EQ(s.kit().pad(1).sound, 38);
    EXPECT_EQ(s.kit().pad(1).name, "Snare");
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(s.kit().pad(1).sound, 39);
}

TEST(DrumKitPadSoundCard, ARightClickOpensThePadMenuOnThatPad) {
    Scene s;
    auto& grid = s.pads().grid();
    const auto mods = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    grid.mouseDown(makeModuleClickWithMods(grid, grid.padBounds(3).getCentre(), mods));
    EXPECT_EQ(s.shown.count, 1);
    EXPECT_TRUE(s.shown.padMenu);
    EXPECT_EQ(s.shown.index, 3);
    EXPECT_EQ(grid.picked(), 3);
    for (const char* text : {"Play", "Replace from file...", "Reset to kit sound", DrumPadSoundMenu::kBuiltInGroup,
                             DrumPadSoundMenu::kInstrumentGroup})
        EXPECT_TRUE(s.hasText(text)) << text;
    EXPECT_FALSE(s.hasText("Hide pad"));
}

TEST(DrumKitPadSoundCard, ShiftF10OpensThePadMenuForThePickedPad) {
    Scene s;
    auto& grid = s.pads().grid();
    grid.pick(5, false);
    EXPECT_TRUE(synth::ui::openContextMenuForFocusedComponent(&grid));
    EXPECT_TRUE(s.shown.padMenu);
    EXPECT_EQ(s.shown.index, 5);
    const int play = s.itemCalled("Play");
    ASSERT_GT(play, 0);
    s.pads().applyMenuResult(5, play);
    s.kit().prepareToPlay(44100.0, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    s.kit().processBlock(buffer, midi);
    EXPECT_EQ(s.kit().lastPlayedPad(), 5);
}

// The component JUCE's drag would drop on at `point` (in `root`): the deepest hit, then up through its parents to the
// first interested target, asked with `details` as JUCE asks it.
static juce::Component* dropTargetAt(juce::Component& root, juce::Point<int> point,
                                     const juce::DragAndDropTarget::SourceDetails& details) {
    for (auto* hit = root.getComponentAt(point); hit != nullptr; hit = hit->getParentComponent()) {
        if (auto* target = dynamic_cast<juce::DragAndDropTarget*>(hit);
            target != nullptr && target->isInterestedInDragSource(details))
            return hit;
        if (hit == &root)
            break;
    }
    return nullptr;
}

// Regression test for FRO1116: the grid decided by the details' position, which JUCE's drag fills with where the drag
// started, so a real drop on a pad fell through to the canvas and made a Sampler.
TEST(DrumKitPadSoundCard, ASoundDropsOnlyOnThePadUnderThePointer) {
    Scene s;
    auto& grid = s.pads().grid();
    const auto before = s.kit().padTable();
    SoundDragState::begin(s.wav);
    EXPECT_TRUE(grid.isShowingSoundOutline()) << "every pad shows the faint outline";
    s.poll();
    EXPECT_EQ(s.pads().caption().getText(), "Drop on a pad to play this sound there");

    const auto onPad = grid.padBounds(6).getCentre();
    const auto gap = juce::Point<int>(grid.padBounds(0).getRight() + DrumPadGrid::kGap / 2, onPad.y);
    const juce::var payload = synth::ui::makeSoundPayload(s.wav.getFullPathName());
    // As JUCE's drag looks for a target: hit-test down from the card, then ask with the drag's starting position
    // (a point in the Sounds view, nowhere near the pad).
    const juce::DragAndDropTarget::SourceDetails started(payload, nullptr, {80, 72});
    EXPECT_EQ(dropTargetAt(s.card(), s.card().getLocalPoint(&grid, onPad), started), &grid);
    EXPECT_EQ(dropTargetAt(s.card(), s.card().getLocalPoint(&grid, gap), started), nullptr)
        << "a gap passes the drop to the card, and the card on to the canvas";
    juce::DragAndDropTarget::SourceDetails details(payload, nullptr, onPad);
    ASSERT_TRUE(grid.isInterestedInDragSource(details));
    grid.itemDragEnter(details);
    EXPECT_EQ(grid.dropTarget(), 6);
    EXPECT_EQ(s.pads().caption().getText(), "Drop to make " + s.kit().pad(6).name + " play this sound");
    // The Sounds view ends its drag state on the release, before the container asks the target and drops.
    SoundDragState::end();
    ASSERT_TRUE(grid.isInterestedInDragSource(started)) << "the drop is still taken";
    grid.itemDropped(details);
    EXPECT_EQ(grid.dropTarget(), -1);

    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        if (i == 6)
            EXPECT_EQ(s.kit().pad(i).kind, PadKind::File);
        else
            EXPECT_EQ(s.kit().pad(i), before.pads[(size_t)i]) << "only the pad under the pointer changes: " << i;
    EXPECT_EQ(s.kit().pad(6).file, s.wav.getFullPathName());
    ASSERT_TRUE(s.canvas.undo.undo());
    EXPECT_EQ(s.kit().pad(6), before.pads[6]) << "one undo step";
}

TEST(DrumKitPadSoundCard, AFinderFileDropsOnThePadUnderThePointer) {
    Scene s;
    auto& grid = s.pads().grid();
    EXPECT_TRUE(grid.isInterestedInFileDrag({s.wav.getFullPathName()}));
    EXPECT_FALSE(grid.isInterestedInFileDrag({"/tmp/notes.txt"}));
    EXPECT_FALSE(grid.isInterestedInFileDrag({s.wav.getFullPathName(), s.wav.getFullPathName()})) << "one file a pad";
    const auto aimedAt = s.kit().pad(3).name;
    const auto onPad = grid.padBounds(3).getCentre();
    grid.filesDropped({s.wav.getFullPathName()}, onPad.x, onPad.y);
    EXPECT_EQ(s.pads().caption().getText(), aimedAt + " now plays " + s.wav.getFileName())
        << "the hint names the pad the user aimed at, not the file it is now named after";
    EXPECT_EQ(s.kit().pad(3).kind, PadKind::File);
    EXPECT_EQ(s.kit().pad(3).file, s.wav.getFullPathName());
}

TEST(DrumKitPadSoundCard, TheSoundRowSitsInsideThePanelWithoutOverlaps) {
    Scene s;
    s.choose(2, s.pluckTitle());
    auto& controls = s.pads().controls();
    std::vector<juce::Component*> shown;
    for (auto* c : controls.getChildren())
        if (c->isVisible() && !c->getBounds().isEmpty())
            shown.push_back(c);
    for (auto* c : shown) {
        EXPECT_TRUE(controls.getLocalBounds().contains(c->getBounds())) << c->getTitle();
        for (auto* other : shown)
            if (other != c)
                EXPECT_FALSE(c->getBounds().intersects(other->getBounds()))
                    << c->getTitle() << " / " << other->getTitle();
    }
}
