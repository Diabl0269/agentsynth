#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlChip.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControlMoreRow.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

class LaneControlTray;

// The row of control chips on a lane header (an automation lane's or a mod lane's) and everything that goes with it:
// the "+" button and the row it opens, and edit mode with its tray. The strip holds no lane state: what each chip does,
// whether it can act and what its tooltip adds are asked of the owner through `Actions`, and the set of shown controls
// is the shared LaneControlSet's (null: the defaults, nothing saved).
//
// Layout: chips run left to right in the owner's bounds for the strip, then the "+" button. A chip that does not fit
// is not shown on the header and is listed in the "+" row instead. In edit mode (Edit controls in the "+" row) the
// header gets an accent outline, the "+" becomes a pressed pencil, and a 34 px tray slides down under the header with
// the controls that are not shown and a Done button. A chip is dragged between header and tray (or clicked, or
// Space) to show or hide it, and dragged sideways, or moved with Alt+Left/Right, to reorder; Left/Right walk the chips;
// Done and Esc leave. Only one strip in a timeline is in edit mode at a time. Message thread only.
class LaneControlStrip
    : public juce::Component
    , private LaneControlSet::Listener {
public:
    static constexpr int kGap = 2; // between chips
    static constexpr int kTrayHeight = 34;
    static constexpr int kTrayGap = 2; // the lane below sits this far under the tray
    static constexpr double kTrayMs = 280.0;
    static constexpr double kSlideMs = 160.0;
    static constexpr double kGrowMs = 220.0;

    struct Actions {
        /** Runs the control; `anchor` is the chip (or the "+" button) a menu opens beside. May destroy the owner. */
        std::function<void(LaneControl, juce::Component& anchor)> perform;
        /** Whether the control can act on this lane now; null: always. */
        std::function<bool(LaneControl)> isEnabled;
        /** What the tooltip adds after "<Name>: <description>" (the current record mode, why it is disabled). */
        std::function<juce::String(LaneControl)> detail;
        /** Drawn lit (Mute while muted). */
        std::function<bool(LaneControl)> isLit;
        /** The picture's colour in place of the control's own. */
        std::function<std::optional<juce::Colour>(LaneControl)> iconColour;
    };

    LaneControlStrip(LaneControlKind kind, LaneControlSet* set, Actions actions);
    ~LaneControlStrip() override;

    LaneControlKind getKind() const noexcept { return kind_; }

    /** Re-reads the shown set and every chip's enabled / lit / tooltip state. */
    void refresh();
    /** Re-reads only the chips' enabled / lit / tooltip state. */
    void refreshStates();

    /** The chip size, px: 22, or less on a short header. */
    void setChipSize(int size);
    int getChipSize() const noexcept { return chipSize_; }
    /** The width the chips and the "+" button want at the current set, px. */
    int preferredWidth() const;

    /** The controls on the header now (those that fit, in order) and those the "+" row lists. */
    std::vector<LaneControl> visibleControls() const;
    std::vector<LaneControl> hiddenControls() const;
    LaneControlChip* chipFor(LaneControl control) const;
    LaneControlChip& moreButton() noexcept { return *more_; }

    // ---- the "+" row ----
    void toggleRow();
    void openRow();
    void closeRow();
    LaneControlMoreRow* getRow() noexcept { return row_.get(); }
    bool isRowOpen() const noexcept { return row_ != nullptr && row_->isOpen(); }
    /** Re-places an open row beside the owner after the owner moved or scrolled. */
    void repositionRow();

    // ---- edit mode (LaneControlStripEdit.cpp) ----
    void enterEdit();
    void exitEdit();
    bool isEditing() const noexcept { return editing_; }
    /** 0..1: how far the tray is open (and the header outline in). */
    float editAmount() const noexcept { return trayOpen_; }
    /** The height the tray adds under the owner, px (the tray and the gap), mid-animation too. */
    int trayExtent() const;
    /** Places the tray under `ownerBounds` (in the owner's parent coordinates). Call after the owner is placed. */
    void placeTray(juce::Rectangle<int> ownerBounds);
    LaneControlTray* getTray() noexcept { return tray_.get(); }
    /** The chips in focus order while editing: shown ones, then hidden ones. */
    std::vector<LaneControlChip*> editOrder() const;
    /** Toggles a control shown / hidden (the click, Space and the keyboard path). */
    void toggleControl(LaneControl control);
    /** Moves a shown control `delta` places (Alt+Left/Right). False at an end or when it is not shown. */
    bool moveControl(LaneControl control, int delta);
    /** Steps the tray / slide / grow animations in flight by hand (t in 0..1; 1 lands them). */
    void stepForTest(float t);
    bool isAnimating() const;

    /** Fires while the tray's height changes (the owner's layout must follow). */
    std::function<void()> onTrayExtentChanged;
    /** Fires when the preferred width changed (edit mode, a new set): the owner re-lays out. */
    std::function<void()> onPreferredWidthChanged;
    /** Fires when the edit outline needs repainting. */
    std::function<void()> onEditLookChanged;
    /** A right-click on a chip or the tray: the owner's menu opens at the screen position. */
    std::function<void(juce::Point<int>)> onContextMenu;

    void resized() override;
    void paint(juce::Graphics&) override {}

private:
    friend class LaneControlTray;

    void laneControlsChanged(LaneControlKind kind) override;
    void buildChips();
    void applyShown(const std::vector<LaneControl>& shown, bool animate);
    void layoutChips(bool animate);
    int pitch() const;
    int headerChipWidth() const;
    std::map<LaneControlChip*, juce::Rectangle<int>> targetBounds(std::optional<int> gapAt = std::nullopt) const;
    void slideTo(const std::map<LaneControlChip*, juce::Rectangle<int>>& targets, bool animate);
    void growChip(LaneControlChip& chip);
    void stopGrow();
    juce::Point<int> rowAnchor() const;
    void perform(LaneControl control, juce::Component& anchor);
    void wireChip(LaneControlChip& chip);
    bool editKey(LaneControlChip& chip, const juce::KeyPress& key);
    void commitShown(std::vector<LaneControl> next);
    std::vector<LaneControl> fitting() const;
    float effectivePitch(int count) const;

    // Edit mode (LaneControlStripEdit.cpp).
    void placeEditChips();
    void layoutTrayChips();
    void applyTrayOpen(float open);
    void animateTray(bool open);
    void landTray();
    void chipPressed(LaneControlChip& chip, const juce::MouseEvent& e);
    void chipDragged(LaneControlChip& chip, const juce::MouseEvent& e);
    void chipReleased(LaneControlChip& chip, const juce::MouseEvent& e);
    void beginDrag(LaneControlChip& chip, const juce::MouseEvent& e);
    void finishDrag(LaneControlChip& chip, juce::Point<int> screenPos);
    enum class DropZone { None, Header, Tray };
    DropZone zoneAt(juce::Point<int> screenPos) const;
    int insertionIndexAt(juce::Point<int> screenPos, LaneControl dragged) const;
    juce::Component* dragLayer() const;
    void focusChip(LaneControl control);

    LaneControlKind kind_;
    LaneControlSet* set_;
    Actions actions_;
    int chipSize_ = LaneControlChip::kSize;

    std::vector<LaneControl> shown_; // in header order, whether or not each fits
    std::map<LaneControl, std::unique_ptr<LaneControlChip>> chips_;
    std::unique_ptr<LaneControlChip> more_;
    std::unique_ptr<LaneControlMoreRow> row_;

    bool editing_ = false;
    float trayOpen_ = 0.0f;
    std::unique_ptr<LaneControlTray> tray_;
    juce::Rectangle<int> ownerBounds_;

    // The drag in progress.
    LaneControlChip* pressed_ = nullptr;
    juce::Point<int> pressScreen_;
    juce::Point<int> grabOffset_;
    bool dragging_ = false;
    juce::Component::SafePointer<juce::Component> dragParent_;
    std::optional<int> dragGapAt_;

    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver trayDriver_;
    AnimationDriver slideDriver_;
    AnimationDriver growDriver_;
    std::map<LaneControlChip*, juce::Rectangle<int>> slideFrom_;
    std::map<LaneControlChip*, juce::Rectangle<int>> slideTo_;
    std::vector<LaneControlChip*> growing_;
    float trayFrom_ = 0.0f;
    float trayTo_ = 0.0f;
    bool animateNextLayout_ = false;
    std::unique_ptr<LaneControlChip> ghost_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneControlStrip)
};

// The slim strip under an edited header: the controls the header does not show, dimmed, and a Done button.
class LaneControlTray : public juce::Component {
public:
    explicit LaneControlTray(LaneControlStrip& strip);

    juce::TextButton& getDoneButton() noexcept { return done_; }
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    /** The area chips lie in, in this component's coordinates. */
    juce::Rectangle<int> chipArea() const;

    static constexpr int kPad = 6;
    static constexpr int kPitch = 24;
    static constexpr int kDoneWidth = 40;
    static constexpr int kDoneHeight = 24;

private:
    LaneControlStrip& strip_;
    juce::TextButton done_{"Done"};
};

} // namespace synth::ui
