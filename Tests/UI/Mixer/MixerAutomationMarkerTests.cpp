// The automation-lane marker on mixer faders and knobs (docs/mixer/panel.md#automation-markers): same glyph and
// rules as on a module card. A column asks the host (GraphEditor::onQueryAutomatedParamsForNode) on its 10 Hz meter
// tick; the fader, pan and send knobs that have a lane get the marker, the "Automated: ..." tooltip line and an
// "automated" screen-reader description, and lose them again when the lane goes.

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerModel/MixerModel.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/AutomatedMarker.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerMasterColumn.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>

namespace {

struct ClockPin {
    explicit ClockPin(double ms) { synth::ui::AutomatedMarkerFade::setClockForTest(ms); }
    ~ClockPin() { synth::ui::AutomatedMarkerFade::setClockForTest(std::nullopt); }
    void set(double ms) { synth::ui::AutomatedMarkerFade::setClockForTest(ms); }
};

juce::Image paint(juce::Component& c) {
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    c.paintEntireComponent(g, false);
    return image;
}

int differing(const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area) {
    int n = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                ++n;
    return n;
}

struct ColumnFixture {
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::ui::MixerColumnComponent column;
    std::set<juce::String> automated;

    ColumnFixture() {
        auto& graph = engine.getGraph();
        graph.setPlayConfigDetails(0, 2, 44100.0, 512);
        editor.setSize(900, 600);
        const synth::DefaultChannelLayout layout{{-100, 0}, {0, 0}, {100, 0}, {200, 0}, {300, 0}};
        const auto channel = synth::buildBusChannel(graph, layout);
        column.configure(graph, undoManager, editor.getMacros(), editor, engine);
        column.setSize(140, 300);
        synth::MixerColumn model;
        model.nodeId = channel.strip->nodeID;
        model.name = "Test Strip";
        column.setColumn(model, "");
        editor.onQueryAutomatedParamsForNode = [this](juce::AudioProcessorGraph::NodeID) { return automated; };
    }
};

} // namespace

TEST(MixerAutomationMarkerTests, FaderGainsAndLosesTheMarkerTooltipAndDescriptionWithItsLane) {
    ClockPin clock(1000.0);
    ColumnFixture f;
    auto& slider = f.column.getFaderForTest().getSlider();
    const auto before = slider.getTooltip();

    f.column.refreshMeter(0.1f);
    EXPECT_FALSE(f.column.isAutomatedMarkerShownForTest(&slider));

    f.automated = {"gain"};
    f.column.refreshMeter(0.1f);
    EXPECT_TRUE(f.column.isAutomatedMarkerShownForTest(&slider));
    EXPECT_TRUE(slider.getTooltip().startsWith(before));
    EXPECT_TRUE(slider.getTooltip().contains("Automated: Gain")) << slider.getTooltip();
    EXPECT_TRUE(slider.getDescription().containsIgnoreCase("automated"));
    EXPECT_FALSE(f.column.isAutomatedMarkerShownForTest(&f.column.getPanSliderForTest())) << "only the gain has a lane";

    f.automated.clear();
    f.column.refreshMeter(0.1f);
    EXPECT_FALSE(f.column.isAutomatedMarkerShownForTest(&slider));
    EXPECT_EQ(slider.getTooltip(), before);
    EXPECT_FALSE(slider.getDescription().containsIgnoreCase("automated"));
}

TEST(MixerAutomationMarkerTests, ColumnPaintsTheMarkerAtTheFadersTopLeftOnlyWhileAutomated) {
    ClockPin clock(1000.0);
    ColumnFixture f;
    auto& slider = f.column.getFaderForTest().getSlider();
    const auto cell = f.column.getLocalArea(&slider, slider.getLocalBounds());
    const auto box = synth::ui::automatedMarkerRect(cell);

    f.column.refreshMeter(0.1f);
    const auto plain = paint(f.column);

    f.automated = {"gain"};
    f.column.refreshMeter(0.1f);
    clock.set(1500.0);
    EXPECT_GT(differing(plain, paint(f.column), box), 0);

    f.automated.clear();
    f.column.refreshMeter(0.1f);
    clock.set(2500.0);
    EXPECT_EQ(differing(plain, paint(f.column), box), 0);
}

TEST(MixerAutomationMarkerTests, MasterFaderFollowsItsLaneToo) {
    ClockPin clock(1000.0);
    AudioEngine engine;
    AppUndoManager undoManager;
    GraphEditor editor{engine, &undoManager};
    synth::MacroSet macros;
    synth::ui::MixerMasterColumn column;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    editor.setSize(900, 600);
    auto master = graph.addNode(std::make_unique<MasterModule>());
    column.configure(graph, undoManager, macros, editor, engine);
    column.setSize(140, 300);
    column.setNodeId(master->nodeID);
    std::set<juce::String> automated;
    editor.onQueryAutomatedParamsForNode = [&](juce::AudioProcessorGraph::NodeID) { return automated; };
    auto& slider = column.getFaderForTest().getSlider();
    const auto cell = column.getLocalArea(&slider, slider.getLocalBounds());
    const auto box = synth::ui::automatedMarkerRect(cell);

    column.refreshMeter(0.1f);
    const auto plain = paint(column);
    EXPECT_FALSE(column.isFaderAutomatedMarkerShownForTest());

    automated = {"gain"};
    column.refreshMeter(0.1f);
    clock.set(1500.0);
    EXPECT_TRUE(column.isFaderAutomatedMarkerShownForTest());
    EXPECT_TRUE(slider.getTooltip().contains("Automated: Gain")) << slider.getTooltip();
    EXPECT_GT(differing(plain, paint(column), box), 0);

    automated.clear();
    column.refreshMeter(0.1f);
    EXPECT_FALSE(column.isFaderAutomatedMarkerShownForTest());
    EXPECT_FALSE(slider.getTooltip().contains("Automated"));
}
