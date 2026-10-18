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
    for (const char* title : {"Attack", "Release", "Level", "Tone", "Feel", "Voices", "Pitch", "Fine"})
        EXPECT_TRUE(scene.shown(scene.knob(title))) << title;
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
    for (const char* title : {"Attack", "Release", "Tone", "Feel", "Voices"})
        EXPECT_FALSE(scene.shown(scene.knob(title))) << title;
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
