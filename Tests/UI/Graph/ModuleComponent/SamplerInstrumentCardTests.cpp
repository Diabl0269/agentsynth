// SamplerInstrumentCardTests.cpp
// The Sampler card in Instrument mode (SamplerChrome): the picker and key strip that replace the waveform and the
// Load Sample row, the mode switch that swaps the knobs, the picker list opened through the key path, the one-step
// undo of an instrument load, the multi-file drop, and the strip lighting a sounding note. Everything goes through the
// card's real components and the real mouse and key handlers.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"

using namespace cardbody_test;
using namespace sampler_test;
using synth::ui::ModMatrixPicker;
using synth::ui::SamplerChrome;

namespace {

constexpr double kRate = 44100.0;

// A pack root with one pack "keys" holding two instruments: a layered one (soft and loud) and a plain one.
struct PackRoot {
    PackRoot() {
        const auto pack = root.file("keys");
        pack.createDirectory();
        writeWav(pack.getChildFile("soft.wav"), 8820, 1, kRate, [](int, int) { return 0.2f; });
        writeWav(pack.getChildFile("loud.wav"), 8820, 1, kRate, [](int, int) { return 0.6f; });
        pack.getChildFile("grand.sfz")
            .replaceWithText("<group> lokey=48 hikey=72 pitch_keycenter=60\n"
                             "<region> sample=soft.wav lovel=1 hivel=63\n"
                             "<region> sample=loud.wav lovel=64 hivel=127\n");
        pack.getChildFile("upright.sfz").replaceWithText("<region> sample=loud.wav key=60\n");
        pack.getChildFile("pack.json")
            .replaceWithText(R"({"id":"keys","name":"Keys","instruments":[)"
                             R"({"id":"grand","name":"Grand Piano","sfz":"grand.sfz"},)"
                             R"({"id":"upright","name":"Upright Piano","sfz":"upright.sfz"}]})");
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{root.path()});
    }
    ~PackRoot() { synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt); }
    TempDir root;
};

// A Sampler on a canvas with the app's undo manager, so a load is a real undo step.
struct Scene {
    CardCanvas canvas;
    NodeID id;

    Scene() {
        id = canvas.add(std::make_unique<SamplerModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    SamplerModule& sampler() { return *dynamic_cast<SamplerModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    SamplerChrome& chrome() { return *card().getSamplerChrome(); }

    void setMode(int index) {
        auto* mode = findParameterByID(&sampler(), "playMode");
        mode->setValueNotifyingHost(mode->convertTo0to1((float)index));
        card().getCardBody()->flushPendingConditionUpdate();
    }
    int modeIndex() {
        return dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&sampler(), "playMode"))->getIndex();
    }

    juce::Slider* knob(const juce::String& title) { return child<juce::Slider>(title); }
    juce::ToggleButton* toggle(const juce::String& title) { return child<juce::ToggleButton>(title); }
    template <typename T>
    T* child(const juce::String& title) {
        for (auto* c : card().getChildren())
            if (auto* t = dynamic_cast<T*>(c); t != nullptr && t->getTitle() == title)
                return t;
        return nullptr;
    }
    bool shown(juce::Component* c) { return c != nullptr && c->isVisible(); }
};

// Opens the picker list the way a keyboard user does: Return on the focused picker. The list is handed to the test
// instead of a call-out window.
std::unique_ptr<ModMatrixPicker> openPickerByKey(Scene& scene) {
    std::unique_ptr<ModMatrixPicker> list;
    SamplerChrome::pickerHookForTest() = [&list](std::unique_ptr<ModMatrixPicker> p) { list = std::move(p); };
    scene.chrome().getPicker().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    SamplerChrome::pickerHookForTest() = nullptr;
    return list;
}

juce::File writeNamedWav(const TempDir& dir, const juce::String& name) {
    const auto file = dir.file(name);
    writeWav(file, 4410, 1, kRate, [](int, int i) { return 0.3f * std::sin(0.05f * (float)i); });
    return file;
}

} // namespace

