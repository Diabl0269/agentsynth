#pragma once

// The panel a click on a knob's mod dot opens: the sources on the knob (ModDotSourcesPage) with, right under its
// rows, the source list (ModDotAddSourcePage) that "Add source" unfolds in place (160 ms out, 110 ms back) and
// "Pick on canvas" (ModDotCanvasPicker). One panel, never a second popup. Esc steps back (stop picking, clear the
// search, fold the list, then close); a click outside closes it, the edits already applied. The window around it is
// ModDotPanelFrame. docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotAddSourcePage.h"
#include "ModDotCanvasPicker.h"
#include "ModDotSourcesPage.h"
#include <functional>
#include <set>

class GraphEditor;

namespace synth::ui {

class ModDotController;

class ModDotPopover final
    : public juce::Component
    , private juce::Timer {
public:
    static constexpr double kOpenMs = 160.0;
    static constexpr double kCloseMs = 110.0;

    ModDotPopover(GraphEditor& editor, ModDotController& controller, juce::AudioProcessorGraph::NodeID card,
                  int destChannel, juce::Component& anchor);
    ~ModDotPopover() override;

    juce::AudioProcessorGraph::NodeID card() const noexcept { return card_; }
    int destChannel() const noexcept { return destChannel_; }
    juce::Component* anchor() const noexcept { return anchor_.getComponent(); }
    /** Milliseconds since the panel was made. */
    juce::uint32 ageMs() const noexcept { return juce::Time::getMillisecondCounter() - createdMs_; }
    /** Highlight mode: every remove button drawn in the negative colour with a pulse, and "Choose a source to
     *  remove" announced. Cleared by closing the panel. */
    void setRemoveHighlighted(bool on);
    bool isRemoveHighlighted() const noexcept { return sourcesPage_.isRemoveHighlighted(); }

    /** Follows the graph while open (called from the editor's tick): rows grow, shrink and re-read amounts. */
    void syncFromGraph();
    /** The controller is going away: close without telling it. */
    void orphan() { controllerForClose_ = nullptr; }
    /** The tallest the whole panel may be (the room on screen): the source list scrolls inside what is left. 0 = no
     *  limit. */
    void setMaxHeight(int height);

    /** The source list under the rows: unfolds it (searching focused) or folds it back. */
    void openList();
    void closeList();
    void toggleList();
    bool isListOpen() const noexcept { return listOpen_; }
    /** "Pick on canvas": the next press on an eligible module card adds it as a source; Esc or the half again stops. */
    void startPick();
    void stopPick();
    void togglePick();
    bool isPicking() const noexcept { return picker_ != nullptr; }
    /** Closes the window the panel sits in (or, with `onDismiss` set, tells the owner to). */
    void dismiss();
    std::function<void()> onDismiss;

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    // Test seams and inspection.
    ModDotSourcesPage& sourcesPage() noexcept { return sourcesPage_; }
    ModDotAddSourcePage& addSourcePage() noexcept { return addPage_; }
    bool isAnimating() const noexcept { return openAnim_.isRunning(); }
    ModDotCanvasPicker* canvasPicker() noexcept { return picker_.get(); }
    /** The height the panel is heading for with the list open or folded. */
    int settledHeight(bool listOpen) const;
    /** Picks the "New <module>" source `typeName` as a click on its row would. */
    void pickNewModule(const juce::String& typeName, int channel);

private:
    void pickSource(const ModSourceItem& item);
    void pickNode(juce::AudioProcessorGraph::NodeID node);
    void finishPick(juce::AudioProcessorGraph::NodeID attenuverter);
    std::vector<ModDotAddSourcePage::Choice> buildChoices(bool fresh) const;
    void refreshChoicesIfChanged(bool fresh);
    void setListTarget(bool open);
    void layoutParts();
    void updateListCap();
    void timerCallback() override;
    bool focusIsInside() const;
    /** Moves keyboard focus into the panel when it is on screen and focus is not already in it. */
    void ensureFocusInside();

    GraphEditor& editor_;
    ModDotController& controller_;
    juce::AudioProcessorGraph::NodeID card_;
    int destChannel_;
    juce::Component::SafePointer<juce::Component> anchor_;
    KnobModTarget target_;
    ModDotSourcesPage sourcesPage_;
    ModDotAddSourcePage addPage_;
    bool listOpen_ = false;
    float listAmount_ = 0.0f; // 0 folded .. 1 open: how much of the list is showing
    int maxHeight_ = 0;
    std::vector<int> choiceSignature_;
    std::unique_ptr<ModDotCanvasPicker> picker_;
    std::unique_ptr<ModDotCanvasPicker> retiredPicker_;
    std::set<juce::uint32> pickable_; // nodes a canvas pick may land on
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver openAnim_;
    ModDotController* controllerForClose_;
    int focusTries_ = 0;
    juce::uint32 createdMs_ = juce::Time::getMillisecondCounter();
};

} // namespace synth::ui
