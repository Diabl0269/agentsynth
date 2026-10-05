// The frame's placement, painting and the outside-click / app-deactivated closes (see the header).

#include "ModDotPanelFrame.h"

#include "ModDotPalette.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

ModDotPanelFrame::ModDotPanelFrame(std::unique_ptr<juce::Component> content, juce::Component& anchor,
                                   juce::Rectangle<int> dot, juce::Rectangle<int> area)
    : content_(std::move(content))
    , anchor_(&anchor)
    , area_(area)
    , dot_(dot) {
    setOpaque(false);
    setWantsKeyboardFocus(false);
    setTitle(content_->getTitle());
    addAndMakeVisible(*content_);
    content_->addComponentListener(this);
    reposition();
}

ModDotPanelFrame::~ModDotPanelFrame() {
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
    repaint();
}

void ModDotPanelFrame::componentMovedOrResized(juce::Component& component, bool, bool wasResized) {
    if (&component == content_.get() && wasResized)
        reposition();
}

void ModDotPanelFrame::showOnDesktop() {
    addToDesktop(juce::ComponentPeer::windowIsTemporary);
    reposition();
    PopupMotion::attach(*this);
    setVisible(true);
    toFront(true);
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    listening_ = true;
    startTimer(200);
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

// A press anywhere that is not this window or the dot closes the panel (the dot's own click toggles it).
void ModDotPanelFrame::mouseDown(const juce::MouseEvent& e) {
    if (closing_ || e.eventComponent == nullptr)
        return;
    if (e.eventComponent->getTopLevelComponent() == getTopLevelComponent())
        return;
    if (keepOpenOnOutsideClick && keepOpenOnOutsideClick())
        return;
    if (auto* anchor = anchor_.getComponent();
        anchor != nullptr && (e.eventComponent == anchor || anchor->isParentOf(e.eventComponent)))
        return;
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

void ModDotPanelFrame::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 8, {0, 2}).drawForPath(g, outline_);
    g.setColour(p.panel);
    g.fillPath(outline_);
    g.setColour(p.border);
    g.strokePath(outline_, juce::PathStrokeType(1.0f));
}

} // namespace synth::ui
