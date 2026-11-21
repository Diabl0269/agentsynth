// A hand on a knob beats the lane that automates it (control-moves-sound-does-not guard).
//
// While the transport rolls, AutomationApplier writes every automated parameter each block. A knob
// turned against a lane used to move on screen and change nothing you hear: the lane overwrote the
// value on the next block (Read lanes always, Touch/Latch only when record was armed). The rule now:
// any gesture on a lane's parameter claims it, whatever the lane's record mode and with record off,
// the claim outlives the gesture by AutomationRecorder::kHandLingerMs (a key step is a whole
// begin/set/end inside one call), and the lane then eases back over
// AutomationApplier::kReleaseGlideSec instead of jumping.
//
// Mouse and key tests go through a real CardKnobSlider with the same SliderParameterAttachment the
// module cards use; the sweep walks every module's parameters through the applier directly.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "Timeline/AutomationApplier.h"
#include "Timeline/AutomationRecorder.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineSnapshot.h"
#include "Transport/OfflineTransportDriver.h"
#include "Transport/TransportService.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include <cmath>
#include <gtest/gtest.h>
#include <memory>

using synth::AutomationRecorder;
using synth::LaneRecordMode;
using synth::TimelineDoc;
using synth::TrackKind;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;
constexpr const char* kFilterUuid = "b0510000-0000-0000-0000-000000000002";
constexpr const char* kNoiseUuid = "b0510000-0000-0000-0000-000000000001";
constexpr double kLaneHz = 1200.0;

int mode(LaneRecordMode m) { return static_cast<int>(m); }

// Noise -> Filter -> Output with a cutoff lane holding 1200 Hz, and a recorder wired in. Record is
// NOT armed: this is the everyday case (a song built and played, a knob turned).
struct Fixture {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    std::unique_ptr<synth::OfflineTransportDriver> driver;
    TimelineDoc doc;
    AppUndoManager undo;
    AutomationRecorder recorder;
    synth::LaneId laneId;

    bool build(LaneRecordMode laneMode) {
        engine.initialise();
        const juce::var patch = juce::JSON::parse(juce::String(R"({
            "nodes": [
                {"id": 1, "type": "Noise",        "uuid": ")") +
                                                  kNoiseUuid + R"("},
                {"id": 2, "type": "Filter",       "uuid": ")" +
                                                  kFilterUuid + R"("},
                {"id": 3, "type": "Audio Output", "uuid": "b0510000-0000-0000-0000-000000000003"}
            ],
            "connections": [
                {"src": 1, "srcPort": 0, "dst": 2, "dstPort": 0},
                {"src": 2, "srcPort": 0, "dst": 3, "dstPort": 0}
            ]
        })");
        if (!patch.isObject() || !synth::AIStateMapper::applyJSONToGraph(patch, engine.getGraph(), true, true))
            return false;
        driver = std::make_unique<synth::OfflineTransportDriver>(engine, kSampleRate, kBlock, 2);

        const auto trackId = doc.addTrack(TrackKind::Midi, "Track 1");
        synth::AutomationLane::RangeSnapshot range;
        range.minValue = 20.0f;
        range.maxValue = 20000.0f;
        range.defaultValue = 440.0f;
        laneId = doc.addLane(trackId, kFilterUuid, "cutoff", range);
        if (!laneId.isValid())
            return false;
        doc.addBreakpoint(laneId, 0.0, kLaneHz, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold));
        doc.setLaneRecordMode(laneId, mode(laneMode));

        recorder.attachTo(doc, undo, engine.getTransport());
        recorder.bindLane(laneId, cutoff(), nodeFor(kFilterUuid));
        engine.setAutomationRecorder(&recorder);
        engine.publishTimeline(doc);
        return cutoff() != nullptr;
    }

    juce::AudioProcessorGraph::Node::Ptr nodeFor(const char* uuid) {
        for (auto* node : engine.getGraph().getNodes())
            if (node != nullptr && node->properties["uuid"].toString() == juce::String(uuid))
                return node;
        return {};
    }
    juce::RangedAudioParameter* cutoff() {
        auto node = nodeFor(kFilterUuid);
        return node != nullptr ? findParameterByID(node->getProcessor(), "cutoff") : nullptr;
    }
    double cutoffHz() { return static_cast<double>(cutoff()->convertFrom0to1(cutoff()->getValue())); }
    void setCutoffHz(double hz) { cutoff()->setValueNotifyingHost(cutoff()->convertTo0to1(static_cast<float>(hz))); }

    // Level of `blocks` rendered blocks, left channel RMS.
    double renderRms(int blocks) {
        const auto audio = driver->renderBlocks(blocks);
        return audio.getRMSLevel(0, 0, audio.getNumSamples());
    }

    ~Fixture() {
        engine.setAutomationRecorder(nullptr);
        recorder.detach();
        if (driver) {
            engine.releaseFromHost();
            engine.shutdown();
        }
    }
};

} // namespace

