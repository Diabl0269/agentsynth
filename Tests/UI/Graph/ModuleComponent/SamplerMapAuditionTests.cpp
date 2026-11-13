// SamplerMapAuditionTests.cpp
// The instrument map window's keyboard: a click or drag on its keys plays the Sampler through its own note inbox
// (heard by running the module's processBlock), the keys light while they sound, P plays the selected recording, a
// held note is released when the view goes, and the app's shortcuts (Cmd+Z) pass through the window to the app.

#include "../../../Modules/Sampler/SamplerTestWavs.h"
#include "../CardBody/CardCanvas.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerKeyStrip.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"

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
        // Two seconds of sine per recording, so a held note is still sounding when the test looks.
        juce::StringArray files;
        for (const char* name : {"sine_C2.wav", "sine_C3.wav", "sine_C4.wav"}) {
            const auto file = dir.file(name);
            writeWav(file, (int)(2 * kRate), 1, kRate, [](int, int i) { return 0.4f * std::sin(0.05f * (float)i); });
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
