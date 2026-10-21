// DrumKitCardTests.cpp
// The Drum Kit card with a recorded kit (DrumKitChrome): the instrument picker above the body, its list (drum kits
// only, plus "Synth drums"), the Recorded switches that are on the card only while a kit is loaded, the Decay knobs
// that grey out while their drums play recordings, the one-step undo of a load and a missing pack on the caption.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using namespace sampler_test;
using synth::ui::DrumKitChrome;
using synth::ui::InstrumentPickerRow;
using synth::ui::ModMatrixPicker;

namespace {

constexpr double kRate = 44100.0;

// A pack root with the drum kit "acoustic-kit" (category Drums) and a piano that must not be offered to a drum kit.
struct PackRoot {
    PackRoot() {
        const auto pack = root.file("core");
        pack.createDirectory();
        writeWav(pack.getChildFile("kick.wav"), 8820, 1, kRate, [](int, int) { return 0.5f; });
        writeWav(pack.getChildFile("keys.wav"), 8820, 1, kRate, [](int, int) { return 0.3f; });
        pack.getChildFile("kit.sfz").replaceWithText("<region> sample=kick.wav lokey=35 hikey=36 pitch_keycenter=36\n");
        pack.getChildFile("piano.sfz").replaceWithText("<region> sample=keys.wav key=60\n");
        pack.getChildFile("pack.json")
            .replaceWithText(R"({"id":"core","name":"Core","instruments":[)"
                             R"({"id":"acoustic-kit","name":"Acoustic Kit","sfz":"kit.sfz","category":"Drums"},)"
                             R"({"id":"grand","name":"Grand Piano","sfz":"piano.sfz","category":"Keys"}]})");
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{root.path()});
    }
    ~PackRoot() { synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt); }
    TempDir root;
};

struct Scene {
    CardCanvas canvas;
    NodeID id;

    Scene() {
        id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(id)); }
    ModuleComponent& card() { return *canvas.card(id); }
    DrumKitChrome& chrome() { return *dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome()); }
    void settle() { card().getCardBody()->flushPendingConditionUpdate(); }

    template <typename T>
    T* child(const juce::String& title) {
        for (auto* c : card().getChildren())
            if (auto* t = dynamic_cast<T*>(c); t != nullptr && t->getTitle() == title)
                return t;
        return nullptr;
    }
    juce::ToggleButton* recorded(const juce::String& group) { return child<juce::ToggleButton>(group + " recorded"); }
    bool shown(juce::Component* c) { return c != nullptr && c->isVisible(); }
};

std::unique_ptr<ModMatrixPicker> openPickerByKey(Scene& scene) {
    std::unique_ptr<ModMatrixPicker> list;
    InstrumentPickerRow::pickerHookForTest() = [&list](std::unique_ptr<ModMatrixPicker> p) { list = std::move(p); };
    scene.chrome().getRow().picker().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    InstrumentPickerRow::pickerHookForTest() = nullptr;
    return list;
}

void choose(Scene& scene, const juce::String& search) {
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    list->setSearchTextForTest(search);
    list->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
}

} // namespace

TEST(DrumKitCard, OnlyADrumKitCardGetsTheInstrumentChrome) {
    Scene scene;
    ASSERT_NE(dynamic_cast<DrumKitChrome*>(scene.card().getInstrumentChrome()), nullptr);
    CardCanvas other;
    const auto id = other.add(std::make_unique<OscillatorModule>(), 0, 0);
    other.editor.updateComponents();
    EXPECT_EQ(other.card(id)->getInstrumentChrome(), nullptr);
}

TEST(DrumKitCard, ThePickerIsATabStopWithANameAndATooltipAndSaysSynthDrumsWhenEmpty) {
    Scene scene;
    auto& picker = scene.chrome().getRow().picker();
    EXPECT_TRUE(picker.isVisible());
    EXPECT_TRUE(picker.getWantsKeyboardFocus());
    EXPECT_EQ(picker.getTitle(), "Instrument, synth drums");
    EXPECT_EQ(picker.getTooltip(), "Choose an instrument (Enter)");
    const auto tabbable = scene.card().createKeyboardFocusTraverser()->getAllComponents(&scene.card());
    EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), &picker), tabbable.end());
    EXPECT_TRUE(scene.chrome().getRow().caption().isVisible());
    EXPECT_FALSE(scene.chrome().getRow().caption().getText().isEmpty());
    EXPECT_TRUE(scene.card().getLocalBounds().contains(picker.getBounds()));
}

