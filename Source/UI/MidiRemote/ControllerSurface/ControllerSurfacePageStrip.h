#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// ControllerSurfacePageStrip.h -- FRO142 (docs/control/midi-remote.md#pages,
// docs/control/midi-remote-ui.md#pages): the row of page buttons ("1", "2", ...) above the surface
// grid for the selected controller, plus a trailing "+" that adds a page. Shown even with a single
// page (just "1" and "+") so the feature is discoverable. Knows nothing about RemoteEngine or
// MidiLearnController -- MidiRemotePanelComponent supplies the page count and reacts to the
// callbacks, same shape as ControllerSurfaceToolbar.
namespace synth::ui {

class ControllerSurfacePageStrip : public juce::Component {
public:
    ControllerSurfacePageStrip();
    ~ControllerSurfacePageStrip() override;

    /** Rebuilds the button row for pages 1..effectivePageCount (>= 1), highlighting `activePage`.
     *  Cheap no-op if neither actually changed -- called on every profile-selection and every
     *  active-page-changed notification. */
    void setPages(int effectivePageCount, int activePage);

    int getEffectivePageCountForTest() const noexcept { return effectivePageCount_; }
    int getActivePageForTest() const noexcept { return activePage_; }
    /** Test seam: drives a real synthesized click on page button `page` (1-based), same as
     *  Source/UI/CLAUDE.md's "test the real mouse path" convention -- no direct callback call. */
    juce::Button* getPageButtonForTest(int page) const;
    juce::Button& getAddButtonForTest() noexcept { return addButton_; }

    /** Fired when the user clicks an inactive page button. Never fired for the already-active page
     *  (clicking it again is a no-op, same as every other single-select button row here). */
    std::function<void(int page)> onPageSelected;
    /** The trailing "+" button. */
    std::function<void()> onAddPageRequested;
    /** Right-click "Delete page" on a button for `page` > 1 (page 1 offers no such menu -- there is
     *  always at least one page). */
    std::function<void(int page)> onDeletePageRequested;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static constexpr int kHeight = 26;
    static constexpr int kButtonSize = 24;
    static constexpr int kGap = 4;

private:
    // A page button forwards a right-click to the strip instead of the ordinary onClick -- see the
    // .cpp for why a plain juce::TextButton has no such hook to begin with.
    class PageButton;

    void rebuildButtons();
    void showDeletePageMenu(int page);

    int effectivePageCount_ = 1;
    int activePage_ = 1;
    juce::OwnedArray<PageButton> pageButtons_;
    juce::TextButton addButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllerSurfacePageStrip)
};

} // namespace synth::ui
