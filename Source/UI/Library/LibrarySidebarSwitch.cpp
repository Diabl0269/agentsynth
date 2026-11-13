// LibrarySidebarSwitch.cpp -- the Modules / Sounds switch: painting, the highlight's glide, mouse, keys and the
// screen-reader value ("Sounds, 2 of 2").
#include "UI/Library/LibrarySidebarSwitch.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

class SwitchValue : public juce::AccessibilityTextValueInterface {
public:
    explicit SwitchValue(const LibrarySidebarSwitch& owner)
        : owner_(owner) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return owner_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const LibrarySidebarSwitch& owner_;
};

} // namespace

LibrarySidebarSwitch::LibrarySidebarSwitch() {
    setTitle("Sidebar view");
    setTooltip(modulesTip_);
    setWantsKeyboardFocus(true);
}

LibrarySidebarSwitch::~LibrarySidebarSwitch() {
    if (vblank_.has_value())
        anim_.stop(*vblank_);
}

void LibrarySidebarSwitch::setSegmentTooltips(const juce::String& modules, const juce::String& sounds) {
    modulesTip_ = modules;
    soundsTip_ = sounds;
    setTooltip(view_ == SidebarView::Modules ? modulesTip_ : soundsTip_);
}

juce::Rectangle<int> LibrarySidebarSwitch::getSegmentBounds(SidebarView view) const {
    const auto track = getLocalBounds().reduced(8, 5);
    const int half = track.getWidth() / 2;
    return view == SidebarView::Modules ? track.withWidth(half) : track.withTrimmedLeft(half);
}

std::optional<SidebarView> LibrarySidebarSwitch::segmentAt(juce::Point<int> p) const {
    if (getSegmentBounds(SidebarView::Modules).contains(p))
        return SidebarView::Modules;
    if (getSegmentBounds(SidebarView::Sounds).contains(p))
        return SidebarView::Sounds;
    return std::nullopt;
}

void LibrarySidebarSwitch::setView(SidebarView view, bool animate) {
    view_ = view;
    setTooltip(view == SidebarView::Modules ? modulesTip_ : soundsTip_);
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    const float target = view == SidebarView::Sounds ? 1.0f : 0.0f;
    if (!animate || !isShowing() || animationsOff()) {
        if (vblank_.has_value())
            anim_.stop(*vblank_);
        pill_ = target;
        repaint();
        return;
    }
    if (!vblank_.has_value())
        vblank_.emplace(this);
    const float from = pill_;
    anim_.start(
        *vblank_, motionMs(160.0, 80.0), easeOutCubic,
        [this, from, target](float t) {
            pill_ = from + (target - from) * t;
            repaint();
        },
        [this, target] {
            pill_ = target;
            repaint();
        });
}

juce::String LibrarySidebarSwitch::getAccessibilityValueText() const {
    return juce::String(view_ == SidebarView::Modules ? "Modules, 1 of 2" : "Sounds, 2 of 2");
}

void LibrarySidebarSwitch::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& c = theme.colors;
    const auto track = getLocalBounds().reduced(8, 5).toFloat();
    const float radius = theme.metrics.pillRadius;
    g.setColour(c.surface);
    g.fillRoundedRectangle(track, radius);
    g.setColour(c.border);
    g.drawRoundedRectangle(track.reduced(0.5f), radius, 1.0f);

    const auto modules = getSegmentBounds(SidebarView::Modules).toFloat();
    const auto sounds = getSegmentBounds(SidebarView::Sounds).toFloat();
    const auto pill = modules.withX(modules.getX() + (sounds.getX() - modules.getX()) * pill_).reduced(2.0f);
    g.setColour(c.accent.withAlpha(0.22f));
    g.fillRoundedRectangle(pill, radius - 2.0f);

    const auto label = [&](SidebarView v, const char* text, juce::Rectangle<float> area) {
        const bool lit = v == view_;
        g.setColour(lit ? c.accent : (hovered_ == v ? c.textPrimary : c.textMuted));
        g.setFont(juce::Font(juce::FontOptions(13.0f, lit ? juce::Font::bold : juce::Font::plain)));
        g.drawText(text, area, juce::Justification::centred);
    };
    label(SidebarView::Modules, "Modules", modules);
    label(SidebarView::Sounds, "Sounds", sounds);
    paintFocusRing(g, track, *this, radius);
}

void LibrarySidebarSwitch::mouseMove(const juce::MouseEvent& e) {
    const auto seg = segmentAt(e.getPosition());
    if (seg == hovered_)
        return;
    hovered_ = seg;
    if (seg.has_value())
        setTooltip(*seg == SidebarView::Modules ? modulesTip_ : soundsTip_);
    setMouseCursor(seg.has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void LibrarySidebarSwitch::mouseExit(const juce::MouseEvent&) {
    if (hovered_.has_value()) {
        hovered_.reset();
        setTooltip(view_ == SidebarView::Modules ? modulesTip_ : soundsTip_);
        repaint();
    }
}

void LibrarySidebarSwitch::choose(SidebarView view) {
    if (view == view_)
        return;
    setView(view, true);
    if (onViewChosen)
        onViewChosen(view);
}

void LibrarySidebarSwitch::mouseDown(const juce::MouseEvent& e) {
    if (const auto seg = segmentAt(e.getPosition()))
        choose(*seg);
}

bool LibrarySidebarSwitch::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        choose(SidebarView::Modules);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::rightKey)) {
        choose(SidebarView::Sounds);
        return true;
    }
    return false;
}

void LibrarySidebarSwitch::focusGained(FocusChangeType) { repaint(); }
void LibrarySidebarSwitch::focusLost(FocusChangeType) { repaint(); }

std::unique_ptr<juce::AccessibilityHandler> LibrarySidebarSwitch::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::list, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<SwitchValue>(*this)});
}

} // namespace synth::ui
