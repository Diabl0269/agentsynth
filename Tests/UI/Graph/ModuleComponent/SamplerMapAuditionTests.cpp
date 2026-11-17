// SamplerMapAuditionTests.cpp
// The instrument map window's keyboard: a click or drag on its keys plays the Sampler through its own note inbox
// (heard by running the module's processBlock), the keys light while they sound, P plays the selected recording, a
// held note is released when the view goes, and the app's shortcuts (Cmd+Z) pass through the window to the app.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../../Layout/PianoKeyShapeProbe.h"
#include "../CardBody/CardCanvas.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerKeyStrip.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"
#include "UI/Layout/PianoKeyShape.h"

using namespace cardbody_test;
using namespace sampler_test;
using synth::ui::SamplerChrome;
using synth::ui::SamplerKeyStrip;
using synth::ui::SamplerMapFields;
using synth::ui::SamplerMapView;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;
constexpr int kLabelWidth = 92; // the map's key area starts after the row labels
constexpr int kMargin = 4;

struct Scene {
    CardCanvas canvas;
    NodeID id;
    TempDir dir;
    std::unique_ptr<SamplerMapView> holder;

    Scene() {
        id = canvas.add(std::make_unique<SamplerModule>(), 0, 0);
        canvas.editor.updateComponents();
        auto* mode = findParameterByID(&sampler(), "playMode");
        mode->setValueNotifyingHost(mode->convertTo0to1((float)SamplerModule::kModeInstrument));
        // Six seconds of sine per recording, so a held note is still sounding when the test looks.
        juce::StringArray files;
        for (const char* name : {"sine_C2.wav", "sine_C3.wav", "sine_C4.wav"}) {
            const auto file = dir.file(name);
            writeWav(file, (int)(6 * kRate), 1, kRate, [](int, int i) { return 0.4f * std::sin(0.05f * (float)i); });
            files.add(file.getFullPathName());
        }
        EXPECT_TRUE(sampler().loadInstrumentFromFiles(files));
        setParam("release", 0.05f);
        setParam("feel", 0.0f);
        sampler().prepareToPlay(kRate, kBlock);
        canvas.editor.updateComponents();
        synth::ui::test_hooks::samplerMapWindowHookForTest() = [this](std::unique_ptr<SamplerMapView> v) {
            holder = std::move(v);
        };
        chrome().getEditMapButton().onClick();
        synth::ui::test_hooks::samplerMapWindowHookForTest() = nullptr;
    }

    SamplerModule& sampler() { return *dynamic_cast<SamplerModule*>(canvas.processor(id)); }
    SamplerChrome& chrome() { return *dynamic_cast<SamplerChrome*>(canvas.card(id)->getInstrumentChrome()); }
    SamplerMapView& view() { return *holder; }
    synth::ui::SamplerMapBlocks& blocks() { return holder->getBlocks(); }