// The reported bug, at engine level: lane at 1200 Hz, hand 120 Hz -> 8 kHz while it plays. The
// parameter follows the hand on every step, the filter output opens up, and letting go neither
// jumps nor leaves the lane dead. Every record mode that plays, record off.
TEST(AutomationHandWinsTest, HeldGestureBeatsTheLaneInEveryPlayingMode) {
    for (const auto laneMode :
         {LaneRecordMode::Read, LaneRecordMode::Touch, LaneRecordMode::Latch, LaneRecordMode::Write}) {
        SCOPED_TRACE(mode(laneMode));
        Fixture f;
        ASSERT_TRUE(f.build(laneMode));
        f.recorder.setHandLingerMs(0.0);
        ASSERT_TRUE(f.driver->getTransport().play());
        f.driver->renderBlocks(8);
        EXPECT_NEAR(f.cutoffHz(), kLaneHz, 2.0) << "the lane plays while nobody touches the knob";

        f.cutoff()->beginChangeGesture();
        double lowRms = 0.0, highRms = 0.0;
        for (const double hz : {120.0, 400.0, 1500.0, 4000.0, 8000.0}) {
            f.setCutoffHz(hz);
            f.driver->renderBlocks(40); // let the filter's own smoothing settle
            EXPECT_NEAR(f.cutoffHz(), hz, hz * 0.01) << "the lane overwrote the hand";
            const double rms = f.renderRms(8);
            if (hz == 120.0)
                lowRms = rms;
            if (hz == 8000.0)
                highRms = rms;
        }
        EXPECT_GT(highRms, lowRms * 2.0) << "turning the cutoff up must be heard";

        // Release: no jump on the first block, then the lane is back within its glide.
        f.cutoff()->endChangeGesture();
        f.driver->renderBlocks(1);
        EXPECT_GT(f.cutoffHz(), 6000.0) << "the first block after release must not jump to the lane";
        f.driver->renderBlocks(64);
        EXPECT_NEAR(f.cutoffHz(), kLaneHz, 5.0) << "the lane resumes after the glide";
    }
}

// The hand keeps the knob for a moment after release (a key step ends inside its own call), then the
// lane eases back; a new touch inside the linger keeps the claim.
TEST(AutomationHandWinsTest, ReleaseLingersThenGlidesBack) {
    Fixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Read));
    f.recorder.setHandLingerMs(60.0);
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(4);

    f.cutoff()->beginChangeGesture();
    f.setCutoffHz(5000.0);
    f.cutoff()->endChangeGesture();
    f.driver->renderBlocks(20);
    EXPECT_NEAR(f.cutoffHz(), 5000.0, 50.0) << "still the hand's inside the linger";

    juce::Thread::sleep(90);
    f.recorder.update(); // the UI timer in the app
    f.driver->renderBlocks(1);
    EXPECT_GT(f.cutoffHz(), 4000.0) << "the glide starts from the hand's value, no jump";
    f.driver->renderBlocks(64);
    EXPECT_NEAR(f.cutoffHz(), kLaneHz, 5.0);
}

// Off lanes still never write, hand or no hand (nothing to win), and a stopped transport is free.
TEST(AutomationHandWinsTest, ReleaseWithTheLaneOffAndAStoppedTransportStayFree) {
    Fixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Off));
    f.recorder.setHandLingerMs(0.0);
    ASSERT_TRUE(f.driver->getTransport().play());
    f.cutoff()->beginChangeGesture();
    f.setCutoffHz(3000.0);
    f.cutoff()->endChangeGesture();
    f.driver->renderBlocks(64);
    EXPECT_NEAR(f.cutoffHz(), 3000.0, 30.0);
}

namespace {

// The module card's own knob on `param`: a CardKnobSlider with the SliderParameterAttachment
// CardBody gives it, so begin/endChangeGesture come from the slider the way they do in the app.
struct KnobRig {
    explicit KnobRig(juce::RangedAudioParameter& param)
        : attachment(param, knob) {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setSize(60, 60);
    }
    synth::ui::CardKnobSlider knob;
    juce::SliderParameterAttachment attachment;

    juce::MouseEvent event(juce::Point<float> pos, bool down) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, &knob, &knob, now,
                                down ? pos : knob.getLocalBounds().toFloat().getCentre(), now, 1, false);
    }
};

} // namespace

