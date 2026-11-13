#pragma once

// HostedOutputsPanel.h -- the Outputs popover of a Hosted Plugin card with more than one output bus: a row per bus
// (Show switch, the plugin's bus name, its channels, Joined | L/R) and two footer pairs. Every row change and footer
// action is ONE undo step, and hiding an output takes its cables in the same step. docs/control/plugin-card-layout.md.

#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/PopoverContent.h"
#include "UI/Layout/SwitchGlide.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class GraphEditor;
class ModuleComponent;

namespace synth {
class HostedPluginModule;
} // namespace synth

namespace synth::ui {

/** Runs `edit` on the Hosted Plugin node `nodeId` as one undo step, retracting the cables it removes. */
void editHostedOutputs(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId,
                       const std::function<void(HostedPluginModule&)>& edit);

/** Opens the Outputs panel on `card` (a second call closes it). */
void showHostedOutputsPanel(ModuleComponent& card);
/** Folds or unfolds the card's aux outputs as one undo step. */
void toggleHostedOutputsFold(ModuleComponent& card);

/** A row's Show switch: the shared SwitchGlide, then the bus name and its channels. */
class OutputShowToggle final : public juce::ToggleButton {
public:
    OutputShowToggle();
    void setRow(const juce::String& name, const juce::String& channels, bool on, bool locked, bool animate);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    SwitchGlide glide_;
    juce::String name_, channels_;
    bool locked_ = false;
};

class HostedOutputsPanel
    : public juce::Component
    , public PopoverContent
    , public AppUndoAware {
public:
    HostedOutputsPanel(GraphEditor& editor, juce::AudioProcessorGraph::NodeID nodeId);

    static constexpr int kWidth = 300;

    bool growsFromOpener() const override { return true; }
    void appUndoneOrRedone() override { refresh(false); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusFirstControl();

    int getNumRowsForTest() const { return rows_.size(); }
    OutputShowToggle& getShowToggleForTest(int row) { return rows_[row]->show; }
    CardSegmentedSwitch* getJoinSwitchForTest(int row) { return rows_[row]->join.get(); }
    juce::Button& getFooterButtonForTest(int index) { return *footer_[index]; }

private:
    struct Row {
        explicit Row(int bus)
            : busIndex(bus) {}
        int busIndex;
        OutputShowToggle show;
        std::unique_ptr<CardSegmentedSwitch> join;
    };

    HostedPluginModule* module() const;
    void refresh(bool animate);
    void rebuildRows();
    void footerAction(int index);

    GraphEditor& editor_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    juce::OwnedArray<Row> rows_;
    juce::OwnedArray<juce::TextButton> footer_;
    juce::Label title_{{}, "Outputs"};
    ArrowKeyNavigation arrows_{*this};
};

} // namespace synth::ui