    void setParam(const juce::String& paramId, float value) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&sampler(), paramId));
        ASSERT_NE(p, nullptr) << paramId;
        p->setValueNotifyingHost(p->convertTo0to1(value));
    }

    // Runs `count` blocks of the module's audio and returns the loudest sample of the last one.
    float run(int count) {
        float peak = 0.0f;
        for (int b = 0; b < count; ++b) {
            juce::AudioBuffer<float> buffer(SamplerModule::kNumChannels, kBlock);
            buffer.clear();
            juce::MidiBuffer midi;
            sampler().processBlock(buffer, midi);
            peak = buffer.getMagnitude(0, kBlock);
        }
        return peak;
    }

    juce::Point<float> keyCentre(int note, float down = 0.5f) {
        auto& b = blocks();
        const float keyWidth = (float)(b.getWidth() - kLabelWidth - kMargin) / (float)(b.getKeySpan().getLength() + 1);
        return {(float)kLabelWidth + (float)(note - b.getKeySpan().getStart() + 0.5f) * keyWidth,
                down * 46.0f}; // the keys are 46 px tall
    }
    void press(int note, float down = 0.5f) {
        blocks().mouseDown(
            mouseAt(blocks(), keyCentre(note, down), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    }
    void dragTo(int note) {
        blocks().mouseDrag(
            mouseAt(blocks(), keyCentre(note), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
    }
    void release(int note) { blocks().mouseUp(mouseAt(blocks(), keyCentre(note), juce::ModifierKeys())); }
    bool lit(int note) {
        juce::DynamicObject state;
        blocks().addTestModeState(state);
        return juce::StringArray::fromTokens(state.getProperty("litKeys").toString(), ",", "")
            .contains(juce::String(note));
    }
    int regionWithRoot(int root) {
        const auto& regions = sampler().getInstrumentRegions();
        for (int i = 0; i < (int)regions.size(); ++i)
            if (regions[(size_t)i].rootKey == root)
                return i;
        return -1;
    }
};

// Offers `key` to `from` and then each parent in turn, as the key travels up from a focused control.
bool bubble(juce::Component* from, const juce::KeyPress& key) {
    for (auto* c = from; c != nullptr; c = c->getParentComponent())
        if (c->keyPressed(key))
            return true;
    return false;
}

const juce::KeyPress kUndoKey('z', juce::ModifierKeys::commandModifier, 0);

} // namespace

TEST(SamplerMapAudition, PressingAKeyPlaysItAndLettingGoEndsIt) {
    Scene scene;
    ASSERT_NE(scene.holder, nullptr);
    EXPECT_EQ(scene.run(2), 0.0f);
    scene.press(60);
    EXPECT_EQ(scene.blocks().getHeldKey(), 60);
    EXPECT_GT(scene.run(4), 0.01f) << "the note-on reached the Sampler";
    EXPECT_TRUE(scene.sampler().getSoundingNotes().test(60));
    scene.release(60);
    EXPECT_EQ(scene.blocks().getHeldKey(), -1);
    EXPECT_EQ(scene.run(40), 0.0f) << "the note-off reached it and the tail ended";
    EXPECT_FALSE(scene.sampler().getSoundingNotes().test(60));
    scene.press(60);
    EXPECT_GT(scene.run(4), 0.01f) << "the same key pressed again sounds again";
    scene.release(60);
}

TEST(SamplerMapAudition, ALouderPressNearTheFrontOfTheKeyPlaysLouder) {
    const auto peakAt = [](float down) {
        Scene scene;
        scene.setParam("feel", 1.0f); // the Scene's 0 makes every velocity equally loud
        scene.press(60, down);
        return scene.run(4);
    };
    const float soft = peakAt(0.0f);
    EXPECT_GT(soft, 0.0f);
    EXPECT_GT(peakAt(0.95f), soft * 1.3f);
}

TEST(SamplerMapAudition, DraggingAcrossKeysReleasesTheOldNoteAndPlaysTheNew) {
    Scene scene;
    scene.press(60);
    scene.run(4);
    scene.dragTo(62);
    EXPECT_EQ(scene.blocks().getHeldKey(), 62);
    scene.run(40);
    EXPECT_FALSE(scene.sampler().getSoundingNotes().test(60));
    EXPECT_TRUE(scene.sampler().getSoundingNotes().test(62));
    scene.release(62);
    EXPECT_EQ(scene.run(40), 0.0f);
}

TEST(SamplerMapAudition, ClickingAKeyDoesNotChangeTheSelection) {
    Scene scene;
    scene.blocks().getBlock(scene.regionWithRoot(60))->onClick();
    const int before = scene.blocks().getSelected();
    scene.press(48);
    scene.release(48);
    EXPECT_EQ(scene.blocks().getSelected(), before);
}

TEST(SamplerMapAudition, ClosingTheViewWhileAKeyIsHeldReleasesTheNote) {
    Scene scene;
    scene.press(60);
    ASSERT_GT(scene.run(4), 0.01f);
    scene.holder.reset();
    EXPECT_EQ(scene.run(40), 0.0f) << "no stuck note";
}

TEST(SamplerMapAudition, DetachingTheViewWhileAKeyIsHeldReleasesTheNoteAndLaterKeysPlayNothing) {
    Scene scene;
    scene.press(60);
    ASSERT_GT(scene.run(4), 0.01f);
    scene.view().detach();
    EXPECT_EQ(scene.run(40), 0.0f);
    scene.press(64);
    EXPECT_EQ(scene.run(4), 0.0f);
}

TEST(SamplerMapAudition, ABypassedSamplerStaysSilentWhenAKeyIsPressed) {
    Scene scene;
    scene.setParam("bypassed", 1.0f);
    scene.press(60);
    EXPECT_EQ(scene.run(6), 0.0f);
}

TEST(SamplerMapAudition, ThePressedKeyIsLitAfterAPollAndDimsWhenItEnds) {
    Scene scene;
    scene.press(60);
    scene.view().poll();
    EXPECT_TRUE(scene.lit(60));
    juce::DynamicObject state;
    scene.blocks().addTestModeState(state);
    EXPECT_EQ(state.getProperty("heldKey"), juce::var(60));
    scene.run(2);
    scene.release(60);
    scene.run(40);
    scene.view().poll();
    EXPECT_FALSE(scene.lit(60));
}

// Bug kind: motion-over-time. A 4 s release keeps the note in the sounding set for seconds; the key follows the press
// and release instead, going dark as soon as the tail is no longer clearly audible.
TEST(SamplerMapAudition, AKeyDimsShortlyAfterLetGoEvenWithALongRelease) {
    Scene scene;
    scene.setParam("release", 4.0f);
    scene.press(60);
    scene.run(4);
    scene.view().poll();
    ASSERT_TRUE(scene.lit(60));
    scene.release(60);
    scene.run(350); // about 2 s: past the -30 dB point (1.5 s) of the 4 s fade-out, which is still running
    scene.view().poll();
    EXPECT_TRUE(scene.sampler().getSoundingNotes().test(60)) << "still fading, so the module reports it sounding";
    EXPECT_FALSE(scene.lit(60)) << "the key is dark";
}

TEST(SamplerMapAudition, PPlaysTheSelectedRecordingsRootKeyAndLettingGoEndsIt) {
    Scene scene;
    auto& blocks = scene.blocks();
    const juce::KeyPress p('p', juce::ModifierKeys::noModifiers, 0);
    EXPECT_FALSE(blocks.keyPressed(p)) << "nothing is selected: P does nothing";
    scene.blocks().getBlock(scene.regionWithRoot(60))->onClick();
    bool down = true;
    blocks.isKeyDownProbe = [&down](int) { return down; };
    EXPECT_TRUE(blocks.keyPressed(p));
    EXPECT_EQ(blocks.getHeldKey(), 60);
    EXPECT_GT(scene.run(4), 0.01f);
    EXPECT_TRUE(blocks.keyPressed(p)) << "key repeat does not play it again";
    blocks.keyStateChanged(true);
    EXPECT_EQ(blocks.getHeldKey(), 60) << "still down";
    down = false;
    blocks.keyStateChanged(false);
    EXPECT_EQ(blocks.getHeldKey(), -1);
    EXPECT_EQ(scene.run(40), 0.0f);
}

TEST(SamplerMapAudition, LosingFocusReleasesAHeldPlayKey) {
    Scene scene;
    scene.blocks().getBlock(scene.regionWithRoot(60))->onClick();
    scene.blocks().keyPressed(juce::KeyPress('p', juce::ModifierKeys::noModifiers, 0));
    ASSERT_GT(scene.run(4), 0.01f);
    scene.blocks().focusLost(juce::Component::focusChangedDirectly);
    EXPECT_EQ(scene.run(40), 0.0f);
}

TEST(SamplerMapAudition, CmdZFromTheViewTheBlocksAndAFieldGoesToTheAppAndEscapeStillCloses) {
    Scene scene;
    int received = 0;
    scene.view().onAppShortcut = [&received](const juce::KeyPress& key) {
        received += key == kUndoKey ? 1 : 0;
        return true;
    };
    EXPECT_TRUE(scene.view().keyPressed(kUndoKey));
    EXPECT_TRUE(bubble(&scene.blocks(), kUndoKey));
    EXPECT_TRUE(bubble(&scene.view().getFields().getField(SamplerMapFields::HighKey), kUndoKey));
    EXPECT_EQ(received, 3);
    EXPECT_FALSE(scene.view().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
        << "no window in a test: Escape is not handed to the app";
    EXPECT_EQ(received, 3);
    scene.view().onAppShortcut = nullptr;
    EXPECT_FALSE(scene.view().keyPressed(kUndoKey));
}

TEST(SamplerMapAudition, AFieldEditIsOneUndoStepAndTheOpenMapFollowsTheUndo) {
    Scene scene;
    const int c3 = scene.regionWithRoot(60);
    scene.blocks().getBlock(c3)->onClick();
    const int oldHigh = scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey;
    const int oldRight = scene.blocks().getBlock(c3)->getRight();
    auto& field = scene.view().getFields().getField(SamplerMapFields::HighKey);
    field.setValue(oldHigh - 4, juce::sendNotificationSync);
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh - 4);

    ASSERT_TRUE(scene.canvas.undo.undo());
    EXPECT_EQ(scene.sampler().getInstrumentRegions()[(size_t)c3].hiKey, oldHigh);
    scene.view().poll();
    EXPECT_EQ((int)field.getValue(), oldHigh) << "the fields follow the undo";
    EXPECT_EQ(scene.blocks().getBlock(c3)->getRight(), oldRight) << "and so do the blocks";
    EXPECT_FALSE(scene.canvas.undo.undo()) << "the edit was one step, no more";
}

namespace {

constexpr int kStripWidth = 300;
constexpr int kStripHeight = 40;

// The strip's x for the middle of `note`, on the same geometry the strip paints with (2 px inset).
float stripX(const SamplerKeyStrip& strip, int note) {
    const float keyWidth = (float)(kStripWidth - 4) / (float)(strip.getKeySpan().getLength() + 1);
    return 2.0f + (float)(note - strip.getKeySpan().getStart() + 0.5f) * keyWidth;
}

juce::MouseEvent stripMouse(SamplerKeyStrip& strip, float x, bool down) {
    return mouseAt(strip, {x, 20.0f},
                   down ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys());
}

} // namespace

TEST(SamplerKeyStripAudition, PressingAKeySoundsItAndLettingGoEndsIt) {
    Scene scene;
    SamplerKeyStrip strip(scene.sampler());
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    EXPECT_EQ(scene.run(2), 0.0f);
    strip.mouseDown(stripMouse(strip, stripX(strip, 48), true));
    EXPECT_EQ(strip.getHeldKey(), 48);
    EXPECT_TRUE(strip.getLitNotes().test(48)) << "lit at once, without waiting for the tick";
    EXPECT_GT(scene.run(4), 0.01f);
    strip.mouseUp(stripMouse(strip, stripX(strip, 48), false));
    EXPECT_EQ(strip.getHeldKey(), -1);
    EXPECT_EQ(scene.run(40), 0.0f);
}

TEST(SamplerKeyStripAudition, DraggingToAnotherKeyReleasesTheOldAndPlaysTheNew) {
    Scene scene;
    SamplerKeyStrip strip(scene.sampler());
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    strip.mouseDown(stripMouse(strip, stripX(strip, 48), true));
    scene.run(4);
    strip.mouseDrag(stripMouse(strip, stripX(strip, 50), true));
    EXPECT_EQ(strip.getHeldKey(), 50);
    scene.run(40);
    EXPECT_FALSE(scene.sampler().getSoundingNotes().test(48));
    EXPECT_TRUE(scene.sampler().getSoundingNotes().test(50));
    strip.mouseUp(stripMouse(strip, stripX(strip, 50), false));
    EXPECT_EQ(scene.run(40), 0.0f);
}

TEST(SamplerKeyStripAudition, APressWithNoInstrumentDoesNothing) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<SamplerModule>(), 0, 0);
    auto& sampler = *dynamic_cast<SamplerModule*>(canvas.processor(id));
    sampler.prepareToPlay(kRate, kBlock);
    SamplerKeyStrip strip(sampler);
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    strip.mouseDown(stripMouse(strip, 150.0f, true));
    EXPECT_EQ(strip.getHeldKey(), -1);
    strip.mouseUp(stripMouse(strip, 150.0f, false));
    EXPECT_EQ(strip.getHeldKey(), -1);
}

