#include "PortConnectionsPanel.h"

#include "AudioEngine/AudioEngine.h"
#include "PortPanelController.h"
#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/PopupMotion.h"
#include <algorithm>

namespace synth::ui {

// The rows live in a holder inside a viewport so a jack with a long fan-out scrolls instead of outgrowing the screen;
// the outline of a row an undo brought back is drawn over them.
class PortConnectionsPanel::RowsHolder final : public juce::Component {
public:
    std::vector<std::pair<juce::Rectangle<int>, float>> outlines;

    void paintOverChildren(juce::Graphics& g) override {
        const auto p = modDotPaletteFor(*this);
        for (const auto& [bounds, alpha] : outlines) {
            g.setColour(p.accent.withAlpha(alpha));
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), p.radius, 1.0f);
        }
    }
};

PortConnectionsPanel::PortConnectionsPanel(GraphEditor& editor, PortPanelController& controller, PortRef port)
    : editor_(editor)
    , controller_(&controller)
    , port_(port)
    , holder_(std::make_unique<RowsHolder>())
    , disconnectAll_("Disconnect all", "Disconnect all connections")
    , updater_(this) {
    ownTitle_ = portTitle(editor_, port_);
    setComponentID("portConnectionsPanel");
    setWantsKeyboardFocus(true); // a fallback target: Esc still reaches the panel when it has no row to hold the keys
    setTitle(ownTitle_ + " connections");
    viewport_.setViewedComponent(holder_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(kScrollBar);
    viewport_.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport_);
    disconnectAll_.setTooltip("Remove every connection on this jack");
    disconnectAll_.onClick = [this] { disconnectEverything(); };
    addChildComponent(disconnectAll_);
    for (const auto& connection : listPortConnections(editor_, port_)) {
        Entry entry;
        entry.row = std::make_unique<PortConnectionRow>(connection);
        entry.current = entry.to = (float)PortConnectionRow::kHeight;
        wireRow(*entry.row);
        holder_->addAndMakeVisible(*entry.row);
        entries_.push_back(std::move(entry));
    }
    footer_.current = footer_.to = entries_.size() >= 2 ? (float)kFooterHeight : 0.0f;
    layoutRows();
}

PortConnectionsPanel::~PortConnectionsPanel() {
    anim_.stop(updater_);
    stopTimer();
    if (controller_ != nullptr)
        controller_->panelClosed(this);
}

void PortConnectionsPanel::wireRow(PortConnectionRow& row) {
    row.onRemove = [this](PortConnectionRow& r) { removeConnection(r); };
    row.onNavigate = [this](PortConnectionRow& r, int step) { navigate(&r, step); };
    row.onHighlight = [this](PortConnectionRow& r, bool on) {
        if (controller_ == nullptr)
            return;
        if (on)
            controller_->setHighlightedCable(r.cableId());
        else
            controller_->clearHighlightIf(r.cableId());
        if (on)
            scrollRowIntoView(r);
    };
}

// One undo step, with the cable retracting on the canvas (GraphEditor::disconnectCable).
void PortConnectionsPanel::removeConnection(PortConnectionRow& row) {
    editor_.disconnectCable(row.connection().cable);
    sync();
    focusEntry();
}

void PortConnectionsPanel::disconnectEverything() {
    if (controller_ != nullptr)
        controller_->disconnectAll(port_);
    sync();
    focusEntry();
}

juce::String PortConnectionsPanel::titleText() const {
    return ownTitle_ + " (" + connectionCountText(rowCount()) + ")";
}

int PortConnectionsPanel::rowCount() const {
    int n = 0;
    for (const auto& e : entries_)
        n += e.leaving ? 0 : 1;
    return n;
}

PortConnectionRow* PortConnectionsPanel::rowAt(int index) const {
    int n = 0;
    for (const auto& e : entries_)
        if (!e.leaving && n++ == index)
            return e.row.get();
    return nullptr;
}

PortConnectionRow* PortConnectionsPanel::rowFor(const GraphEditor::CableId& id) const {
    for (const auto& e : entries_)
        if (!e.leaving && e.row->cableId() == id)
            return e.row.get();
    return nullptr;
}

PortConnectionRow* PortConnectionsPanel::animatingRowFor(const GraphEditor::CableId& id) const {
    for (const auto& e : entries_)
        if (e.row->cableId() == id)
            return e.row.get();
    return nullptr;
}

int PortConnectionsPanel::slotHeightFor(const GraphEditor::CableId& id) const {
    for (const auto& e : entries_)
        if (e.row->cableId() == id)
            return juce::roundToInt(e.current);
    return 0;
}