TEST(SamplerInstrumentCard, InstrumentModeShowsThePickerAndStripAndHidesTheSampleChromeAndKnobs) {
    Scene scene;
    const auto size = scene.card().getBounds();
    auto& chrome = scene.chrome();
    EXPECT_TRUE(chrome.getLoadSampleButton().isVisible());
    EXPECT_TRUE(chrome.getWaveform().isVisible());
    EXPECT_FALSE(chrome.getPicker().isVisible());
    EXPECT_FALSE(chrome.getKeyStrip().isVisible());
    EXPECT_FALSE(scene.shown(scene.knob("Attack")));

    scene.setMode(SamplerModule::kModeInstrument);
    EXPECT_TRUE(chrome.getPicker().isVisible());
    EXPECT_TRUE(chrome.getKeyStrip().isVisible());
    EXPECT_FALSE(chrome.getLoadSampleButton().isVisible());
    EXPECT_FALSE(chrome.getWaveform().isVisible());
    for (const char* title : {"Attack", "Release", "Level", "Tone", "Feel", "Voices", "Pitch", "Fine", "Glide"})
        EXPECT_TRUE(scene.shown(scene.knob(title))) << title;
    EXPECT_TRUE(scene.shown(scene.toggle("Mono")));
    EXPECT_TRUE(chrome.getEditMapButton().isVisible());
    for (const char* title : {"Start", "End", "Root Note", "Grain Size", "Density", "Spray"})
        EXPECT_FALSE(scene.shown(scene.knob(title))) << title;
    EXPECT_TRUE(scene.shown(scene.toggle("Release Sound")));
    EXPECT_FALSE(scene.shown(scene.toggle("Loop")));
    EXPECT_FALSE(scene.shown(scene.toggle("Reverse")));
    EXPECT_EQ(scene.card().getBounds(), size) << "a mode switch never resizes the card";

    scene.setMode(SamplerModule::kModeSample);
    EXPECT_TRUE(chrome.getLoadSampleButton().isVisible());
    EXPECT_TRUE(chrome.getWaveform().isVisible());
    EXPECT_FALSE(chrome.getPicker().isVisible());
    EXPECT_FALSE(chrome.getKeyStrip().isVisible());
    for (const char* title : {"Start", "End", "Level", "Pitch", "Root Note", "Fine"})
        EXPECT_TRUE(scene.shown(scene.knob(title))) << title;
    for (const char* title : {"Attack", "Release", "Tone", "Feel", "Voices", "Glide"})
        EXPECT_FALSE(scene.shown(scene.knob(title))) << title;
    EXPECT_FALSE(scene.shown(scene.toggle("Mono")));
    EXPECT_FALSE(chrome.getEditMapButton().isVisible());
    EXPECT_TRUE(scene.shown(scene.toggle("Loop")));
    EXPECT_FALSE(scene.shown(scene.toggle("Release Sound")));
    EXPECT_EQ(scene.card().getBounds(), size);
}

TEST(SamplerInstrumentCard, ThePickerIsATabStopWithANameAndATooltip) {
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    auto& picker = scene.chrome().getPicker();
    EXPECT_TRUE(picker.getWantsKeyboardFocus());
    EXPECT_EQ(picker.getTitle(), "Instrument, none");
    EXPECT_EQ(picker.getTooltip(), "Choose an instrument (Enter)");
    const auto tabbable = scene.card().createKeyboardFocusTraverser()->getAllComponents(&scene.card());
    EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), &picker), tabbable.end());
}

TEST(SamplerInstrumentCard, TheKeyStripIsNotAFocusStopButHasAnAccessibleTitleAndATooltip) {
    PackRoot packs;
    Scene scene;
    auto& strip = scene.chrome().getKeyStrip();
    EXPECT_FALSE(strip.getWantsKeyboardFocus());
    EXPECT_FALSE(strip.getTooltip().isEmpty());
    EXPECT_EQ(strip.getTitle(), "No instrument");

    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    scene.card().timerCallback();
    EXPECT_EQ(strip.getTitle(), "2 recordings, soft and loud");
    EXPECT_EQ(scene.chrome().getCaption().getText(),
              juce::String("2 recordings ") + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " soft and loud");
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "upright"));
    scene.card().timerCallback();
    EXPECT_EQ(strip.getTitle(), "1 recording, one layer");
}

