// A real LFO cabled to a Delay's Level fader, the graph processed offline: the ring (the mod bar beside the fader's
// slot) follows the LFO while the fader's value and its number stay where they are, and the control shows its mod dot.
// docs/modules/modulation.md#modulation-rings-on-knobs.

#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/DelayModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <gtest/gtest.h>

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 512;

struct LevelRing {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    synth::theme::AppLookAndFeel lnf;
    juce::AudioProcessorGraph::NodeID lfoId, delayId;
    ModuleComponent* card = nullptr;
    synth::ui::CardFader* fader = nullptr;
    int levelChannel = -1;

    LevelRing() {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto lfo = std::make_unique<LFOModule>();
        if (auto* mode = findParameterByID(lfo.get(), "mode"))
            mode->setValueNotifyingHost(0.0f); // Free: no transport needed
        if (auto* rate = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(lfo.get(), "rateHz")))
            rate->setValueNotifyingHost(rate->convertTo0to1(1.0f));
        lfoId = graph.addNode(std::move(lfo))->nodeID;
        auto delayNode = graph.addNode(std::make_unique<DelayModule>());
        delayId = delayNode->nodeID;
        levelChannel = dynamic_cast<ModuleBase*>(delayNode->getProcessor())->modulationChannelForParam("outputLevel");
        editor->updateComponents();
        editor->connectModulationSource(lfoId, 0, delayId, levelChannel, 1.0f);
        editor->updateComponents();
        editor->timerCallback();
        card = findModuleComp(*editor, delayNode->getProcessor());
        if (card != nullptr) {
            card->setLookAndFeel(&lnf);
            fader = dynamic_cast<synth::ui::CardFader*>(card->getCardBody()->findWidget("outputLevel"));
        }
        graph.setPlayConfigDetails(0, 2, kRate, kBlock);
        graph.prepareToPlay(kRate, kBlock);
    }
    ~LevelRing() {
        if (card != nullptr)
            card->setLookAndFeel(nullptr);
        editor.reset();
        engine.shutdown();
    }

    // Runs the whole graph (LFO -> attenuverter -> Delay) `blocks` blocks, then lets the card see the new reading.
    void process(int blocks) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        for (int i = 0; i < blocks; ++i) {
            buffer.clear();
            engine.getGraph().processBlock(buffer, midi);
        }
        editor->timerCallback();
        card->timerCallback();
    }

    juce::Image paintCard() {
        juce::Image image(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        card->paintEntireComponent(g, true); // the card and its faders, dots and all
        return image;
    }

    // How many pixels along the fader's bar track the card painted in a mod ring colour.
    float ringExtent(const juce::Image& image) {
        const auto track = fader->modBarTrack().translated((float)fader->getX(), (float)fader->getY());
        const auto& colours = lnf.getTheme().colors;
        auto near = [](juce::Colour a, juce::Colour b) {
            return std::abs(a.getRed() - b.getRed()) < 40 && std::abs(a.getGreen() - b.getGreen()) < 40 &&
                   std::abs(a.getBlue() - b.getBlue()) < 40;
        };
        int lit = 0;
        const int y = juce::roundToInt(track.getCentreY());
        for (int x = juce::roundToInt(track.getX()); x < juce::roundToInt(track.getRight()); ++x) {
            const auto px = image.getPixelAt(x, y);
            if (near(px, colours.modRingPositive) || near(px, colours.modRingNegative))
                ++lit;
        }
        return (float)lit;
    }
};

// Written only when FRO_RING_EVIDENCE_DIR names a folder, for a person to look at.
void saveEvidence(const juce::Image& image, const juce::String& name) {
    const char* dir = std::getenv("FRO_RING_EVIDENCE_DIR");
    if (dir == nullptr)
        return;
    juce::FileOutputStream stream(juce::File(dir).getChildFile(name));
    if (stream.openedOk()) {
        stream.setPosition(0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream(image, stream);
    }
}

} // namespace

