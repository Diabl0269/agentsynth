#include "TimelineTopBarKeys.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kRingOutset = 1;

bool isReachable(const juce::Component& c, const juce::Component& owner) {
    if (!c.isEnabled())
        return false;
    for (const auto* at = &c; at != nullptr && at != &owner; at = at->getParentComponent())
        if (!at->isVisible())
            return false;
    return owner.isParentOf(&c);
}

juce::Rectangle<int> boundsInOwner(const juce::Component& c, const juce::Component& owner) {
    return owner.getLocalArea(c.getParentComponent(), c.getBounds());
}
} // namespace

TimelineTopBarKeys::TimelineTopBarKeys(juce::Component& owner, StopsFn candidates)
    : owner_(owner)
    , candidates_(std::move(candidates)) {}

std::vector<juce::Component*> TimelineTopBarKeys::getStops() const {
    auto stops = candidates_();
    stops.erase(std::remove_if(stops.begin(), stops.end(),
                               [this](juce::Component* c) { return c == nullptr || !isReachable(*c, owner_); }),
                stops.end());
    std::stable_sort(stops.begin(), stops.end(), [this](juce::Component* a, juce::Component* b) {
        return boundsInOwner(*a, owner_).getX() < boundsInOwner(*b, owner_).getX();
    });
    return stops;
}

// A ringed control that has since gone (hidden, disabled) hands the ring to the first one that is left.
juce::Component* TimelineTopBarKeys::getRingedStop() const {
    const auto stops = getStops();
    if (stops.empty())
        return nullptr;
    return std::find(stops.begin(), stops.end(), ringed_.getComponent()) != stops.end() ? ringed_.getComponent()
                                                                                        : stops.front();
}

bool TimelineTopBarKeys::enter() {
    auto* stop = getRingedStop();
    if (stop == nullptr)
        return false;
    active_ = true;
    moveRingTo(stop);
    return true;
}

void TimelineTopBarKeys::leave() {
    if (!active_)
        return;
    active_ = false;
    owner_.repaint();
}

void TimelineTopBarKeys::moveRingTo(juce::Component* stop) {
    ringed_ = stop;
    owner_.repaint();
}

bool TimelineTopBarKeys::handleKey(const juce::KeyPress& key) {
    if (!active_ || key.getModifiers().isAnyModifierKeyDown())
        return false;
    const auto stops = getStops();
    auto* current = getRingedStop();
    if (stops.empty() || current == nullptr) {
        leave();
        return false;
    }
    const auto at = std::find(stops.begin(), stops.end(), current) - stops.begin();
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        moveRingTo(stops[(size_t)std::max<long>(0, at - 1)]);
    } else if (key.isKeyCode(juce::KeyPress::rightKey)) {
        moveRingTo(stops[(size_t)std::min<long>((long)stops.size() - 1, at + 1)]);
    } else if (key.isKeyCode(juce::KeyPress::homeKey)) {
        moveRingTo(stops.front());
    } else if (key.isKeyCode(juce::KeyPress::endKey)) {
        moveRingTo(stops.back());
    } else if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        activate(*current);
    } else if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::escapeKey)) {
        leave();
    } else if (!key.isKeyCode(juce::KeyPress::upKey)) {
        return false; // a tool digit, a letter: the panel's own shortcuts still work with the ring up
    }
    return true;
}

// A menu opens, a text readout starts editing, a button is pressed: what a click on it would do.
void TimelineTopBarKeys::activate(juce::Component& stop) {
    if (auto* menu = dynamic_cast<juce::ComboBox*>(&stop))
        menu->showPopup();
    else if (auto* label = dynamic_cast<juce::Label*>(&stop))
        label->showEditor();
    else if (auto* button = dynamic_cast<juce::Button*>(&stop))
        button->triggerClick();
}

void TimelineTopBarKeys::paintRing(juce::Graphics& g) const {
    if (!active_)
        return;
    if (const auto* stop = getRingedStop())
        paintFocusRingAlways(g, boundsInOwner(*stop, owner_).expanded(kRingOutset).toFloat(), owner_, 4.0f);
}

// Every control of the top bar. Add Track is not here: it is the first stop of the track column below, reached with
// Down.
std::vector<juce::Component*> TimelinePanelComponent::topBarStops() {
    std::vector<juce::Component*> stops{&sidePaneButton_, &clipSizingButton_,     &snapToggleButton_,
                                        &snapCombo_,      &followPlayheadButton_, dockBack_};
    for (auto tool : kAllEditTools)
        stops.push_back(getToolButton(tool));
    stops.insert(stops.end(),
                 {&transportBar_.getReturnToStartButton(), &transportBar_.getPlayStopButton(),
                  &transportBar_.getRecordButton(), &transportBar_.getLoopButton(), &transportBar_.getMetronomeButton(),
                  &transportBar_.getCountInCombo(), &transportBar_.getBpmLabel(), &transportBar_.getTimeSigLabel(),
                  &transportBar_.getSongKeyField()});
    return stops;
}

} // namespace synth::ui