TEST(SamplerInstrumentCard, ThePickerListsPackInstrumentsAndTheFileLoaders) {
    PackRoot packs;
    Scene scene;
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getVisibleRowNamesForTest(),
              (std::vector<juce::String>{"Built in", "Grand Piano", "Upright Piano", "Your files",
                                         "Load an instrument file (.sfz)...", "Load recordings..."}));
    EXPECT_EQ(list->getVisibleItemDetailsForTest()[0], "Keys") << "the pack name sits dim under the instrument";
}

TEST(SamplerInstrumentCard, ThePickerSaysSoWhenNoInstrumentIsInstalled) {
    TempDir empty;
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{empty.path()});
    Scene scene;
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    const auto rows = list->getVisibleItemTextsForTest();
    ASSERT_GE(rows.size(), 1u);
    EXPECT_EQ(rows[0], "No instruments installed yet");
    EXPECT_FALSE(list->isVisibleItemPickableForTest(0));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(SamplerInstrumentCard, PickingAnInstrumentFromTheListLoadsItAndSelectsInstrumentMode) {
    PackRoot packs;
    Scene scene;
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    list->setSearchTextForTest("upright");
    EXPECT_EQ(list->getVisibleItemTextsForTest(), (std::vector<juce::String>{"Upright Piano"}));
    EXPECT_TRUE(list->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    list.reset();

    EXPECT_EQ(scene.sampler().getInstrumentName(), "Upright Piano");
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeInstrument);
    EXPECT_EQ(scene.chrome().getPicker().getTitle(), "Instrument, Upright Piano");
    EXPECT_TRUE(scene.chrome().getKeyStrip().isVisible());
}

TEST(SamplerInstrumentCard, ALoadIsUndoneInOneStepWithItsMode) {
    PackRoot packs;
    Scene scene;
    const auto choose = [&](const juce::String& search) {
        auto list = openPickerByKey(scene);
        ASSERT_NE(list, nullptr);
        list->setSearchTextForTest(search);
        list->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    };
    choose("grand");
    ASSERT_EQ(scene.sampler().getInstrumentName(), "Grand Piano");
    choose("upright");
    ASSERT_EQ(scene.sampler().getInstrumentName(), "Upright Piano");

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentName(), "Grand Piano");
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeInstrument);

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentName(), "") << "the first load took the card from nothing";
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeSample) << "and the mode went back with it";
    EXPECT_FALSE(scene.canvas.undo.undo()) << "two loads were two steps, no more";

    ASSERT_TRUE(scene.canvas.undo.redo());
    EXPECT_EQ(scene.sampler().getInstrumentName(), "Grand Piano");
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeInstrument);
}

TEST(SamplerInstrumentCard, DroppingSeveralNamedRecordingsBuildsAnInstrumentPlacedByTheirNotes) {
    TempDir dir;
    juce::StringArray dropped;
    for (const char* name : {"sine_C4.wav", "sine_C2.wav", "sine_C3.wav"})
        dropped.add(writeNamedWav(dir, name).getFullPathName());
    Scene scene;
    EXPECT_TRUE(scene.card().isInterestedInFileDrag(dropped));

    scene.card().fileDragEnter(dropped, 10, 10);
    EXPECT_TRUE(scene.card().isFileDragHighlighted());
    EXPECT_EQ(scene.chrome().getDragFileCount(), 3) << "the highlight carries the '3 files: ...' hint";

    scene.card().filesDropped(dropped, 10, 10);
    EXPECT_FALSE(scene.card().isFileDragHighlighted());
    EXPECT_EQ(scene.chrome().getDragFileCount(), 0);
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeInstrument);
    const auto instrument = scene.sampler().getInstrument();
    ASSERT_NE(instrument, nullptr);
    ASSERT_EQ(instrument->regions.size(), 3u);
    std::vector<int> roots;
    for (const auto& region : instrument->regions)
        roots.push_back(region.rootKey);
    std::sort(roots.begin(), roots.end());
    EXPECT_EQ(roots, (std::vector<int>{48, 60, 72})) << "C2, C3 and C4 with C3 = 60";
}

