#pragma once

// The panel a click on a knob's mod dot opens: the sources on the knob (ModDotSourcesPage) and, in the same
// panel, the list of sources to add (ModDotAddSourcePage). It lives in a juce::CallOutBox the controller launches
// under the dot, arrow pointing at it. Switching pages cross-fades the content over 110 ms while the height
// settles over 160 ms; there is never a second popup. Esc steps back (clear the search, then the sources page,
// then closed); a click outside closes it, the edits already applied.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotAddSourcePage.h"
#include "ModDotSourcesPage.h"
#include "UI/Layout/CalloutReveal.h"
#include <functional>

class GraphEditor;

namespace synth::ui {

class ModDotController;

class ModDotPopover final
    : public juce::Component
    , private juce::Timer {
public:
    enum class Page { Sources, AddSource };
    static constexpr double kFadeMs = 110.0;
    static constexpr double kSettleMs = 160.0;

    ModDotPopover(GraphEditor& editor, ModDotController& controller, juce::AudioProcessorGraph::NodeID card,
                  int destChannel, juce::Component& anchor);
    ~ModDotPopover() override;

    juce::AudioProcessorGraph::NodeID card() const noexcept { return card_; }
    int destChannel() const noexcept { return destChannel_; }
    Page page() const noexcept { return page_; }
    juce::Component* anchor() const noexcept { return anchor_.getComponent(); }
    /** Milliseconds since the panel was made (a callout ignores a click on its dot for its first 200 ms). */
    juce::uint32 ageMs() const noexcept { return juce::Time::getMillisecondCounter() - createdMs_; }
    /** Highlight mode: every remove button drawn in the negative colour with a pulse, and "Choose a source to
     *  remove" announced. Cleared by closing the panel. */
    void setRemoveHighlighted(bool on);
    bool isRemoveHighlighted() const noexcept { return sourcesPage_.isRemoveHighlighted(); }

    /** Follows the graph while open (called from the editor's tick): rows grow, shrink and re-read amounts. */
    void syncFromGraph();
    /** The controller is going away: close without telling it. */
    void orphan() { controllerForClose_ = nullptr; }
    /** The tallest the panel may grow (the room left on the side of the dot the callout opened on): the Add source
     *  list then scrolls instead of the callout re-placing itself on another side. 0 = no limit. */
    void setMaxHeight(int height);
    /** Limits the panel's height to the room on the side of the dot `box` opened on inside `area`, so growing to the
     *  Add source page never makes the callout re-place itself on another side. */
    void keepSideOf(const juce::CallOutBox& box, juce::Rectangle<int> dot, juce::Rectangle<int> area);
    /** Pure: the panel height that still fits on the side of `dot` that a callout at `box` is on. */
    static int roomOnSide(juce::Rectangle<int> box, juce::Rectangle<int> dot, juce::Rectangle<int> area,
                          int borderSize);
    void showSources();
    void showAddSource();
    /** Closes the callout the panel sits in (or, with `onDismiss` set, tells the owner to). */
    void dismiss();
    std::function<void()> onDismiss;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    // Test seams and inspection.
    ModDotSourcesPage& sourcesPage() noexcept { return sourcesPage_; }
    ModDotAddSourcePage& addSourcePage() noexcept { return addPage_; }
    bool isSwitching() const noexcept { return pageAnim_.isRunning(); }
    CalloutReveal& reveal() noexcept { return reveal_; }

private:
    void pickSource(const ModSourceItem& item);
    std::vector<ModDotAddSourcePage::Choice> buildChoices() const;
    void switchTo(Page page);
    void applyHeight(int height);
    void pageHeightChanged(Page which);
    ModDotPage& pageComponent(Page page);
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
    Page page_ = Page::Sources;
    CalloutReveal reveal_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver pageAnim_;
    ModDotController* controllerForClose_;
    int focusTries_ = 0;
    juce::uint32 createdMs_ = juce::Time::getMillisecondCounter();
};

} // namespace synth::ui
