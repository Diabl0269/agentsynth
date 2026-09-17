// Concern: FRO147 -- MeterColourStopsEditor's layout math, painting, mouse/keyboard interaction
// and accessibility. See the header for the interaction contract.
#include "MeterColourStopsEditor.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

// Read-only mirror of MixerMeter.cpp's own MeterValueInterface -- see that file's comment. Here
// it reports the CURRENTLY SELECTED stop's dB and colour rather than a live meter reading; an
// editor with nothing selected reads back "No stop selected" instead of a value.
class StopValueInterface : public juce::AccessibilityValueInterface {
public:
    explicit StopValueInterface(const MeterColourStopsEditor& editor)
        : editor_(editor) {}

    bool isReadOnly() const override { return false; }

    double getCurrentValue() const override {
        const auto& stops = editor_.getStops().getStops();
        const int index = editor_.getSelectedIndexForTest();
        return (index >= 0 && index < (int)stops.size()) ? (double)stops[(size_t)index].dbFrom : 0.0;
    }

    juce::String getCurrentValueAsString() const override {
        const auto& stops = editor_.getStops().getStops();
        const int index = editor_.getSelectedIndexForTest();
        if (index < 0 || index >= (int)stops.size())
            return "No stop selected";
        const auto& stop = stops[(size_t)index];
        juce::String dbText = index == 0 ? juce::String("floor (from -inf)") : juce::String(stop.dbFrom, 1) + " dB";
        return dbText + ", colour " + stop.colour.toDisplayString(false);
    }

    void setValue(double) override {}
    void setValueAsString(const juce::String&) override {}
    AccessibleValueRange getRange() const override { return {{(double)kMeterMinDb, (double)kMeterMaxDb}, 0.5}; }

private:
    const MeterColourStopsEditor& editor_;
};

} // namespace

MeterColourStopsEditor::MeterColourStopsEditor() {
    setWantsKeyboardFocus(true);
    setStops(MeterColourStops::fromTheme(synth::theme::Colors{}));
}

void MeterColourStopsEditor::setStops(const MeterColourStops& stops) {
    stops_ = stops;
    selected_ = -1; // indices may no longer refer to the same stops after a wholesale replace
    dragIndex_ = -1;
    repaint();
}

int MeterColourStopsEditor::barTop() const noexcept { return kHandleHeight / 2; }
int MeterColourStopsEditor::barBottom() const noexcept {
    return juce::jmax(barTop() + 1, getHeight() - kHandleHeight / 2);
}

int MeterColourStopsEditor::dbToY(float db) const noexcept {
    const float fraction = meterDbToFraction(db);
    return barBottom() - juce::roundToInt(fraction * (float)(barBottom() - barTop()));
}

float MeterColourStopsEditor::yToDb(int y) const noexcept {
    const float span = (float)juce::jmax(1, barBottom() - barTop());
    const float fraction = juce::jlimit(0.0f, 1.0f, (float)(barBottom() - y) / span);
    return meterFractionToDb(fraction);
}

float MeterColourStopsEditor::snapDb(float db) noexcept {
    const float snapped = std::round(db / kDbSnap) * kDbSnap;
    return juce::jlimit(kMeterMinDb, kMeterMaxDb, snapped);
}

juce::Rectangle<int> MeterColourStopsEditor::handleBounds(int index) const {
    const auto& stops = stops_.getStops();
    if (index < 0 || index >= (int)stops.size())
        return {};
    const int y = dbToY(stops[(size_t)index].dbFrom);
    const int x = kScaleWidth + kPreviewBarWidth + kPreviewBarGap * 2;
    const int w = juce::jmax(0, getWidth() - x);
    return juce::Rectangle<int>(x, y - kHandleHeight / 2, w, kHandleHeight);
}

juce::Rectangle<int> MeterColourStopsEditor::swatchBounds(int index) const {
    const auto hb = handleBounds(index);
    if (hb.isEmpty())
        return {};
    return juce::Rectangle<int>(hb.getX() + 2, hb.getCentreY() - kSwatchSize / 2, kSwatchSize, kSwatchSize);
}

MeterColourStopsEditor::Hit MeterColourStopsEditor::hitTestStop(juce::Point<int> localPos) const {
    const int n = (int)stops_.getStops().size();
    for (int i = 0; i < n; ++i) {
        const auto hb = handleBounds(i);
        if (hb.contains(localPos))
            return {i, swatchBounds(i).contains(localPos) ? HitZone::Swatch : HitZone::Draggable};
    }
    return {-1, HitZone::None};
}

void MeterColourStopsEditor::selectIndex(int index) {
    const int n = (int)stops_.getStops().size();
    selected_ = (index >= 0 && index < n) ? index : -1;
    repaint();
}