namespace {

// The rows a black key and a white key cover in a painted keyboard, read from the pixels of the note's own column.
float blackOverWhiteHeight(juce::Component& keyboard, float blackX, float whiteX) {
    const auto& colors = synth::theme::themeOf(keyboard).colors;
    juce::Image image(juce::Image::ARGB, keyboard.getWidth(), keyboard.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    keyboard.paintEntireComponent(g, false);
    const auto black = pianokeyprobe::extentOf(image, (int)blackX, colors.pianoKeyBlack);
    const auto white = pianokeyprobe::extentOf(image, (int)whiteX, colors.pianoKeyWhite);
    EXPECT_GT(black.height(), 0) << "no black key pixels in the column";
    EXPECT_GT(white.height(), 0) << "no white key pixels in the column";
    // The black key hangs from the white key's top; what is above that is the card, not the key.
    return white.height() > 0 ? (float)(black.last - white.first + 1) / (float)white.height() : 0.0f;
}

// The same for the card strip, whose keys sit at the bottom under the bands, with nothing under a black key but the
// card. Each key is held in turn so it shows in the accent colour: the white key is read up from its foot and the
// black key down from the white key's top.
float stripBlackOverWhiteHeight(SamplerKeyStrip& strip, int blackNote, int whiteNote) {
    const auto paintHolding = [&strip](int note) {
        strip.mouseDown(stripMouse(strip, stripX(strip, note), true));
        juce::Image image(juce::Image::ARGB, strip.getWidth(), strip.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        strip.paintEntireComponent(g, false);
        strip.mouseUp(stripMouse(strip, stripX(strip, note), false));
        return image;
    };
    const auto whiteImage = paintHolding(whiteNote);
    const auto white = pianokeyprobe::runAround(whiteImage, (int)stripX(strip, whiteNote), strip.getHeight() - 4);
    const auto blackImage = paintHolding(blackNote);
    const auto black = pianokeyprobe::runAround(blackImage, (int)stripX(strip, blackNote), white.first + 1);
    EXPECT_GT(white.height(), 4) << "no white key at the foot of the strip";
    return white.height() > 0 ? (float)black.height() / (float)white.height() : 0.0f;
}

// The first C# in the span that has an E after it, and that E.
std::pair<int, int> blackAndWhiteKey(juce::Range<int> span) {
    for (int note = span.getStart(); note + 3 <= span.getEnd(); ++note)
        if (note % 12 == 1)
            return {note, note + 3};
    return {-1, -1};
}

} // namespace

// Bug kind: sibling-gap. Black keys are 62% of a white key's height on the card strip and the map window.
TEST(SamplerKeyShape, TheCardStripAndTheMapWindowDrawBlackKeysAtTheSharedHeight) {
    Scene scene;
    SamplerKeyStrip strip(scene.sampler());
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    const auto [stripBlack, stripWhite] = blackAndWhiteKey(strip.getKeySpan());
    ASSERT_GE(stripBlack, 0);
    EXPECT_NEAR(stripBlackOverWhiteHeight(strip, stripBlack, stripWhite), synth::ui::pianokeys::kBlackKeyHeightShare,
                0.06f);

    auto& blocks = scene.blocks();
    if (blocks.getWidth() == 0)
        blocks.setSize(700, 220);
    const auto [mapBlack, mapWhite] = blackAndWhiteKey(blocks.getKeySpan());
    ASSERT_GE(mapBlack, 0);
    EXPECT_NEAR(blackOverWhiteHeight(blocks, scene.keyCentre(mapBlack).x, scene.keyCentre(mapWhite).x),
                synth::ui::pianokeys::kBlackKeyHeightShare, 0.06f);
}

// Bug kind: sibling-gap / proof-misses-claim. A short black key shows only when the white keys run on underneath it
// (read a few pixels off the key's centre, where the hairline between two white keys runs):
// the card strip once drew nothing there, so on the card the black keys looked as tall as the white ones.
TEST(SamplerKeyShape, ThereIsWhiteKeyUnderEveryBlackKeyOnTheCardStripAndTheMapWindow) {
    Scene scene;
    SamplerKeyStrip strip(scene.sampler());
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    const auto& colors = synth::theme::themeOf(strip).colors;
    const auto [stripBlack, stripWhite] = blackAndWhiteKey(strip.getKeySpan());
    ASSERT_GE(stripBlack, 0);
    juce::Image stripImage(juce::Image::ARGB, strip.getWidth(), strip.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(stripImage);
        strip.paintEntireComponent(g, false);
    }
    const int stripBottom = strip.getHeight() - 4;
    EXPECT_TRUE(pianokeyprobe::near(stripImage.getPixelAt((int)stripX(strip, stripBlack) - 3, stripBottom),
                                    colors.pianoKeyWhite))
        << "under the card strip's black key";

    auto& blocks = scene.blocks();
    if (blocks.getWidth() == 0)
        blocks.setSize(700, 220);
    const auto [mapBlack, mapWhite] = blackAndWhiteKey(blocks.getKeySpan());
    ASSERT_GE(mapBlack, 0);
    juce::Image mapImage(juce::Image::ARGB, blocks.getWidth(), blocks.getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(mapImage);
        blocks.paintEntireComponent(g, false);
    }
    EXPECT_TRUE(pianokeyprobe::near(
        mapImage.getPixelAt((int)scene.keyCentre(mapBlack).x - 3, (int)scene.keyCentre(mapBlack, 0.9f).y),
        colors.pianoKeyWhite))
        << "under the map window's black key";
}

// The strip and the map answer a press at the same height with the same velocity: soft at the top of a key, loud at its
// front edge, the way on-screen keyboards in other music software do.
TEST(SamplerKeyShape, TheCardStripAndTheMapWindowPlayLouderLowerOnTheKey) {
    Scene scene;
    SamplerKeyStrip strip(scene.sampler());
    strip.setBounds(0, 0, kStripWidth, kStripHeight);
    const auto stripVelocityAt = [&](float y) {
        const auto note = strip.getKeySpan().getStart() + 3;
        strip.mouseDown(
            mouseAt(strip, {stripX(strip, note), y}, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
        strip.mouseUp(mouseAt(strip, {stripX(strip, note), y}, juce::ModifierKeys()));
        juce::MidiBuffer midi;
        scene.sampler().noteInbox().drainInto(midi, kBlock);
        int velocity = -1;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                velocity = meta.getMessage().getVelocity();
        return velocity;
    };
    const int stripTop = stripVelocityAt(0.0f);
    const int stripBottom = stripVelocityAt((float)kStripHeight);
    EXPECT_EQ(stripTop, synth::ui::pianokeys::kSoftestKeyVelocity);
    EXPECT_EQ(stripBottom, synth::ui::pianokeys::kLoudestKeyVelocity);

    int mapVelocity = -1;
    scene.blocks().onPlayKey = [&](int, int velocity) { mapVelocity = velocity; };
    const int note = scene.blocks().getKeySpan().getStart() + 3;
    scene.press(note, 0.0f);
    EXPECT_EQ(mapVelocity, stripTop);
    scene.release(note);
    scene.press(note, 0.98f);
    EXPECT_EQ(mapVelocity, stripBottom);
    scene.release(note);
}
