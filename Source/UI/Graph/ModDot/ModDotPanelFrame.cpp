// The frame's placement, painting and the outside-click / app-deactivated closes (see the header).

#include "ModDotPanelFrame.h"

#include "ModDotPalette.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

// Everything the window draws: the shadow, the fill and outline, and the panel on top. It fills the frame and takes
// the same clicks (the panel and its arrow only), so scaling it scales the whole picture.
class ModDotPanelFrame::Body final : public juce::Component {
public:
    explicit Body(const juce::Path& outline)
        : outline_(outline) {
        setOpaque(false);
        setWantsKeyboardFocus(false);
    }

    bool hitTest(int x, int y) override { return outline_.contains((float)x, (float)y); }

    void paint(juce::Graphics& g) override {
        const auto p = modDotPaletteFor(*this);
        juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 8, {0, 2}).drawForPath(g, outline_);
        g.setColour(p.panel);
        g.fillPath(outline_);
        g.setColour(p.border);
        g.strokePath(outline_, juce::PathStrokeType(1.0f));
    }

private:
    const juce::Path& outline_;
};

ModDotPanelFrame::ModDotPanelFrame(std::unique_ptr<juce::Component> content, juce::Component& anchor,
                                   juce::Rectangle<int> dot, juce::Rectangle<int> area)
    : content_(std::move(content))
    , anchor_(&anchor)
    , area_(area)
    , dot_(dot) {
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setTitle(content_->getTitle());
    body_ = std::make_unique<Body>(outline_);
    addAndMakeVisible(*body_);
    body_->addAndMakeVisible(*content_);
    content_->addComponentListener(this);
    reposition();
}

ModDotPanelFrame::~ModDotPanelFrame() {
    tooltipWindow_.reset();
    content_->removeComponentListener(this);
    if (listening_)
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

int ModDotPanelFrame::maxContentHeight() const { return modDotPanel::maxPanelHeight(area_); }

// The panel is placed from the dot and its current size (all in the space the frame's own bounds are in: the screen
// for a window), then the frame (panel plus its transparent margin) is moved there, so the arrow tip lands exactly on
// the dot's centre line whatever the panel's height.
void ModDotPanelFrame::reposition() {
    const auto outer = modDotPanel::place(dot_, {content_->getWidth(), content_->getHeight()}, area_);
    const auto frame = outer.panel.expanded(kShadow);
    setBounds(frame);
    const auto origin = frame.getPosition();
    local_ = outer;
    local_.panel = outer.panel.translated(-origin.x, -origin.y);
    local_.tipX -= origin.x;
    local_.tipY -= origin.y;
    content_->setTopLeftPosition(local_.panel.getPosition());
    outline_ = modDotPanel::outline(local_);
    body_->repaint();
}

juce::Component& ModDotPanelFrame::body() noexcept { return *body_; }

void ModDotPanelFrame::resized() {
    if (body_ != nullptr)
        body_->setBounds(getLocalBounds());
}

void ModDotPanelFrame::componentMovedOrResized(juce::Component& component, bool, bool wasResized) {
    if (&component == content_.get() && wasResized)
        reposition();
}

void ModDotPanelFrame::installTooltipWindow(juce::ApplicationProperties* appProperties) {
    tooltipWindow_ = std::make_unique<AppTooltipWindow>(this, appProperties);
}

void ModDotPanelFrame::showOnDesktop() {
    addToDesktop(juce::ComponentPeer::windowIsTemporary);
    reposition();
    PopupMotion::attach(*this, motionStyle());
    setVisible(true);
    toFront(true);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    listening_ = true;
    startTimer(200);
}

// Grows out of the dot: the body scales from 40% about the dot's centre with a 3% overshoot while it fades in, and
// shrinks back toward it on the way out. The window itself does not slide: the scale already moves the panel.
popup_motion::Style ModDotPanelFrame::motionStyle() const {
    popup_motion::Style style;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.overshoot = true;
    style.inMs = kInMs;
    style.outMs = kOutMs;
    style.alphaInFraction = kFadeInFraction;
    style.inStartScale = kInStartScale;
    style.outEndScale = kOutEndScale;
    style.anchor = [dot = dot_] { return dot.getCentre(); };
    style.body = [this]() -> juce::Component* { return body_.get(); };
    // The dot's centre in the body's parent space (the frame's own): the dot is in the space the frame's bounds are in.
    style.bodyPivot = [this] { return (dot_.getCentre() - getPosition()).toFloat(); };
    return style;
}

// The dot was clicked again while the panel fades out: end the fade at once (the new panel takes over the spot).
void ModDotPanelFrame::finishClosingNow() {
    if (!closing_)
        return;
    setVisible(false); // the pending close then runs onClosed on the next turn
}

void ModDotPanelFrame::close() {
    if (closing_)
        return;
    closing_ = true;
    stopTimer();
    juce::Component::SafePointer<ModDotPanelFrame> self(this);
    PopupMotion::dismiss(*this, [self] {
        if (self == nullptr)
            return;
        self->setVisible(false);
        if (self->onClosed)
            self->onClosed();
    });
}

// A press anywhere that is not this window or the dot (or the jack region) closes the panel; the owner's own click
// toggles it.
void ModDotPanelFrame::mouseDown(const juce::MouseEvent& e) {
    if (closing_ || e.eventComponent == nullptr)
        return;
    if (e.eventComponent->getTopLevelComponent() == getTopLevelComponent())
        return;
    if (keepOpenOnOutsideClick && keepOpenOnOutsideClick())
        return;
    if (anchorIsRegion_) {
        if (dot_.contains(e.getScreenPosition()))
            return;
    } else if (auto* anchor = anchor_.getComponent();
               anchor != nullptr && (e.eventComponent == anchor || anchor->isParentOf(e.eventComponent))) {
        return;
    }
    close();
}

// Like a call-out, the panel does not outlive the app being in front.
void ModDotPanelFrame::timerCallback() {
    if (!juce::Process::isForegroundProcess())
        close();
}

bool ModDotPanelFrame::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    close();
    return true;
}

bool ModDotPanelFrame::hitTest(int x, int y) { return outline_.contains((float)x, (float)y); }

} // namespace synth::ui