float MeterColourStopsEditor::clampDbForIndex(int index, float requestedDb) const {
    const auto& stops = stops_.getStops();
    if (index <= 0 || index >= (int)stops.size())
        return index >= 0 && index < (int)stops.size() ? stops[(size_t)index].dbFrom : requestedDb; // floor: unmoved

    float lo = juce::jmax(kMeterMinDb, stops[(size_t)(index - 1)].dbFrom + kDbSnap);
    float hi = kMeterMaxDb;
    if (index + 1 < (int)stops.size())
        hi = juce::jmin(hi, stops[(size_t)(index + 1)].dbFrom - kDbSnap);
    if (lo > hi) // neighbours already exactly kDbSnap apart -- no room to move into
        return stops[(size_t)index].dbFrom;
    return juce::jlimit(lo, hi, requestedDb);
}

void MeterColourStopsEditor::setStopDb(int index, float db, bool committed) {
    auto stops = stops_.getStops();
    if (index < 0 || index >= (int)stops.size())
        return;
    stops[(size_t)index].dbFrom = db;
    stops_.setStops(std::move(stops)); // order is preserved by clampDbForIndex, so this never re-sorts
    notify(committed);
    repaint();
}

void MeterColourStopsEditor::setStopColour(int index, juce::Colour colour, bool committed) {
    auto stops = stops_.getStops();
    if (index < 0 || index >= (int)stops.size())
        return;
    stops[(size_t)index].colour = colour;
    stops_.setStops(std::move(stops));
    notify(committed);
    repaint();
}

void MeterColourStopsEditor::addStopAt(float db) {
    if ((int)stops_.getStops().size() >= MeterColourStops::kMaxStops)
        return;

    const float snapped = snapDb(db);
    // Refuse a collision rather than let MeterColourStops::setStops() silently dedup it away --
    // an add that vanishes with no feedback would read as a bug, not a boundary.
    for (const auto& stop : stops_.getStops())
        if (juce::approximatelyEqual(stop.dbFrom, snapped))
            return;

    auto stops = stops_.getStops();
    const juce::Colour bandColour = stops_.colourForDb(snapped).brighter(0.25f);
    stops.push_back({snapped, bandColour});
    stops_.setStops(std::move(stops));

    // Re-find the new stop's index (setStops() just re-sorted) and select it.
    const auto& sorted = stops_.getStops();
    for (int i = 0; i < (int)sorted.size(); ++i)
        if (juce::approximatelyEqual(sorted[(size_t)i].dbFrom, snapped)) {
            selected_ = i;
            break;
        }

    notify(true);
    repaint();
}

void MeterColourStopsEditor::removeSelectedStop() {
    if (selected_ <= 0) // -1 = nothing selected, 0 = the floor -- neither removable
        return;
    auto stops = stops_.getStops();
    if (stops.size() <= 1 || selected_ >= (int)stops.size())
        return;

    stops.erase(stops.begin() + selected_);
    stops_.setStops(std::move(stops));
    selected_ = -1;
    notify(true);
    repaint();
}

void MeterColourStopsEditor::notify(bool committed) {
    if (onChanged)
        onChanged(stops_, committed);
}

void MeterColourStopsEditor::mouseDown(const juce::MouseEvent& e) {
    const auto hit = hitTestStop(e.getPosition());
    if (hit.index >= 0) {
        selectIndex(hit.index);
        if (hit.zone == HitZone::Swatch) {
            // Deferred: a press on the swatch stays undecided between "click" (open the picker)
            // and "drag" (move the handle) until mouseDrag/mouseUp settle it below -- the swatch is
            // the row's most natural grab point, so it must be able to start a drag too, not just
            // recolour. Still arm the drag itself now (same as a body hit) so mouseDrag has
            // something to move the instant the pointer crosses the threshold.
            pendingSwatchClickIndex_ = hit.index;
            swatchPressPos_ = e.getPosition();
            if (hit.index > 0) { // the floor's swatch still opens on click, but never drags
                dragIndex_ = hit.index;
                dragStartDb_ = stops_.getStops()[(size_t)hit.index].dbFrom;
            }
            return;
        }
        pendingSwatchClickIndex_ = -1;
        if (hit.index > 0) { // the floor never drags
            dragIndex_ = hit.index;
            dragStartDb_ = stops_.getStops()[(size_t)hit.index].dbFrom;
        }
        return;
    }

    // Empty area -- add a stop where the click landed.
    pendingSwatchClickIndex_ = -1;
    addStopAt(yToDb(e.y));
}

void MeterColourStopsEditor::mouseDrag(const juce::MouseEvent& e) {
    if (pendingSwatchClickIndex_ >= 0) {
        // Still within the "might just be a click" window -- do nothing (not even repaint) until
        // the pointer actually moves enough to commit to a drag.
        if (e.getPosition().getDistanceFrom(swatchPressPos_) < kSwatchDragThresholdPx)
            return;
        pendingSwatchClickIndex_ = -1; // committed to a drag now -- mouseUp must not open the picker
    }
    if (dragIndex_ < 0)
        return;
    const float requested = snapDb(yToDb(e.y));
    setStopDb(dragIndex_, clampDbForIndex(dragIndex_, requested), /*committed*/ false);
}