float PortConnectionsPanel::outlineAlphaFor(const GraphEditor::CableId& id) const {
    for (const auto& e : entries_)
        if (e.row->cableId() == id)
            return e.outline;
    return 0.0f;
}

// True when `list` is exactly the rows that are not on their way out.
bool PortConnectionsPanel::followsSameConnections(const std::vector<PortConnection>& list) const {
    const auto isShown = [this](const PortConnection& c) {
        return std::any_of(entries_.begin(), entries_.end(),
                           [&c](const Entry& e) { return !e.leaving && e.row->cableId() == c.cable.id; });
    };
    const auto shown = std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.leaving; });
    return static_cast<size_t>(shown) == list.size() && std::all_of(list.begin(), list.end(), isShown);
}

void PortConnectionsPanel::sync() {
    if (editor_.getAudioEngine().getGraph().getNodeForId(port_.node) == nullptr) {
        dismiss();
        return;
    }
    const auto list = listPortConnections(editor_, port_);
    // The same cables as the motion in flight was started for: only names and colours follow, the motion plays on.
    if (motionActive_ && followsSameConnections(list)) {
        for (auto& e : entries_)
            if (!e.leaving)
                e.row->update(*std::find_if(list.begin(), list.end(),
                                            [&e](const PortConnection& c) { return c.cable.id == e.row->cableId(); }));
        return;
    }
    landMotion(); // a different change lands the one playing first
    for (auto& e : entries_) {
        const auto it = std::find_if(list.begin(), list.end(),
                                     [&e](const PortConnection& c) { return c.cable.id == e.row->cableId(); });
        e.leaving = it == list.end();
        if (!e.leaving)
            e.row->update(*it);
        else if (controller_ != nullptr)
            controller_->clearHighlightIf(e.row->cableId());
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& connection = list[i];
        const bool known = std::any_of(entries_.begin(), entries_.end(), [&connection](const Entry& e) {
            return e.row->cableId() == connection.cable.id;
        });
        if (known)
            continue;
        Entry entry;
        entry.row = std::make_unique<PortConnectionRow>(connection);
        entry.restored = true; // a cable that appeared (an undo, a drag from the jack) grows in with the outline
        wireRow(*entry.row);
        holder_->addAndMakeVisible(*entry.row);
        // In the canvas' own order: after the row of the closest cable before it that is shown.
        auto at = entries_.begin();
        for (size_t before = i; before-- > 0;) {
            const auto it = std::find_if(entries_.begin(), entries_.end(), [&list, before](const Entry& e) {
                return e.row->cableId() == list[before].cable.id;
            });
            if (it != entries_.end()) {
                at = it + 1;
                break;
            }
        }
        entries_.insert(at, std::move(entry));
    }
    footer_.to = list.size() >= 2 ? (float)kFooterHeight : 0.0f;
    animateLayout();
    ensureFocusInside(); // a removed row must not leave the keyboard with nothing
}

void PortConnectionsPanel::dismiss() {
    stopTimer(); // a leaving panel must not go back for the keyboard
    if (controller_ != nullptr)
        controller_->clearHighlight();
    if (onDismiss)
        onDismiss();
}

void PortConnectionsPanel::setMaxHeight(int height) {
    maxHeight_ = height;
    layoutRows();
}

// ---- layout ----

void PortConnectionsPanel::layoutRows() {
    int natural = 0;
    for (const auto& e : entries_)
        natural += juce::roundToInt(e.current);
    const int footerHeight = juce::roundToInt(footer_.current);
    const int room = maxHeight_ > 0 ? juce::jmax(0, maxHeight_ - kTitleHeight - footerHeight - kBottomPad) : natural;
    const int viewHeight = juce::jmin(natural, room);
    const int rowWidth = natural > viewHeight ? kWidth - kScrollBar : kWidth;
    int y = 0;
    holder_->outlines.clear();
    for (auto& e : entries_) {
        y += exit_enter_slots::place(*e.row, e, y, rowWidth);
        if (e.outline > 0.0f)
            holder_->outlines.emplace_back(e.row->getBounds(), e.outline);
    }
    rowsTotal_ = y;
    holder_->setSize(rowWidth, rowsTotal_);
    holder_->repaint();
    desiredHeight_ = kTitleHeight + viewHeight + footerHeight + kBottomPad;
    if (getHeight() != desiredHeight_ || getWidth() != kWidth)
        setSize(kWidth, desiredHeight_);
    else
        arrange();
    heightChanged();
}

