#include "StepBlocks.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth {

using songsteps::StepStatus;
using synth::ui::StepIconState;

namespace {

juce::String statusWords(StepStatus status) {
    switch (status) {
    case StepStatus::waiting:
        return "waiting";
    case StepStatus::running:
        return "running";
    case StepStatus::fixing:
        return "being fixed";
    case StepStatus::done:
        return "done";
    case StepStatus::skipped:
        return "left out";
    }
    return {};
}

juce::String clockText(int ms) {
    const int s = ms / 1000;
    return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
}

} // namespace

// -- one block ----------------------------------------------------------------------------------------------------

StepBlock::StepBlock() {
    addAndMakeVisible(icon_);
    icon_.setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setAccessible(true);
}

juce::Colour StepBlock::kindColour() const {
    return synth::ui::stepKindColour(synth::theme::themeOf(*this).colors, step_.info.kind);
}

void StepBlock::update(const songsteps::StepState& state) {
    step_ = state;
    const juce::String words = statusWords(step_.status);
    setTitle(step_.info.label + ", " + words);
    setDescription(synth::ui::stepKindDescription(step_.info.kind));
    juce::String tip = step_.info.label + ": " + words;
    if (step_.ms >= 0)
        tip << " in " << clockText(step_.ms);
    tip << ". " << (step_.summary.isNotEmpty() ? step_.summary : synth::ui::stepKindDescription(step_.info.kind));
    setTooltip(tip);

    icon_.setKind(step_.info.kind);
    // The kind's art keeps its own colour in every state, so the kind stays readable; the state shows in the art's
    // motion, its strength and the badge.
    icon_.setState(step_.status == StepStatus::running   ? StepIconState::running
                   : step_.status == StepStatus::fixing  ? StepIconState::fixing
                   : step_.status == StepStatus::skipped ? StepIconState::skipped
                                                         : StepIconState::still);
    repaint();
}

void StepBlock::resized() {
    const int pad = 4;
    icon_.setBounds(getLocalBounds().reduced(pad));
}

void StepBlock::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto hue = kindColour();
    const bool waiting = step_.status == StepStatus::waiting;
    const bool skipped = step_.status == StepStatus::skipped;
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(hue.withAlpha(waiting || skipped ? 0.05f : 0.16f));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(hue.withAlpha(waiting || skipped ? 0.3f : 0.65f));
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    icon_.setColour(step_.status == StepStatus::fixing ? c.warning : hue.withAlpha(waiting || skipped ? 0.55f : 1.0f));
    if (step_.status == StepStatus::done) {
        // A small drawn check in the corner: the kind stays visible and the block still says it is finished.
        const float d = 10.0f;
        const juce::Rectangle<float> badge(r.getRight() - d + 2.0f, r.getBottom() - d + 2.0f, d, d);
        g.setColour(c.surface);
        g.fillEllipse(badge.expanded(1.0f));
        g.setColour(c.success);
        g.fillEllipse(badge);
        juce::Path tick;
        tick.startNewSubPath(badge.getX() + d * 0.26f, badge.getY() + d * 0.54f);
        tick.lineTo(badge.getX() + d * 0.44f, badge.getY() + d * 0.72f);
        tick.lineTo(badge.getX() + d * 0.76f, badge.getY() + d * 0.30f);
        g.setColour(c.iconInk);
        g.strokePath(tick, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, 6.0f);
}

StepBlockBox* StepBlock::box() const { return findParentComponentOfClass<StepBlockBox>(); }

void StepBlock::focusGained(FocusChangeType) {
    if (auto* b = box())
        b->scrollToShow(*this);
    repaint();
}