void MeterColourStopsEditor::mouseUp(const juce::MouseEvent&) {
    if (pendingSwatchClickIndex_ >= 0) {
        // The pointer never crossed the drag threshold -- a genuine click on the swatch.
        const int index = pendingSwatchClickIndex_;
        pendingSwatchClickIndex_ = -1;
        dragIndex_ = -1;
        if (index < (int)stops_.getStops().size() && onColourPickerRequested)
            onColourPickerRequested(index, localAreaToGlobal(swatchBounds(index)),
                                    stops_.getStops()[(size_t)index].colour);
        return;
    }

    // Only a REAL move fires a change -- a plain press-and-release on a handle body (no drag in
    // between) must select it and nothing more, matching a swatch/empty-area click's own "no
    // notify on selection alone" contract.
    if (dragIndex_ >= 0 && dragIndex_ < (int)stops_.getStops().size() &&
        !juce::approximatelyEqual(stops_.getStops()[(size_t)dragIndex_].dbFrom, dragStartDb_))
        notify(true); // final, committed write of wherever the drag landed
    dragIndex_ = -1;
}

bool MeterColourStopsEditor::keyPressed(const juce::KeyPress& key) {
    if (selected_ < 0)
        return false;

    // Compare the KEY CODE alone, not the whole KeyPress (which also matches modifiers) -- Up/
    // Down must be recognised whether or not Shift is held; the modifier is read separately below
    // to pick the nudge size, not to gate which branch runs.
    const int keyCode = key.getKeyCode();

    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey) {
        removeSelectedStop();
        return true;
    }

    if (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey) {
        if (selected_ == 0)
            return true; // floor: consume the key, no movement
        const float step = key.getModifiers().isShiftDown() ? kShiftNudgeDb : kNudgeDb;
        const float direction = keyCode == juce::KeyPress::upKey ? 1.0f : -1.0f;
        const float requested = stops_.getStops()[(size_t)selected_].dbFrom + step * direction;
        setStopDb(selected_, clampDbForIndex(selected_, snapDb(requested)), /*committed*/ true);
        return true;
    }

    return false;
}

std::unique_ptr<juce::AccessibilityHandler> MeterColourStopsEditor::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<StopValueInterface>(*this)});
}

void MeterColourStopsEditor::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const synth::theme::Colors fallback{};
    const auto& colors = laf != nullptr ? laf->getTheme().colors : fallback;

    const juce::Rectangle<int> scale(0, barTop(), kScaleWidth, barBottom() - barTop());
    for (const float db : kMeterTickDb) {
        const int y = dbToY(db);
        const bool isZeroDb = juce::approximatelyEqual(db, 0.0f);
        g.setColour(colors.border.withAlpha(isZeroDb ? 0.9f : 0.5f));
        g.drawLine((float)scale.getX(), (float)y, (float)scale.getRight(), (float)y, isZeroDb ? 1.4f : 0.6f);
        g.setColour(colors.textMuted);
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(juce::String((int)db), scale.getX(), y - 6, scale.getWidth() - 4, 12,
                   juce::Justification::centredRight, false);
    }

    const juce::Rectangle<int> previewBar(scale.getRight() + kPreviewBarGap, barTop(), kPreviewBarWidth,
                                          barBottom() - barTop());
    g.setColour(colors.bg1);
    g.fillRect(previewBar);
    stops_.forEachBand(kMeterMinDb, kMeterMaxDb, [&](float fromDb, float toDb, juce::Colour colour) {
        const int yBottom = dbToY(fromDb);
        const int yTop = dbToY(toDb);
        g.setColour(colour);
        g.fillRect(previewBar.getX(), yTop, previewBar.getWidth(), yBottom - yTop);
    });

    const auto& stops = stops_.getStops();
    for (int i = 0; i < (int)stops.size(); ++i) {
        const auto& stop = stops[(size_t)i];
        const auto hb = handleBounds(i);
        const bool isSelected = (i == selected_);

        g.setColour(colors.border.withAlpha(0.6f));
        g.drawLine((float)previewBar.getRight(), (float)dbToY(stop.dbFrom), (float)hb.getX(), (float)hb.getCentreY(),
                   1.0f);

        const auto swatch = swatchBounds(i);
        g.setColour(stop.colour);
        g.fillRoundedRectangle(swatch.toFloat(), 2.0f);
        g.setColour(isSelected ? colors.accent : colors.border);
        g.drawRoundedRectangle(swatch.toFloat(), 2.0f, isSelected ? 1.8f : 1.0f);

        const juce::String label = i == 0 ? juce::String("Floor") : (juce::String(stop.dbFrom, 1) + " dB");
        const auto textArea = hb.withTrimmedLeft(swatch.getWidth() + 6);
        g.setColour(colors.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(10.5f)));
        g.drawText(label, textArea, juce::Justification::centredLeft, true);

        if (isSelected) {
            g.setColour(colors.accent.withAlpha(0.5f));
            g.drawRoundedRectangle(hb.toFloat().reduced(1.0f), 3.0f, 1.2f);
        }
    }
}

} // namespace synth::ui
