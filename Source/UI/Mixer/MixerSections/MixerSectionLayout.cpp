// Concern: MixerSectionLayout's shared section state -- snapping, fitting the sections into a
// column, the divider gestures and persistence.
#include "MixerSectionLayout.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
size_t indexOf(MixerSection section) noexcept { return (size_t)section; }

// Everything a column lays out that is not a section, the pan knob or the fader itself.
constexpr int fixedChrome(int sourceLineHeight) noexcept {
    return 2 * MixerSectionLayout::kColumnInset + MixerSectionLayout::kHeaderHeight + sourceLineHeight +
           MixerSectionLayout::kSectionCount * MixerSectionLayout::kDividerHeight +
           MixerSectionLayout::kMeterReadoutHeight + MixerSectionLayout::kMsRowHeight;
}
} // namespace

int MixerSectionLayout::rowHeightOf(MixerSection section) noexcept {
    switch (section) {
    case MixerSection::Inserts:
        return kInsertRowHeight;
    case MixerSection::Sends:
        return kSendRowHeight;
    case MixerSection::Eq:
        return kEqHeight;
    }
    return kInsertRowHeight;
}

int MixerSectionLayout::defaultHeightOf(MixerSection section) noexcept {
    switch (section) {
    case MixerSection::Inserts:
        return kDefaultInsertRows * kInsertRowHeight;
    case MixerSection::Sends:
        return kDefaultSendRows * kSendRowHeight;
    case MixerSection::Eq:
        return kEqHeight;
    }
    return kEqHeight;
}

const char* MixerSectionLayout::nameOf(MixerSection section) noexcept {
    switch (section) {
    case MixerSection::Inserts:
        return "Inserts";
    case MixerSection::Sends:
        return "Sends";
    case MixerSection::Eq:
        return "EQ";
    }
    return "";
}

// The EQ section is one fixed-height curve, so it never takes a user height: a drag on its divider
// snaps straight back to kEqHeight.
int MixerSectionLayout::snapHeight(MixerSection section, int px) noexcept {
    if (section == MixerSection::Eq)
        return kEqHeight;
    const int row = rowHeightOf(section);
    const int rows = (int)std::lround((double)px / (double)row);
    return std::clamp(rows, 1, kMaxRows) * row;
}

int MixerSectionLayout::getRequestedHeight(MixerSection section) const noexcept { return requested_[indexOf(section)]; }

void MixerSectionLayout::setRequestedHeight(MixerSection section, int px) {
    const int snapped = snapHeight(section, px);
    if (requested_[indexOf(section)] == snapped)
        return;
    requested_[indexOf(section)] = snapped;
    notifyGeometry();
}

int MixerSectionLayout::getRowCount(MixerSection section) const noexcept {
    return getRequestedHeight(section) / rowHeightOf(section);
}

void MixerSectionLayout::resetToDefault(MixerSection section) {
    setRequestedHeight(section, defaultHeightOf(section));
    notifyCommitted();
}

bool MixerSectionLayout::isHidden(MixerSection section) const noexcept { return hidden_[indexOf(section)]; }

void MixerSectionLayout::setHidden(MixerSection section, bool hidden) {
    if (hidden_[indexOf(section)] == hidden)
        return;
    hidden_[indexOf(section)] = hidden;
    notifyGeometry();
    notifyCommitted();
}

// Fits the sections into a column `columnHeight` px tall. With room to spare every section gets its
// requested height (a hidden one its 14 px strip) and the fader takes the rest. When the column is
// too short to also leave the fader kMinFaderHeight, the shortfall is taken lowest priority first --
// EQ, then Sends, then Inserts, then the pan knob -- so the fader is the last thing to give way.
// The result depends only on this layout and `columnHeight`, which is what keeps every column's
// sections, and so every fader, on the same lines.
MixerSectionLayout::Geometry MixerSectionLayout::resolve(int columnHeight) const noexcept {
    std::array<int, kSectionCount> heights{};
    for (size_t i = 0; i < heights.size(); ++i)
        heights[i] = hidden_[i] ? kCollapsedHeight : requested_[i];
    int pan = kPanHeight;

    const int sourceLine = sourceLineHeight();
    int deficit = fixedChrome(sourceLine) + kMinFaderHeight + pan - columnHeight;
    for (const auto& h : heights)
        deficit += h;
    auto reclaim = [&deficit](int& budget) {
        if (deficit <= 0)
            return;
        const int taken = std::min(budget, deficit);
        budget -= taken;
        deficit -= taken;
    };
    reclaim(heights[indexOf(MixerSection::Eq)]);
    reclaim(heights[indexOf(MixerSection::Sends)]);
    reclaim(heights[indexOf(MixerSection::Inserts)]);
    reclaim(pan);

    Geometry g;
    int y = kColumnInset + kHeaderHeight;
    g.sourceLineTop = y;
    g.sourceLineHeight = sourceLine;
    y += sourceLine;
    for (size_t i = 0; i < heights.size(); ++i) {
        g.sectionTop[i] = y;
        g.sectionHeight[i] = heights[i];
        y += heights[i];
        g.dividerTop[i] = y;
        y += kDividerHeight;
    }
    g.panTop = y;
    g.panHeight = pan;
    y += pan;
    g.readoutTop = y;
    y += kMeterReadoutHeight;
    g.faderTop = y;
    g.msTop = std::max(y, columnHeight - kColumnInset - kMsRowHeight);
    g.faderHeight = std::max(0, g.msTop - y);
    return g;
}