TEST(SamplerInstrumentCard, DroppingOneRecordingStaysASampleLoad) {
    TempDir dir;
    const auto file = writeNamedWav(dir, "sine_C3.wav");
    Scene scene;
    const juce::StringArray dropped{file.getFullPathName()};
    scene.card().fileDragEnter(dropped, 10, 10);
    EXPECT_EQ(scene.chrome().getDragFileCount(), 1);
    scene.card().filesDropped(dropped, 10, 10);
    EXPECT_EQ(scene.sampler().getSampleFilePath(), file.getFullPathName());
    EXPECT_NE(scene.sampler().getSample(), nullptr);
    EXPECT_EQ(scene.sampler().getInstrument(), nullptr);
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeSample);
}

TEST(SamplerInstrumentCard, DroppingAnSfzLoadsItAsTheInstrument) {
    PackRoot packs;
    const auto sfz = packs.root.file("keys/upright.sfz");
    Scene scene;
    const juce::StringArray dropped{sfz.getFullPathName()};
    EXPECT_TRUE(scene.card().isInterestedInFileDrag(dropped));
    scene.card().filesDropped(dropped, 10, 10);
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeInstrument);
    EXPECT_NE(scene.sampler().getInstrument(), nullptr);
}

TEST(SamplerInstrumentCard, AFailedLoadShowsItsMessageOnTheCaptionAndKeepsTheMode) {
    TempDir dir;
    const auto sfz = dir.file("broken.sfz");
    sfz.replaceWithText("<region> sample=missing.wav key=60\n");
    Scene scene;
    scene.card().filesDropped({sfz.getFullPathName()}, 10, 10);
    EXPECT_EQ(scene.modeIndex(), SamplerModule::kModeSample);
    EXPECT_FALSE(scene.chrome().getLoadMessages().isEmpty());
    EXPECT_TRUE(scene.chrome().getCaption().getText().startsWith(scene.chrome().getLoadMessages()[0]));
    EXPECT_FALSE(scene.chrome().getCaption().getTooltip().isEmpty());
}

TEST(SamplerInstrumentCard, AFailedRestoreShowsItsMessageOnTheCaption) {
    Scene scene;
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("instrumentPack", "missing/thing");
    scene.sampler().setExtraState(juce::var(obj.get()));
    scene.chrome().poll();
    ASSERT_FALSE(scene.chrome().getLoadMessages().isEmpty());
    EXPECT_TRUE(scene.chrome().getCaption().getText().contains("not found"));
}

// The card is built after the failed restore landed (a project opening with a pack that is not installed).
TEST(SamplerInstrumentCard, ACardBuiltAfterAFailedRestoreShowsItsMessage) {
    CardCanvas canvas;
    auto module = std::make_unique<SamplerModule>();
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("instrumentPack", "missing/thing");
    module->setExtraState(juce::var(obj.get()));
    auto* mode = findParameterByID(module.get(), "playMode");
    mode->setValueNotifyingHost(mode->convertTo0to1((float)SamplerModule::kModeInstrument));
    const auto id = canvas.add(std::move(module), 0, 0);
    canvas.editor.updateComponents();
    auto* chrome = canvas.card(id)->getSamplerChrome();
    ASSERT_NE(chrome, nullptr);
    EXPECT_TRUE(chrome->getCaption().getText().contains("not found"));
}

TEST(SamplerInstrumentCard, TheStripLightsASoundingNoteAndDimsItWhenItEnds) {
    PackRoot packs;
    Scene scene;
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "upright"));
    scene.sampler().prepareToPlay(kRate, 512);
    auto& strip = scene.chrome().getKeyStrip();
    scene.card().timerCallback();
    EXPECT_TRUE(strip.getLitNotes().none());

    juce::AudioBuffer<float> block(SamplerModule::kNumChannels, 512);
    block.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    scene.sampler().processBlock(block, midi);
    scene.card().timerCallback();
    EXPECT_TRUE(strip.getLitNotes().test(60));
    EXPECT_EQ(strip.getLitNotes().count(), 1u);

    midi.clear();
    midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
    block.clear();
    scene.sampler().processBlock(block, midi);
    scene.card().timerCallback();
    EXPECT_TRUE(strip.getLitNotes().none());
}