// The real mouse path: mouseDown, mouseDrag, mouseUp on the card's own knob while the lane plays.
TEST(AutomationHandWinsTest, DraggingTheCardKnobTakesOverFromTheLane) {
    Fixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Read));
    f.recorder.setHandLingerMs(0.0);
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(8);

    KnobRig rig(*f.cutoff());
    const auto centre = rig.knob.getLocalBounds().toFloat().getCentre();
    const double before = f.cutoffHz();
    rig.knob.mouseDown(rig.event(centre, true));
    EXPECT_TRUE(f.recorder.getAudioState().claims.isClaimed(f.cutoff())) << "the press claims the parameter";

    auto drag = rig.event(centre.translated(0.0f, -40.0f), true);
    rig.knob.mouseDrag(drag);
    const double dragged = f.cutoffHz();
    EXPECT_NE(dragged, before) << "the drag moves the parameter";
    f.driver->renderBlocks(32);
    EXPECT_NEAR(f.cutoffHz(), dragged, dragged * 0.01) << "the lane must not undo the drag";

    rig.knob.mouseUp(rig.event(centre.translated(0.0f, -40.0f), true));
    EXPECT_FALSE(f.recorder.getAudioState().claims.isClaimed(f.cutoff()));
}

// The keyboard path: an arrow step on a focused automated knob is a whole gesture in one call. It
// has to survive the next blocks, which is what the linger is for (default linger, no override).
TEST(AutomationHandWinsTest, ArrowKeyStepOnAnAutomatedKnobSticks) {
    Fixture f;
    ASSERT_TRUE(f.build(LaneRecordMode::Read));
    ASSERT_TRUE(f.driver->getTransport().play());
    f.driver->renderBlocks(8);

    KnobRig rig(*f.cutoff());
    const double before = f.cutoffHz();
    ASSERT_TRUE(rig.knob.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    const double stepped = f.cutoffHz();
    EXPECT_GT(stepped, before) << "Up raises the cutoff";
    f.driver->renderBlocks(32);
    EXPECT_NEAR(f.cutoffHz(), stepped, stepped * 0.01) << "the lane must not undo the key step";
}

// THE guard for the kind (bug-classes/control-moves-sound-does-not.md): every automatable parameter
// of every module follows a held gesture while its lane plays. The lane is Read with record off, the
// everyday state. A parameter that is added later is covered by the walk; one the lane may not
// override needs a reason in the allow-list, never a skipped case.
TEST(AutomationHandWinsTest, EveryAutomatableParameterFollowsAHeldGesture) {
    int checked = 0;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto module = synth::AIStateMapper::createModule(type);
        if (module == nullptr)
            continue;
        for (auto* p : module->getParameters()) {
            auto* param = dynamic_cast<juce::RangedAudioParameter*>(p);
            if (param == nullptr || !param->isAutomatable())
                continue;
            const auto& range = param->getNormalisableRange();
            if (!(range.end > range.start))
                continue;
            SCOPED_TRACE((type + "/" + param->paramID).toStdString());

            TimelineDoc doc;
            AppUndoManager undo;
            synth::TransportService transport;
            const auto trackId = doc.addTrack(TrackKind::Midi, "T");
            synth::AutomationLane::RangeSnapshot snap;
            snap.minValue = range.start;
            snap.maxValue = range.end;
            snap.defaultValue = param->convertFrom0to1(param->getDefaultValue());
            const auto laneId = doc.addLane(trackId, "uuid", param->paramID, snap);
            ASSERT_TRUE(laneId.isValid());
            // The lane holds the low end; the hand goes to the high end.
            doc.addBreakpoint(laneId, 0.0, range.start, 0.0f, static_cast<int>(synth::BreakpointCurve::Hold));
            doc.setLaneRecordMode(laneId, mode(LaneRecordMode::Read));

            AutomationRecorder recorder;
            recorder.setHandLingerMs(0.0);
            recorder.attachTo(doc, undo, transport);
            recorder.bindLane(laneId, param, nullptr);

            auto snapshot = synth::TimelineSnapshot::buildFrom(doc);
            synth::AutomationBindingTable table;
            table.snapshot = snapshot.get();
            synth::AutomationBindingTable::Binding binding;
            binding.laneIndex = 0;
            binding.param = param;
            table.bindings.push_back(binding);

            synth::BlockTimeInfo info;
            info.playing = true;
            info.numSamples = kBlock;
            info.sampleRate = kSampleRate;
            synth::AutomationApplier applier;

            applier.applyBlock(table, info, &recorder.getAudioState());
            const float laneValue = param->getValue();

            param->beginChangeGesture();
            param->setValueNotifyingHost(1.0f);
            const float hand = param->getValue();
            if (hand != laneValue) { // a one-value parameter has no hand to win
                for (int i = 0; i < 8; ++i)
                    applier.applyBlock(table, info, &recorder.getAudioState());
                EXPECT_EQ(param->getValue(), hand) << "the lane overwrote a held gesture";
            }
            param->endChangeGesture();
            recorder.detach();
            ++checked;
        }
    }
    EXPECT_GT(checked, 300) << "the walk must reach every module's parameters";
}