TEST(LevelRingMoves, TheRingFollowsTheLfoWhileTheFaderAndItsNumberStayPut) {
    LevelRing f;
    ASSERT_NE(f.card, nullptr);
    ASSERT_NE(f.fader, nullptr) << "the Delay's Level is a fader on its card";
    ASSERT_GE(f.levelChannel, 0);
    ASSERT_GT(f.card->getWidth(), 0);

    f.fader->setValue(0.5 * (f.fader->getMinimum() + f.fader->getMaximum()), juce::sendNotificationSync);
    const double value = f.fader->getValue();
    const auto number = f.fader->getTextFromValue(value);
    std::vector<float> extents, readings;
    // A 1 Hz LFO is 86 blocks of 512 at 44.1 kHz: each step moves a quarter of the cycle on.
    for (int step = 0; step < 4; ++step) {
        f.process(step == 0 ? 20 : 21); // a quarter of the cycle each
        const auto image = f.paintCard();
        saveEvidence(image, "level-ring-phase" + juce::String(step) + ".png");
        extents.push_back(f.ringExtent(image));
        for (const auto& info : f.editor->getCachedModDisplayInfo())
            if (info.destNodeID == f.delayId && info.destChannelIndex == f.levelChannel)
                readings.push_back(info.modSignalValue);
        EXPECT_DOUBLE_EQ(f.fader->getValue(), value) << "the fader never moves with the CV";
        EXPECT_EQ(f.fader->getTextFromValue(f.fader->getValue()), number) << "nor does its number";
    }
    ASSERT_EQ(readings.size(), 4u);
    const auto [lo, hi] = std::minmax_element(extents.begin(), extents.end());
    EXPECT_GT(*hi - *lo, 4.0f) << "the ring's length differs between LFO phases";
    const auto [rlo, rhi] = std::minmax_element(readings.begin(), readings.end());
    EXPECT_GT(*rhi - *rlo, 0.1f) << "the live reading the ring is drawn from moves with the LFO";
}

TEST(LevelRingMoves, TheLevelFaderShowsItsModDot) {
    LevelRing f;
    ASSERT_NE(f.fader, nullptr);
    f.process(3);
    auto* dot = f.card->getModDotButton(f.levelChannel);
    ASSERT_NE(dot, nullptr) << "a cabled Level fader has a visible mod dot";
    EXPECT_TRUE(dot->isVisible());
    EXPECT_EQ(dot->getKnob(), static_cast<juce::Component*>(f.fader));
    EXPECT_TRUE(f.card->getLocalBounds().contains(dot->getBounds())) << "the dot is inside the card";

    // The dot you see is the cable's landing dot, painted by the canvas over the card (so a screenshot of the card
    // alone never holds it): the cable ends on the fader's landing point, beside the left end of its bar.
    f.editor->updateComponents(); // lay the card out as the canvas does before reading where the cable ends
    f.editor->timerCallback();
    f.card = findModuleComp(*f.editor, f.engine.getGraph().getNodeForId(f.delayId)->getProcessor());
    f.fader = dynamic_cast<synth::ui::CardFader*>(f.card->getCardBody()->findWidget("outputLevel"));
    ASSERT_NE(f.fader, nullptr);
    const auto landing = f.card->getPosition().toFloat() + f.fader->getPosition().toFloat() +
                         f.fader->landingPoint(ModuleComponent::kKnobLandingDotDiameter);
    int landed = 0;
    for (const auto& cable : f.editor->buildVisibleCables()) {
        if (cable.destNodeId != f.delayId.uid || cable.destChannel != f.levelChannel)
            continue;
        EXPECT_TRUE(cable.landsOnKnob) << "the cable ends on the fader, not at a gutter jack";
        EXPECT_NEAR(cable.p2.x, landing.x, 2.0f);
        EXPECT_NEAR(cable.p2.y, landing.y, 2.0f);
        ++landed;
    }
    EXPECT_EQ(landed, 1);
}

// A cable on Pan while the More row is folded ends at the More button, not at a gutter row that belongs to the next
// real jack (a Compressor's Key input sits on it).
TEST(LevelRingMoves, ACableOnAFoldedPanEndsAtTheMoreButton) {
    LevelRing f;
    ASSERT_NE(f.card, nullptr);
    auto* delay = dynamic_cast<ModuleBase*>(f.engine.getGraph().getNodeForId(f.delayId)->getProcessor());
    const int pan = delay->modulationChannelForParam("pan");
    ASSERT_GE(pan, 0);
    f.editor->connectModulationSource(f.lfoId, 0, f.delayId, pan, 1.0f);
    f.editor->updateComponents();
    f.editor->timerCallback();
    f.card = findModuleComp(*f.editor, delay);
    auto* more = f.card->findChildWithID("cardMoreRow");
    ASSERT_NE(more, nullptr);
    ASSERT_TRUE(more->isVisible()) << "the More row is folded";
    const auto end = f.card->getPortCenter(delay->mapInputChannel(pan).visibleJackIndex, true);
    EXPECT_EQ(end.y, more->getBounds().getCentreY());
    EXPECT_LE(end.x, more->getX());
}