TEST(SamplerInstrumentCard, TheStripDrawsOneBandPerRecordingOnTheKeysItCovers) {
    PackRoot packs;
    Scene scene;
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    auto& strip = scene.chrome().getKeyStrip();
    scene.card().timerCallback();
    ASSERT_EQ(strip.getBands().size(), 2u);
    int loud = 0, soft = 0;
    for (const auto& band : strip.getBands()) {
        EXPECT_EQ(band.lowKey, 48);
        EXPECT_EQ(band.highKey, 72);
        EXPECT_FALSE(band.single);
        (band.loud ? loud : soft)++;
    }
    EXPECT_EQ(loud, 1);
    EXPECT_EQ(soft, 1);
    EXPECT_EQ(strip.getKeySpan(), juce::Range<int>(48, 72));
}

// ---- The instrument map window ----

namespace {

using synth::ui::MapDrop;
using synth::ui::SamplerMapFields;
using synth::ui::SamplerMapView;

// Opens the map the way the Edit map button does; the window is handed to the test as a view instead.
SamplerMapView* openMap(Scene& scene, std::unique_ptr<SamplerMapView>& holder) {
    synth::ui::test_hooks::samplerMapWindowHookForTest() = [&holder](std::unique_ptr<SamplerMapView> view) {
        holder = std::move(view);
    };
    scene.chrome().getEditMapButton().onClick();
    synth::ui::test_hooks::samplerMapWindowHookForTest() = nullptr;
    return holder.get();
}

// An instrument of three recordings on C2, C3 and C4 (one velocity layer) the module owns.
void loadThreeRecordings(Scene& scene, const TempDir& dir) {
    scene.setMode(SamplerModule::kModeInstrument);
    juce::StringArray files;
    for (const char* name : {"sine_C2.wav", "sine_C3.wav", "sine_C4.wav"})
        files.add(writeNamedWav(dir, name).getFullPathName());
    ASSERT_TRUE(scene.sampler().loadInstrumentFromFiles(files));
    scene.card().timerCallback();
}

// The x just past the block that ends on `lastKey`.
int blocksRightEdge(const synth::ui::SamplerMapBlocks& blocks, int lastKey) {
    const float keyWidth = (float)(blocks.getWidth() - 92 - 4) / (float)(blocks.getKeySpan().getLength() + 1);
    return 92 + (int)((float)(lastKey - blocks.getKeySpan().getStart() + 1) * keyWidth) - 1;
}

int regionWithRoot(const SamplerModule& sampler, int root) {
    const auto& regions = sampler.getInstrumentRegions();
    for (int i = 0; i < (int)regions.size(); ++i)
        if (regions[(size_t)i].rootKey == root)
            return i;
    return -1;
}

} // namespace

TEST(SamplerMapWindow, OpensFromTheEditMapButton) {
    PackRoot packs;
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    std::unique_ptr<SamplerMapView> view;
    ASSERT_NE(openMap(scene, view), nullptr);
    EXPECT_EQ(scene.chrome().getMapView(), view.get());
    EXPECT_EQ(view->getWindowTitle(), "Grand Piano - map");

    std::unique_ptr<SamplerMapView> again;
    EXPECT_EQ(openMap(scene, again), nullptr) << "one map per card";
}

TEST(SamplerMapWindow, ADoubleClickOnTheStripOpensTheMap) {
    PackRoot packs;
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    std::unique_ptr<SamplerMapView> view;
    synth::ui::test_hooks::samplerMapWindowHookForTest() = [&view](std::unique_ptr<SamplerMapView> v) {
        view = std::move(v);
    };
    auto& strip = scene.chrome().getKeyStrip();
    strip.mouseDoubleClick(cardbody_test::mouseAt(strip, {5.0f, 5.0f}, juce::ModifierKeys(), 2));
    synth::ui::test_hooks::samplerMapWindowHookForTest() = nullptr;
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(scene.chrome().getMapView(), view.get());
}

