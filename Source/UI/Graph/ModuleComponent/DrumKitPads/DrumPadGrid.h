#pragma once

// The Drum Kit card's pad grid: four pads a row, pad 1 top left, one Tab stop. A click plays a pad and picks it,
// Shift+click and the arrow keys pick it without a sound, Space and Return play the picked pad, H switches the picked
// pad's Shown off or on. A pad that is not shown leaves the grid and the pads after it move up. A right-click or
// Shift+F10 opens the pad menu; a sound dragged from the Sounds view drops onto the pad under the pointer.
// docs/modules/drum-kit.md#the-card.

#include "Modules/DrumKit/DrumKitModule.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

namespace synth::ui {

class DrumPadGrid
    : public juce::Component
    , public juce::TooltipClient
    , public synth::testmode::StateSource
    , public KeyboardContextMenuProvider
    , public SoundDropAdapter
    , public juce::FileDragAndDropTarget {
public:
    static constexpr int kColumns = 4;
    static constexpr int kGap = 4;
    static constexpr int kTallPad = 44;
    static constexpr int kShortPad = 38;
    static constexpr int kTallPadLimit = 16; // above this many pads they are short
    static constexpr double kFlashMs = 120.0;
    static constexpr double kRingSlideMs = 120.0;
    static constexpr double kShrinkMs = 110.0; ///< a pad leaving shrinks (ease-in)
    static constexpr double kSlideMs = 160.0;  ///< the others make room
    static constexpr double kReducedMs = 80.0; ///< Reduce Motion: the pad fades in place

    /** The grid's height for `count` shown pads. */
    static int heightFor(int count);

    /** The kit must outlive this. */
    explicit DrumPadGrid(DrumKitModule& kit);
    ~DrumPadGrid() override;

    /** The picked pad, 0-based; one of the pads within the count. It stays picked after its Shown switch goes off,
     *  so the switch can be turned back on from its controls. */
    int picked() const noexcept { return picked_; }
    /** H on the grid: asks the owner to flip pad `index`'s Shown switch. */
    std::function<void(int index)> onToggleShown;
    /** How many pads the grid draws (the shown ones within the count). */
    int visibleCount() const noexcept { return (int)order_.size(); }
    bool isPadVisible(int index) const noexcept;
    /** True while pads slide to make room for one that left or came back. */
    bool isSliding() const noexcept { return sliding_; }
    /** Test seam: the slide's driver, to step or finish it without VBlank frames. */
    AnimationDriver& slideDriverForTest() noexcept { return slide_; }
    /** Picks `index` (clamped to the shown pads); `play` also plays it. */
    void pick(int index, bool play);
    /** Called after every pick with the pad and whether it played. */
    std::function<void(int index, bool played)> onPicked;
    /** Right-click or Shift+F10 on pad `index` (already picked); `keyboard` anchors the menu at the pad. */
    std::function<void(int index, bool keyboard)> onContextMenu;
    /** A sound was dropped on pad `index`. */
    std::function<void(int index, const juce::File& file)> onFileDropped;
    /** The pad a sound drag is over changed (dropTarget()). */
    std::function<void()> onDropTargetChanged;
    /** The pad a sound being dragged would land on now, or -1. */
    int dropTarget() const noexcept { return dropTarget_; }

    /** Re-reads the pad count, names and kinds. */
    void refresh();

    /** Local bounds of pad `index` (empty while it is not shown). */
    juce::Rectangle<int> padBounds(int index) const;
    /** The pad under `p` (local), or -1. */
    int padAt(juce::Point<int> p) const;
    /** What a screen reader says for pad `index`: "Snare pad, built-in sound, 2 of 8". */
    juce::String padTitle(int index) const;
    juce::String padTooltip(int index) const;
    /** True for the 120 ms after pad `index` played from the card (on screen, with Animations on). */
    bool isFlashing(int index) const;
    /** The child that stands for pad `index` to a screen reader (hidden beyond the count). */
    juce::Component& padCell(int index);
    /** Where the selection ring is drawn now (it slides between pads). */
    juce::Rectangle<float> ringBounds() const noexcept { return ringRect_; }

    juce::String getTooltip() override;
    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void addTestModeState(juce::DynamicObject& into) const override;
    bool showContextMenuForKeyboardFocus() override;

    // While a sound is dragged only the pads hit-test, so a drop in a gap reaches the card and the canvas.
    bool hitTest(int x, int y) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    class Cell;
    friend class Cell;

    void paintPad(juce::Graphics& g, int index, juce::Rectangle<int> area) const;
    void moveRingTo(int index, bool animate);
    void setDropTarget(int index);
    bool isSoundDragLive() const;
    void paintDropOutlines(juce::Graphics& g) const;
    int slotOf(int pad) const noexcept;
    void layoutCells();
    void startSlide(const std::array<juce::Rectangle<int>, synth::drums::kMaxPads>& before);
    void stepSlide(float t);
    void finishSlide();
    bool pickKey(const juce::KeyPress& key);

    DrumKitModule& kit_;
    std::array<std::unique_ptr<Cell>, synth::drums::kMaxPads> cells_;
    int flashing_ = -1;
    int count_ = 0;
    std::vector<int> order_; ///< the pads drawn, in order: the shown ones within the count
    bool laidOut_ = false;
    bool sliding_ = false;
    int slideWidth_ = 0;
    bool slideReduced_ = false;
    std::array<std::pair<juce::Rectangle<float>, juce::Rectangle<float>>, synth::drums::kMaxPads> moves_;
    AnimationDriver slide_;
    int picked_ = 0;
    int auditioned_ = -1;
    int dropTarget_ = -1;
    juce::Rectangle<float> ringRect_;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver ringSlide_;
    AnimationDriver flash_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPadGrid)
};

} // namespace synth::ui