bool StepBlock::keyPressed(const juce::KeyPress& key) {
    auto* b = box();
    if (b == nullptr)
        return false;
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::leftKey) {
        b->moveFocus(*this, key == juce::KeyPress::rightKey ? 1 : -1, 0);
        return true;
    }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey) {
        b->moveFocus(*this, 0, key == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    return false;
}

// -- the box -------------------------------------------------------------------------------------------------------

class StepBlockBox::Content : public juce::Component {
public:
    explicit Content(StepBlockBox& ownerIn)
        : owner(ownerIn) {
        setInterceptsMouseClicks(false, true);
    }
    // The highlight sits under the blocks (children paint over it).
    void paint(juce::Graphics& g) override {
        if (owner.hlAlpha_ <= 0.001f)
            return;
        const auto& c = synth::theme::themeOf(*this).colors;
        g.setColour(c.accent.withAlpha(0.2f * owner.hlAlpha_));
        g.fillRoundedRectangle(owner.hlRect_, 8.0f);
        g.setColour(c.accent.withAlpha(owner.hlAlpha_));
        g.drawRoundedRectangle(owner.hlRect_.reduced(0.75f), 8.0f, 1.5f);
    }
    StepBlockBox& owner;
};

StepBlockBox::StepBlockBox()
    : content_(std::make_unique<Content>(*this)) {
    setComponentID("songStepBlocks");
    setTitle("Steps");
    setDescription("One block for each step of the song build; the running step is highlighted");
    viewport_.setViewedComponent(content_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(kScrollBar);
    viewport_.setWantsKeyboardFocus(false);
    viewport_.setFocusContainerType(juce::Component::FocusContainerType::none);
    addAndMakeVisible(viewport_);
}

StepBlockBox::~StepBlockBox() {
    hlAnim_.stop(vblank_);
    viewport_.setViewedComponent(nullptr, false);
}

// The public sizes take the box's width; the blocks lay out in the viewport inside its 1 px border.
int StepBlockBox::columnsFor(int width) const { return columnsIn(width - 2); }
int StepBlockBox::contentHeightFor(int width) const { return contentHeightIn(width - 2); }

int StepBlockBox::columnsIn(int width) const {
    auto fit = [](int w) { return juce::jmax(1, (w - 2 * kInset + kGap) / (kBlock + kGap)); };
    const int n = (int)blocks_.size();
    int cols = fit(width);
    const int rows = n == 0 ? 0 : (n + cols - 1) / cols;
    if (rows * (kBlock + kGap) - kGap + 2 * kInset > kMaxHeight)
        cols = fit(width - kScrollBar);
    return cols;
}

int StepBlockBox::contentHeightIn(int width) const {
    const int n = (int)blocks_.size();
    if (n == 0)
        return 2 * kInset;
    const int cols = columnsFor(width);
    const int rows = (n + cols - 1) / cols;
    return rows * (kBlock + kGap) - kGap + 2 * kInset;
}

int StepBlockBox::heightFor(int width) const { return juce::jmin(kMaxHeight, contentHeightFor(width)); }

void StepBlockBox::setSteps(const std::vector<songsteps::StepState>& steps) {
    while (blocks_.size() < steps.size()) {
        blocks_.push_back(std::make_unique<StepBlock>());
        blocks_.back()->setComponentID("songStep:" + juce::String(blocks_.size() - 1));
        content_->addAndMakeVisible(*blocks_.back());
    }
    int running = -1;
    for (size_t i = 0; i < steps.size(); ++i) {
        blocks_[i]->update(steps[i]);
        if (running < 0 && (steps[i].status == StepStatus::running || steps[i].status == StepStatus::fixing))
            running = (int)i;
    }
    layoutContent(getWidth());
    setHighlight(running);
}

void StepBlockBox::setBlocksFocusable(bool focusable) {
    for (auto& b : blocks_)
        b->setWantsKeyboardFocus(focusable);
}

juce::Rectangle<int> StepBlockBox::blockBoundsInContent(int i) const { return blocks_[(size_t)i]->getBounds(); }

juce::Rectangle<int> StepBlockBox::blockBoundsInBox(int i) const {
    return getLocalArea(content_.get(), blocks_[(size_t)i]->getBounds());
}

void StepBlockBox::layoutContent(int width) {
    if (width <= 0)
        return;
    const int cols = columnsIn(width);
    const bool bar = contentHeightIn(width) > kMaxHeight;
    const int contentW = width - (bar ? kScrollBar : 0);
    for (size_t i = 0; i < blocks_.size(); ++i) {
        const int col = (int)i % cols, row = (int)i / cols;
        blocks_[i]->setBounds(kInset + col * (kBlock + kGap), kInset + row * (kBlock + kGap), kBlock, kBlock);
    }
    content_->setSize(contentW, contentHeightIn(width));
    shownWidth_ = width;
}

void StepBlockBox::resized() {
    viewport_.setBounds(getLocalBounds().reduced(1));
    layoutContent(getWidth() - 2);
    if (highlight_ >= 0 && !hlAnim_.isRunning()) {
        hlRect_ = highlightTarget();
        hlTo_ = hlRect_;
        content_->repaint();
    }
}

juce::Rectangle<float> StepBlockBox::highlightTarget() const {
    if (highlight_ < 0 || highlight_ >= (int)blocks_.size())
        return hlRect_;
    return blocks_[(size_t)highlight_]->getBounds().toFloat().expanded(3.0f);
}

void StepBlockBox::setHighlight(int index) {
    const bool moved = index != highlight_;
    highlight_ = index;
    const auto target = highlightTarget();
    const float toAlpha = index >= 0 ? 1.0f : 0.0f;
    if (!moved && !hlAnim_.isRunning()) {
        hlRect_ = target;
        hlAlpha_ = toAlpha;
        content_->repaint();
        return;
    }
    const bool animate = synth::ui::FadeVisibility::canAnimateIn(this);
    if (!animate) {
        hlAnim_.stop(vblank_);
        hlRect_ = target;
        hlAlpha_ = toAlpha;
        content_->repaint();
        return;
    }
    hlFrom_ = hlRect_;
    hlTo_ = target;
    hlAlphaFrom_ = hlAlpha_;
    hlAlphaTo_ = toAlpha;
    if (hlAlpha_ <= 0.001f)
        hlFrom_ = target; // it fades in where the step is, not from wherever it last was
    hlAnim_.start(
        vblank_, synth::ui::motionMs(180.0, 80.0), synth::ui::easeOutCubic,
        [this](float t) {
            hlRect_ = {hlFrom_.getX() + (hlTo_.getX() - hlFrom_.getX()) * t,
                       hlFrom_.getY() + (hlTo_.getY() - hlFrom_.getY()) * t, hlTo_.getWidth(), hlTo_.getHeight()};
            hlAlpha_ = hlAlphaFrom_ + (hlAlphaTo_ - hlAlphaFrom_) * t;
            content_->repaint();
        },
        [this] {
            hlRect_ = hlTo_;
            hlAlpha_ = hlAlphaTo_;
            content_->repaint();
        });
}

void StepBlockBox::scrollToShow(const juce::Component& block) {
    const auto b = block.getBounds().expanded(0, 3);
    const int top = viewport_.getViewPositionY();
    const int h = viewport_.getMaximumVisibleHeight();
    if (b.getY() < top)
        viewport_.setViewPosition(0, b.getY());
    else if (b.getBottom() > top + h)
        viewport_.setViewPosition(0, b.getBottom() - h);
}

void StepBlockBox::moveFocus(const StepBlock& from, int dx, int dy) {
    int index = -1;
    for (int i = 0; i < (int)blocks_.size(); ++i)
        if (blocks_[(size_t)i].get() == &from)
            index = i;
    if (index < 0)
        return;
    const int cols = columnsFor(getWidth());
    const int next = juce::jlimit(0, (int)blocks_.size() - 1, index + dx + dy * cols);
    blocks_[(size_t)next]->grabKeyboardFocus();
}

// The box is sunken: darker than the card around it, a hairline border and a soft shade along its top edge.
void StepBlockBox::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(c.bg0);
    g.fillRoundedRectangle(r, 7.0f);
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, 7.0f);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.28f), 0.0f, r.getY(),
                                               juce::Colours::transparentBlack, 0.0f, r.getY() + 8.0f, false));
        g.fillRect(r);
    }
    g.setColour(c.border);
    g.drawRoundedRectangle(r, 7.0f, 1.0f);
}

} // namespace synth