TEST(DrumKitCard, TheListOffersDrumKitsOnlyPlusSynthDrumsAndTheFileLoaders) {
    PackRoot packs;
    Scene scene;
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getVisibleRowNamesForTest(),
              (std::vector<juce::String>{"Built in", "Synth drums", "Acoustic Kit", "Your files",
                                         "Load an instrument file (.sfz)...", "Load recordings..."}));
}

TEST(DrumKitCard, TheListSaysSoWhenNoDrumKitIsInstalled) {
    TempDir empty;
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{empty.path()});
    Scene scene;
    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    const auto rows = list->getVisibleItemTextsForTest();
    ASSERT_GE(rows.size(), 2u);
    EXPECT_EQ(rows[0], "Synth drums");
    EXPECT_EQ(rows[1], "No drums installed yet");
    EXPECT_FALSE(list->isVisibleItemPickableForTest(1));
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(DrumKitCard, PickingAKitLoadsItAndPickingSynthDrumsClearsIt) {
    PackRoot packs;
    Scene scene;
    choose(scene, "acoustic");
    EXPECT_EQ(scene.kit().getInstrumentName(), "Acoustic Kit");
    EXPECT_TRUE(scene.kit().hasRecordedKit());
    EXPECT_EQ(scene.chrome().getRow().picker().getTitle(), "Instrument, Acoustic Kit");

    auto list = openPickerByKey(scene);
    ASSERT_NE(list, nullptr);
    list->setSearchTextForTest("synth");
    EXPECT_EQ(list->getVisibleItemTextsForTest(), (std::vector<juce::String>{"Synth drums"}));
    list->sendKeyForTest(juce::KeyPress(juce::KeyPress::returnKey));
    list.reset();
    EXPECT_FALSE(scene.kit().hasRecordedKit());
    EXPECT_EQ(scene.kit().getInstrumentName(), "");
    EXPECT_EQ(scene.chrome().getRow().picker().getTitle(), "Instrument, synth drums");
}

TEST(DrumKitCard, EachKitLoadAndEachClearIsOneUndoStep) {
    PackRoot packs;
    Scene scene;
    choose(scene, "acoustic");
    ASSERT_TRUE(scene.kit().hasRecordedKit());
    choose(scene, "synth");
    ASSERT_FALSE(scene.kit().hasRecordedKit());

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.kit().getInstrumentName(), "Acoustic Kit") << "the clear is undone alone";
    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_FALSE(scene.kit().hasRecordedKit()) << "and the load is the step before it";
    EXPECT_FALSE(scene.canvas.undo.undo());
    ASSERT_TRUE(scene.canvas.undo.redo());
    EXPECT_TRUE(scene.kit().hasRecordedKit());
}

TEST(DrumKitCard, TheRecordedSwitchesAreOnTheCardOnlyWhileAKitIsLoaded) {
    PackRoot packs;
    Scene scene;
    const auto size = scene.card().getBounds();
    for (const char* group : {"Kick", "Snare", "Clap", "Hat", "Tom", "Cymbal", "Cowbell"})
        ASSERT_NE(scene.recorded(group), nullptr) << group << " has a switch on the card";
    EXPECT_FALSE(scene.shown(scene.recorded("Kick"))) << "no kit, no switch";

    choose(scene, "acoustic");
    scene.settle();
    EXPECT_TRUE(scene.shown(scene.recorded("Kick")));
    EXPECT_EQ(scene.card().getBounds(), size) << "a kit never resizes the card";

    scene.kit().clearInstrument();
    scene.card().timerCallback();
    scene.settle();
    EXPECT_FALSE(scene.shown(scene.recorded("Kick")));
}

