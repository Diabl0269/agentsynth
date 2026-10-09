#pragma once

// The panel's top part: one row per source on the knob and the "Add source" split button (source list, pick on canvas).
// Edits go straight to the graph (amount drags and typed amounts are one undo step each); rows follow the graph while
// the panel is open. A removed row shrinks toward its centre and the rows below then close the gap; an undo makes room,
// grows the row back and fades an outline around it (ExitEnterTimeline.h); a source the user adds grows in at once.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "KnobModSources.h"
#include "ModDotPage.h"
#include "ModDotSourceRow.h"
#include "UI/Layout/ExitEnterRowSlots.h"
#include "UI/Layout/ExitEnterTimeline.h"
#include "UI/Layout/SplitButton.h"
#include <memory>
#include <vector>

class GraphEditor;

namespace synth::ui {

class ModDotController;

class ModDotSourcesPage final : public ModDotPage {
public:
    static constexpr int kTitleHeight = 30;
    static constexpr int kSplitRowHeight = 32;
    static constexpr double kGrowMs = 160.0; // a source the user adds

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
    /** Highlights every row's remove button (rows that grow in later join); cleared with the panel. */
    void setRemoveHighlighted(bool on);
    bool isRemoveHighlighted() const noexcept { return removeHighlighted_; }

    std::function<void()> onAddSourceRequested;
    std::function<void()> onPickOnCanvasRequested;

    // Test seams and inspection.
    int rowCount() const; // rows not shrinking out
    ModDotSourceRow* rowAt(int index) const;
    ModDotSourceRow* rowFor(juce::AudioProcessorGraph::NodeID attenuverterId) const;
    SplitButton& splitButton() noexcept { return split_; }
    juce::Button& addButton() noexcept { return split_.leftHalf(); }
    juce::String titleText() const;
    bool isAnimating() const noexcept { return anim_.isRunning(); }
    /** Test seam: animate even though the page is not showing. */
    void setForceAnimateForTest(bool on) noexcept { forceAnimate_ = on; }
    /** Test seam: puts a removal or restore motion at `elapsedMs` after it began (what each frame does). */
    void applyTimelineAtMs(double elapsedMs);
    const synth::ui::ExitEnterTimeline& timeline() const noexcept { return timeline_; }
    /** The row for `attenuverterId` including one shrinking away (rowFor skips it); null when there is none. */
    ModDotSourceRow* animatingRowFor(juce::AudioProcessorGraph::NodeID attenuverterId) const;
    /** The height of the slot `attenuverterId`'s row holds now, and the alpha of the outline around a restored row. */
    int slotHeightFor(juce::AudioProcessorGraph::NodeID attenuverterId) const;
    float outlineAlphaFor(juce::AudioProcessorGraph::NodeID attenuverterId) const;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Entry : ExitEnterSlot {
        std::unique_ptr<ModDotSourceRow> row;
    };

    void wireRow(ModDotSourceRow& row);
    void animateLayout();
    void growNewRows();
    void startRemovalMotion();
    void landMotion();
    void layoutRows();
    void finishLeaving();
    int restoreSerial() const;
    bool followsSameSources(const std::vector<KnobModSource>& sources) const;
    void navigate(juce::Component* from, int step);
    void applyAmount(ModDotSourceRow& row, float amount);

    GraphEditor& editor_;
    ModDotController& controller_;
    juce::AudioProcessorGraph::NodeID card_;
    int destChannel_;
    KnobModTarget target_;
    std::vector<Entry> entries_;
    SplitButton split_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    int dividerY_ = 0;
    bool removeHighlighted_ = false;
    synth::ui::ExitEnterTimeline timeline_;
    bool motionActive_ = false;
    bool forceAnimate_ = false;
    bool reducedMotion_ = false;
    int restoreSerialSeen_ = 0;
};

} // namespace synth::ui