TEST(SamplerMapWindow, ShowsOneRowPerVelocityLayerAndABlockPerRecordingOnItsKeys) {
    PackRoot packs;
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    std::unique_ptr<SamplerMapView> holder;
    auto& blocks = openMap(scene, holder)->getBlocks();
    EXPECT_EQ(blocks.getRowCount(), 2);
    ASSERT_EQ(blocks.getBlockCount(), 2);
    EXPECT_LE(blocks.getKeySpan().getStart(), 48);
    EXPECT_GE(blocks.getKeySpan().getEnd(), 72);
    // Soft (index 0, velocity 1-63) sits under the loud one and both span the same keys.
    auto* soft = blocks.getBlock(0);
    auto* loud = blocks.getBlock(1);
    EXPECT_GT(soft->getY(), loud->getY());
    EXPECT_EQ(soft->getX(), loud->getX());
    EXPECT_EQ(soft->getWidth(), loud->getWidth());
    EXPECT_EQ(blocks.keyAt(soft->getX() + 1), 48);
    EXPECT_EQ(blocks.keyAt(soft->getRight() - 1), 72);
}

TEST(SamplerMapWindow, ClickingABlockShowsItsFields) {
    TempDir dir;
    Scene scene;
    loadThreeRecordings(scene, dir);
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    auto& fields = view->getFields();
    EXPECT_EQ(view->getBlocks().getSelected(), -1);
    const int c3 = regionWithRoot(scene.sampler(), 60);
    ASSERT_GE(c3, 0);
    view->getBlocks().getBlock(c3)->onClick();
    EXPECT_EQ(view->getBlocks().getSelected(), c3);
    const auto& region = scene.sampler().getInstrumentRegions()[(size_t)c3];
    EXPECT_EQ((int)fields.getField(SamplerMapFields::LowKey).getValue(), region.loKey);
    EXPECT_EQ((int)fields.getField(SamplerMapFields::HighKey).getValue(), region.hiKey);
    EXPECT_EQ((int)fields.getField(SamplerMapFields::RootKey).getValue(), 60);
    EXPECT_EQ((int)fields.getField(SamplerMapFields::LowVelocity).getValue(), 1);
    EXPECT_EQ((int)fields.getField(SamplerMapFields::HighVelocity).getValue(), 127);
    EXPECT_EQ(fields.getFileLabel().getText(), "sine_C3.wav");
    EXPECT_EQ(fields.getField(SamplerMapFields::RootKey).getTextFromValue(60), "C3");
}

TEST(SamplerMapWindow, AFieldEditChangesTheRegionsAndIsOneUndoStep) {
    TempDir dir;
    Scene scene;
    loadThreeRecordings(scene, dir);
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    const int c3 = regionWithRoot(scene.sampler(), 60);
    view->getBlocks().getBlock(c3)->onClick();
    const int oldHigh = scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey;

    view->getFields().getField(SamplerMapFields::HighKey).setValue(oldHigh - 3, juce::sendNotificationSync);
    view->getFields().getField(SamplerMapFields::Volume).setValue(-6.0, juce::sendNotificationSync);
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh - 3);
    EXPECT_FLOAT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].volumeDb, -6.0f);
    EXPECT_EQ(view->getBlocks().getBlock(c3)->getRight(), blocksRightEdge(view->getBlocks(), oldHigh - 3));

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_FLOAT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].volumeDb, 0.0f) << "one edit per step";
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh - 3);
    view->poll();
    EXPECT_NEAR(view->getFields().getField(SamplerMapFields::Volume).getValue(), 0.0, 1.0e-9)
        << "the open map follows the undo";
    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh);
    ASSERT_TRUE(scene.canvas.undo.redo());
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh - 3);
}

