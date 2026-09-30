#pragma once

#include <array>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>

// MixerSectionLayout.h (docs/mixer/panel.md#shared-sections): the Inserts/Sends/EQ section heights
// and hidden flags every mixer column shares. The panel owns one; columns read it and
// resolve the same geometry from the same column height, so every fader starts on the same line.
namespace synth::ui {

enum class MixerSection { Inserts = 0, Sends = 1, Eq = 2 };

class MixerSectionLayout {
public:
    static constexpr int kSectionCount = 3;
    static constexpr int kDividerHeight = 6;
    static constexpr int kCollapsedHeight = 14;
    static constexpr int kInsertRowHeight = 18;
    static constexpr int kSendRowHeight = 20;
    static constexpr int kEqHeight = 28;
    static constexpr int kDefaultInsertRows = 5; // 4 inserts plus the link row: 90 px
    static constexpr int kDefaultSendRows = 3;   // 2 sends plus "+ Send": 60 px
    static constexpr int kMaxRows = 16;

    // Column chrome every column kind lays out around the sections (pixels).
    static constexpr int kColumnInset = 2;
    static constexpr int kHeaderHeight = 24;
    static constexpr int kSourceLineHeight = 14;
    static constexpr int kPanHeight = 28;
    static constexpr int kMeterReadoutHeight = 12;
    static constexpr int kMsRowHeight = 20;
    static constexpr int kMinFaderHeight = 56;

    // Settings keys (the app-wide user settings file, like the bottom dock's height).
    static constexpr const char* kInsertsHeightKey = "mixerSectionInsertsHeight";
    static constexpr const char* kSendsHeightKey = "mixerSectionSendsHeight";
    static constexpr const char* kInsertsHiddenKey = "mixerSectionInsertsHidden";
    static constexpr const char* kSendsHiddenKey = "mixerSectionSendsHidden";
    static constexpr const char* kEqHiddenKey = "mixerSectionEqHidden";

    /** Column-local y positions and heights, all in pixels. */
    struct Geometry {
        std::array<int, kSectionCount> sectionTop{};
        std::array<int, kSectionCount> sectionHeight{};
        std::array<int, kSectionCount> dividerTop{};
        int panTop = 0;
        int panHeight = 0;
        int readoutTop = 0;
        int faderTop = 0;
        int faderHeight = 0;
        int msTop = 0;
    };

    static int rowHeightOf(MixerSection section) noexcept;
    static int defaultHeightOf(MixerSection section) noexcept;
    /** `px` rounded to whole rows of `section`, clamped to [1, kMaxRows] rows. */
    static int snapHeight(MixerSection section, int px) noexcept;
    static const char* nameOf(MixerSection section) noexcept;

    /** The user's height for `section` (EQ is fixed), before any fitting to a short column. */
    int getRequestedHeight(MixerSection section) const noexcept;
    void setRequestedHeight(MixerSection section, int px);
    int getRowCount(MixerSection section) const noexcept;
    void resetToDefault(MixerSection section);

    bool isHidden(MixerSection section) const noexcept;
    void setHidden(MixerSection section, bool hidden);
    void toggleHidden(MixerSection section) { setHidden(section, !isHidden(section)); }

    /** Every column's geometry for a column `columnHeight` px tall. */
    Geometry resolve(int columnHeight) const noexcept;
    /** The column height at which every section gets its full height and the fader its minimum. */
    int requiredColumnHeight() const noexcept;

    // ---- Divider gestures (hover, drag, reset), shared by the columns ----
    int getHoveredDivider() const noexcept { return hoveredDivider_; }
    void setHoveredDivider(int dividerIndex);
    int getDraggingDivider() const noexcept { return draggingDivider_; }
    void beginDividerDrag(MixerSection section);
    /** `deltaPx`: pointer travel since beginDividerDrag(), in screen pixels (down is positive). */
    void dragDividerBy(MixerSection section, int deltaPx);
    void endDividerDrag();

    void loadFrom(const juce::PropertySet& settings);
    void saveTo(juce::PropertySet& settings) const;

    /** Heights or hidden flags changed: every column re-lays out. */
    std::function<void()> onGeometryChanged;
    /** Hover or drag state changed: dividers repaint. */
    std::function<void()> onAppearanceChanged;
    /** A gesture finished (drag end, show/hide, reset): the owner persists. */
    std::function<void()> onCommitted;

private:
    void notifyGeometry();
    void notifyAppearance();
    void notifyCommitted();

    std::array<int, kSectionCount> requested_{kDefaultInsertRows * kInsertRowHeight, kDefaultSendRows* kSendRowHeight,
                                              kEqHeight};
    std::array<bool, kSectionCount> hidden_{false, false, false};
    int hoveredDivider_ = -1;
    int draggingDivider_ = -1;
    int dragStartHeight_ = 0;
};

} // namespace synth::ui
