#pragma once

// The panel's first page: one row per source on the knob and "+ Add source". Edits go straight to the graph
// (amount drags and typed amounts are one undo step each); rows follow the graph while the panel is open, so an
// undo, a removal or a new source grows or shrinks its row in place. docs/modules/modulation.md#the-mod-dot-menu.

#include "KnobModSources.h"
#include "ModDotPage.h"
#include "ModDotSourceRow.h"
#include <memory>
#include <vector>

class GraphEditor;

namespace synth::ui {

class ModDotController;

class ModDotSourcesPage final : public ModDotPage {
public:
    static constexpr int kTitleHeight = 30;
    static constexpr int kAddHeight = 32;
    static constexpr double kGrowMs = 160.0;
    static constexpr double kShrinkMs = 110.0;

    ModDotSourcesPage(GraphEditor& editor, ModDotController& controller, juce::AudioProcessorGraph::NodeID card,
                      int destChannel, KnobModTarget target);
    ~ModDotSourcesPage() override;

    int preferredHeight() const override;
    void focusEntry() override;

    /** Re-reads the knob's sources: new ones grow in, gone ones shrink out, amounts follow. `fresh` asks the
     *  engine now instead of the editor's cache (after an edit made just before). */
    void sync(bool fresh = true);
    /** Marks `attenuverterId`'s row as the chosen one (and tells the controller). */
    void select(juce::AudioProcessorGraph::NodeID attenuverterId);

    std::function<void()> onAddSourceRequested;

    // Test seams and inspection.
    int rowCount() const; // rows not shrinking out
    ModDotSourceRow* rowAt(int index) const;
    ModDotSourceRow* rowFor(juce::AudioProcessorGraph::NodeID attenuverterId) const;
    juce::Button& addButton() noexcept;
    juce::String titleText() const;
    bool isAnimating() const noexcept { return anim_.isRunning(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class AddButton;
    struct Entry {
        std::unique_ptr<ModDotSourceRow> row;
        float from = 0.0f;
        float to = 0.0f;
        float current = 0.0f;
        bool leaving = false;
    };

    void wireRow(ModDotSourceRow& row);
    void animateLayout();
    void layoutRows();
    void finishLeaving();
    void navigate(juce::Component* from, int step);
    void applyAmount(ModDotSourceRow& row, float amount);

    GraphEditor& editor_;
    ModDotController& controller_;
    juce::AudioProcessorGraph::NodeID card_;
    int destChannel_;
    KnobModTarget target_;
    std::vector<Entry> entries_;
    std::unique_ptr<AddButton> addButton_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    int dividerY_ = 0;
};

} // namespace synth::ui