void PortConnectionsPanel::resized() { arrange(); }

void PortConnectionsPanel::arrange() {
    const int footerHeight = juce::roundToInt(footer_.current);
    const int viewHeight = juce::jmax(0, getHeight() - kTitleHeight - footerHeight - kBottomPad);
    viewport_.setBounds(0, kTitleHeight, getWidth(), viewHeight);
    const int footerY = kTitleHeight + viewHeight;
    disconnectAll_.setVisible(footerHeight > 0 && footer_.to > 0.0f);
    disconnectAll_.setAlpha(footer_.to > 0.0f ? juce::jlimit(0.0f, 1.0f, footer_.current / (float)kFooterHeight)
                                              : 1.0f);
    const int w = disconnectAll_.preferredWidth();
    disconnectAll_.setBounds((getWidth() - w) / 2, footerY + 4, w, kFooterHeight - 8);
}

void PortConnectionsPanel::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.text);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText(titleText(), juce::Rectangle<int>(0, 0, getWidth(), kTitleHeight).reduced(12, 0),
               juce::Justification::centredLeft, true);
    if (footer_.current > 1.0f) {
        g.setColour(p.border.withAlpha(juce::jlimit(0.0f, 1.0f, footer_.current / (float)kFooterHeight)));
        g.fillRect(
            juce::Rectangle<int>(8, getHeight() - kBottomPad - juce::roundToInt(footer_.current), getWidth() - 16, 1));
    }
}

// ---- keyboard ----

void PortConnectionsPanel::focusEntry() {
    for (auto& e : entries_)
        if (!e.leaving) {
            e.row->removeButton().grabKeyboardFocus();
            return;
        }
    grabKeyboardFocus();
}

// Up/Down walk the rows' remove buttons and end on "Disconnect all" when it is showing.
void PortConnectionsPanel::navigate(juce::Component* from, int step) {
    struct Stop {
        juce::Component* control;
        juce::Component* container;
    };
    std::vector<Stop> stops;
    for (auto& e : entries_)
        if (!e.leaving)
            stops.push_back({&e.row->removeButton(), e.row.get()});
    if (footer_.to > 0.0f)
        stops.push_back({&disconnectAll_, &disconnectAll_});
    if (stops.empty())
        return;
    int index = (int)stops.size() - 1;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (from != nullptr && (stops[(size_t)i].container == from || stops[(size_t)i].container->isParentOf(from)))
            index = i;
    index = juce::jlimit(0, (int)stops.size() - 1, index + step);
    stops[(size_t)index].control->grabKeyboardFocus();
}

void PortConnectionsPanel::scrollRowIntoView(const PortConnectionRow& row) {
    const auto bounds = row.getBounds();
    const int top = viewport_.getViewPositionY();
    const int height = viewport_.getViewHeight();
    if (bounds.getY() < top)
        viewport_.setViewPosition(0, bounds.getY());
    else if (bounds.getBottom() > top + height)
        viewport_.setViewPosition(0, bounds.getBottom() - height);
}

bool PortConnectionsPanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        dismiss();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        navigate(juce::Component::getCurrentlyFocusedComponent(), key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

bool PortConnectionsPanel::focusIsInside() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (focused == this || isParentOf(focused));
}

void PortConnectionsPanel::ensureFocusInside() {
    auto* peer = getPeer();
    if (peer == nullptr || !isShowing() || focusIsInside())
        return;
    // A click on the canvas moves the keys to the app window while the panel fades out; pulling the panel's window to
    // the front now would re-show it mid-fade.
    if (auto* window = getTopLevelComponent(); window != nullptr && PopupMotion::isDismissing(*window))
        return;
    if (!peer->isFocused()) {
        if (auto* window = getTopLevelComponent())
            window->toFront(true); // the panel's window must be the key one before a control can take the keys
        return;
    }
    focusEntry();
}

void PortConnectionsPanel::timerCallback() {
    ensureFocusInside();
    if (focusIsInside() || ++focusTries_ > 60)
        stopTimer();
}

// The panel's window only becomes the key window a moment after it is shown, so focus is taken as soon as it can give
// it.
void PortConnectionsPanel::parentHierarchyChanged() {
    if (getParentComponent() != nullptr && !focusIsInside())
        startTimerHz(30);
}

void PortConnectionsPanel::visibilityChanged() {
    if (isShowing() && !focusIsInside() && !isTimerRunning()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

} // namespace synth::ui