TEST(SamplerMapWindow, ALowKeyAboveTheHighKeyPullsTheHighKeyAlong) {
    TempDir dir;
    Scene scene;
    loadThreeRecordings(scene, dir);
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    const int c3 = regionWithRoot(scene.sampler(), 60);
    view->getBlocks().getBlock(c3)->onClick();
    view->getFields().getField(SamplerMapFields::LowKey).setValue(120, juce::sendNotificationSync);
    const auto& region = scene.sampler().getInstrumentRegions()[(size_t)c3];
    EXPECT_EQ(region.loKey, 120);
    EXPECT_GE(region.hiKey, 120);
}

TEST(SamplerMapWindow, ArrowKeysMoveBetweenBlocksAndEnterOpensTheFields) {
    PackRoot packs;
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    auto& blocks = view->getBlocks();
    ASSERT_EQ(blocks.getRowCount(), 2);
    EXPECT_TRUE(blocks.getWantsKeyboardFocus());
    const auto press = [&](int code) { return blocks.keyPressed(juce::KeyPress(code)); };

    EXPECT_TRUE(press(juce::KeyPress::downKey)) << "the first arrow selects the first block";
    const int first = blocks.getSelected();
    ASSERT_GE(first, 0);
    EXPECT_TRUE(press(juce::KeyPress::downKey));
    EXPECT_NE(blocks.getSelected(), first) << "Down goes to the other layer";
    EXPECT_TRUE(press(juce::KeyPress::upKey));
    EXPECT_EQ(blocks.getSelected(), first);
    EXPECT_TRUE(press(juce::KeyPress::rightKey));
    EXPECT_EQ(blocks.getSelected(), first) << "one block per row here: the edge stops";
    EXPECT_EQ(blocks.getTitle().startsWith("Instrument map, "), true) << "the selection is announced";

    int opened = 0;
    const auto original = blocks.onOpenFields;
    blocks.onOpenFields = [&] {
        ++opened;
        original();
    };
    EXPECT_TRUE(press(juce::KeyPress::returnKey));
    EXPECT_EQ(opened, 1);
    EXPECT_FALSE(blocks.keyPressed(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)))
        << "Cmd+Z and friends pass through to the app";
}

TEST(SamplerMapWindow, ADropOnABlockReplacesItAndADropOnAKeyAddsARecording) {
    TempDir dir;
    Scene scene;
    loadThreeRecordings(scene, dir);
    const auto replacement = writeNamedWav(dir, "other.wav");
    const auto added = writeNamedWav(dir, "extra.wav");
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    auto& blocks = view->getBlocks();
    EXPECT_TRUE(blocks.isInterestedInFileDrag({replacement.getFullPathName()}));
    EXPECT_FALSE(blocks.isInterestedInFileDrag({dir.file("notes.txt").getFullPathName()}));

    const int c3 = regionWithRoot(scene.sampler(), 60);
    const auto centre = blocks.getBlock(c3)->getBounds().getCentre();
    blocks.filesDropped({replacement.getFullPathName()}, centre.x, centre.y);
    ASSERT_EQ(scene.sampler().getInstrumentRegions().size(), 3u);
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].samplePath, replacement.getFullPathName());
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].rootKey, 60) << "its keys stay";

    // On the keyboard itself, above the rows: a new recording on that key.
    const int key = 66;
    const float keyWidth = (float)(blocks.getWidth() - 92 - 4) / (float)(blocks.getKeySpan().getLength() + 1);
    const int x = 92 + (int)((float)(key - blocks.getKeySpan().getStart() + 0.5f) * keyWidth);
    ASSERT_EQ(blocks.keyAt(x), key);
    blocks.filesDropped({added.getFullPathName()}, x, 10);
    ASSERT_EQ(scene.sampler().getInstrumentRegions().size(), 4u);
    const auto& region = scene.sampler().getInstrumentRegions().back();
    EXPECT_EQ(region.samplePath, added.getFullPathName());
    EXPECT_EQ(region.loKey, key);
    EXPECT_EQ(region.hiKey, key);
    EXPECT_EQ(view->getBlocks().getBlockCount(), 4);

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentRegions().size(), 3u) << "the add was one step";
    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].samplePath, dir.file("sine_C3.wav").getFullPathName());
}