TEST(DrumKitCard, EveryNewControlHasAnAccessibleNameAndATooltip) {
    PackRoot packs;
    Scene scene;
    choose(scene, "acoustic");
    scene.settle();
    for (const char* group : {"Kick", "Snare", "Clap", "Hat", "Tom", "Cymbal", "Cowbell"}) {
        auto* toggle = scene.recorded(group);
        ASSERT_NE(toggle, nullptr) << group;
        EXPECT_TRUE(toggle->getWantsKeyboardFocus()) << group;
        EXPECT_FALSE(toggle->getTooltip().isEmpty()) << group;
        EXPECT_FALSE(toggle->getTitle().isEmpty()) << group;
    }
    auto& row = scene.chrome().getRow();
    EXPECT_FALSE(row.picker().getTitle().isEmpty());
    EXPECT_FALSE(row.picker().getTooltip().isEmpty());
}

TEST(DrumKitCard, ADecayKnobGreysOutWhileItsDrumsPlayRecordings) {
    PackRoot packs;
    Scene scene;
    auto* decay = scene.child<juce::Slider>("Kick decay");
    ASSERT_NE(decay, nullptr);
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*decay));

    choose(scene, "acoustic");
    scene.settle();
    EXPECT_TRUE(synth::theme::AppLookAndFeel::paintsDimmed(*decay)) << "the kick plays its recording";
    EXPECT_TRUE(decay->isEnabled()) << "it stays operable, like every dimmed control";

    auto* recorded = findParameterByID(&scene.kit(), "kickRecorded");
    recorded->setValueNotifyingHost(0.0f);
    scene.settle();
    EXPECT_FALSE(synth::theme::AppLookAndFeel::paintsDimmed(*decay)) << "Recorded off: the synth kick uses it";
}

TEST(DrumKitCard, AMissingPackShowsItsMessageOnTheCaptionAndKeepsTheReference) {
    TempDir empty;
    synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{empty.path()});
    {
        Scene scene;
        auto* state = new juce::DynamicObject();
        state->setProperty("instrumentPack", "core/acoustic-kit");
        scene.kit().setExtraState(juce::var(state));
        scene.card().timerCallback();
        const auto& caption = scene.chrome().getRow().caption();
        EXPECT_TRUE(caption.getText().contains("core/acoustic-kit")) << caption.getText();
        EXPECT_TRUE(caption.getText().contains("not found")) << caption.getText();
        EXPECT_EQ(scene.kit().getExtraState()["instrumentPack"].toString(), "core/acoustic-kit");
        EXPECT_FALSE(scene.shown(scene.recorded("Kick")));
    }
    synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt);
}

TEST(DrumKitCard, TheCardSizeMatchesItsEstimate) {
    Scene scene;
    const auto estimate = GraphEditor::estimateModuleSize("Drum Kit");
    EXPECT_EQ(estimate.x, scene.card().getWidth());
    EXPECT_EQ(estimate.y, scene.card().getHeight());
}

TEST(DrumKitCard, TheDrumMachineButtonIsDisabledWithAReasonWhenNoTrackPlaysTheKit) {
    Scene scene;
    auto& button = scene.chrome().getDrumMachineButton();
    scene.chrome().refreshDrumMachineButton();
    EXPECT_TRUE(button.isVisible());
    EXPECT_EQ(button.getTitle(), "Drum machine");
    EXPECT_FALSE(button.isEnabled());
    EXPECT_TRUE(button.getTooltip().contains("No track plays this Drum Kit")) << button.getTooltip();

    scene.canvas.editor.drumKitHasTrack = [](NodeID) { return false; };
    scene.chrome().refreshDrumMachineButton();
    EXPECT_FALSE(button.isEnabled());
}

TEST(DrumKitCard, TheDrumMachineButtonOpensTheGridForThisKitWhenATrackPlaysIt) {
    Scene scene;
    NodeID opened;
    scene.canvas.editor.drumKitHasTrack = [](NodeID) { return true; };
    scene.canvas.editor.onOpenDrumMachineRequested = [&opened](NodeID n) { opened = n; };
    scene.chrome().refreshDrumMachineButton();
    auto& button = scene.chrome().getDrumMachineButton();
    EXPECT_TRUE(button.isEnabled());
    EXPECT_FALSE(button.getTooltip().isEmpty());
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    button.onClick();
    EXPECT_EQ(opened, scene.id);
}