int MixerSectionLayout::requiredColumnHeight() const noexcept {
    int total = fixedChrome(sourceLineHeight()) + kPanHeight + kMinFaderHeight;
    for (size_t i = 0; i < requested_.size(); ++i)
        total += hidden_[i] ? kCollapsedHeight : requested_[i];
    return total;
}

bool MixerSectionLayout::setSourceLineVisible(bool visible, bool notify) {
    if (sourceLineVisible_ == visible)
        return false;
    sourceLineVisible_ = visible;
    if (notify)
        notifyGeometry();
    return true;
}

void MixerSectionLayout::setHoveredDivider(int dividerIndex) {
    if (hoveredDivider_ == dividerIndex)
        return;
    hoveredDivider_ = dividerIndex;
    notifyAppearance();
}

void MixerSectionLayout::beginDividerDrag(MixerSection section) {
    draggingDivider_ = (int)section;
    dragStartHeight_ = getRequestedHeight(section);
    notifyAppearance();
}

// Absolute from the drag start, never an accumulated delta: when the drag grows the bottom dock the
// whole panel moves up under the pointer, so only the pointer's own travel is a stable reference.
// Dragging a hidden section's divider shows it again at the dragged height.
void MixerSectionLayout::dragDividerBy(MixerSection section, int deltaPx) {
    if (draggingDivider_ != (int)section)
        return;
    if (isHidden(section)) {
        if (deltaPx <= 0)
            return;
        hidden_[indexOf(section)] = false;
        dragStartHeight_ = kCollapsedHeight; // the strip is where the section grows from
        requested_[indexOf(section)] = snapHeight(section, dragStartHeight_ + deltaPx);
        notifyGeometry();
        return;
    }
    setRequestedHeight(section, dragStartHeight_ + deltaPx);
}

void MixerSectionLayout::endDividerDrag() {
    if (draggingDivider_ < 0)
        return;
    draggingDivider_ = -1;
    notifyAppearance();
    notifyCommitted();
}

void MixerSectionLayout::loadFrom(const juce::PropertySet& settings) {
    requested_[indexOf(MixerSection::Inserts)] = snapHeight(
        MixerSection::Inserts, settings.getIntValue(kInsertsHeightKey, defaultHeightOf(MixerSection::Inserts)));
    requested_[indexOf(MixerSection::Sends)] =
        snapHeight(MixerSection::Sends, settings.getIntValue(kSendsHeightKey, defaultHeightOf(MixerSection::Sends)));
    hidden_[indexOf(MixerSection::Inserts)] = settings.getBoolValue(kInsertsHiddenKey, false);
    hidden_[indexOf(MixerSection::Sends)] = settings.getBoolValue(kSendsHiddenKey, false);
    hidden_[indexOf(MixerSection::Eq)] = settings.getBoolValue(kEqHiddenKey, false);
    notifyGeometry();
}

void MixerSectionLayout::saveTo(juce::PropertySet& settings) const {
    settings.setValue(kInsertsHeightKey, getRequestedHeight(MixerSection::Inserts));
    settings.setValue(kSendsHeightKey, getRequestedHeight(MixerSection::Sends));
    settings.setValue(kInsertsHiddenKey, isHidden(MixerSection::Inserts));
    settings.setValue(kSendsHiddenKey, isHidden(MixerSection::Sends));
    settings.setValue(kEqHiddenKey, isHidden(MixerSection::Eq));
}

void MixerSectionLayout::notifyGeometry() {
    if (onGeometryChanged)
        onGeometryChanged();
}

void MixerSectionLayout::notifyAppearance() {
    if (onAppearanceChanged)
        onAppearanceChanged();
}

void MixerSectionLayout::notifyCommitted() {
    if (onCommitted)
        onCommitted();
}

} // namespace synth::ui