TEST(SamplerMapWindow, ABuiltInInstrumentIsReadOnlyUntilTheFirstEditMakesACopy) {
    PackRoot packs;
    Scene scene;
    scene.setMode(SamplerModule::kModeInstrument);
    ASSERT_TRUE(scene.sampler().loadInstrumentFromPack("keys", "grand"));
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    EXPECT_TRUE(view->isReadOnly());
    EXPECT_TRUE(view->getCopyButton().isVisible());
    view->getBlocks().getBlock(0)->onClick();
    for (int f = 0; f < SamplerMapFields::NumFields; ++f)
        EXPECT_FALSE(view->getFields().getField((SamplerMapFields::Field)f).isEnabled());
    EXPECT_FALSE(view->getBlocks().isInterestedInFileDrag({packs.root.file("keys/soft.wav").getFullPathName()}));
    EXPECT_FALSE(view->getAddButton().isEnabled());
    const auto blocksBefore = view->getBlocks().getBlockCount();
    view->getBlocks().filesDropped({packs.root.file("keys/soft.wav").getFullPathName()}, 120, 10);
    EXPECT_EQ(scene.sampler().getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Pack);
    EXPECT_EQ(view->getBlocks().getBlockCount(), blocksBefore);

    view->getCopyButton().onClick();
    EXPECT_EQ(scene.sampler().getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Files);
    EXPECT_FALSE(view->isReadOnly());
    EXPECT_TRUE(view->getFields().getField(SamplerMapFields::LowKey).isEnabled());
    EXPECT_TRUE(view->getAddButton().isEnabled());
    EXPECT_EQ(scene.sampler().getInstrumentName(), "Grand Piano");

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentKind(), synth::sampler::InstrumentSource::Kind::Pack);
    view->poll();
    EXPECT_TRUE(view->isReadOnly()) << "undo of the copy is a built-in instrument again";
}

TEST(SamplerMapWindow, EveryControlHasAScreenReaderNameAndATooltip) {
    TempDir dir;
    Scene scene;
    loadThreeRecordings(scene, dir);
    std::unique_ptr<SamplerMapView> holder;
    auto* view = openMap(scene, holder);
    auto& edit = scene.chrome().getEditMapButton();
    EXPECT_EQ(edit.getTitle(), "Edit map");
    EXPECT_FALSE(edit.getTooltip().isEmpty());
    EXPECT_FALSE(view->getBlocks().getTitle().isEmpty());
    EXPECT_FALSE(view->getBlocks().getTooltip().isEmpty());
    for (int i = 0; i < view->getBlocks().getBlockCount(); ++i) {
        EXPECT_TRUE(view->getBlocks().getBlock(i)->getTitle().contains("keys")) << i;
        EXPECT_FALSE(view->getBlocks().getBlock(i)->getTooltip().isEmpty()) << i;
    }
    for (int f = 0; f < SamplerMapFields::NumFields; ++f) {
        auto& field = view->getFields().getField((SamplerMapFields::Field)f);
        EXPECT_FALSE(field.getTitle().isEmpty()) << f;
        EXPECT_FALSE(field.getTooltip().isEmpty()) << f;
    }
    for (juce::Button* b : {(juce::Button*)&view->getCopyButton(), (juce::Button*)&view->getAddButton(),
                            (juce::Button*)&view->getDoneButton(), (juce::Button*)&view->getFields().getReplaceButton(),
                            (juce::Button*)&view->getFields().getRemoveButton()}) {
        EXPECT_FALSE(b->getTitle().isEmpty()) << b->getButtonText();
        EXPECT_FALSE(b->getTooltip().isEmpty()) << b->getButtonText();
    }
}

TEST(SamplerMapWindow, ACardThatGoesAwayTakesItsMapViewDown) {
    TempDir dir;
    std::unique_ptr<SamplerMapView> holder;
    {
        Scene scene;
        loadThreeRecordings(scene, dir);
        ASSERT_NE(openMap(scene, holder), nullptr);
    }
    // The view outlived the card: it lets go of the module instead of reading a dead one.
    holder->poll();
    EXPECT_EQ(holder->getBlocks().getBlockCount(), 0);
}
