#include "CommandPaletteComponent.h"
#include "UI/Graph/ModMatrixPicker.h"

#include "UI/Layout/DragCursor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kFrame = 8;       // the frame around the bar that drags it
constexpr int kTitleStrip = 20; // the title line above the bar, also a drag handle
constexpr int kHintSize = 16;

constexpr const char* kTitle = "Search everything";
constexpr const char* kHintText =
    "Type to find actions, views, modules, tracks, markers, settings, projects and presets. Type + to add a track, "
    "module or preset. Up and Down move, Return runs, Tab reaches a row's button, Escape closes. Drag the frame to "
    "move the bar.";

struct Colours {
    juce::Colour frame, border, muted;
    float radius = 8.0f;
};

Colours coloursFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c);
    return {t.colors.bg1, t.colors.border, t.colors.textMuted, t.metrics.cornerRadius + 2.0f};
}

// The (i) in the title strip: hovering it (or a screen reader) explains what the bar finds and the "+" narrowing.
class InfoHint
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    InfoHint() {
        setComponentID("commandPaletteInfo");
        setTitle("About the search bar");
        setDescription(kHintText);
        setTooltip(kHintText);
    }
    void paint(juce::Graphics& g) override {
        const auto c = coloursFor(*this);
        const auto area = getLocalBounds().toFloat().reduced(1.5f);
        g.setColour(c.muted);
        g.drawEllipse(area, 1.2f);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText("i", area, juce::Justification::centred, false);
    }
};

} // namespace

CommandPaletteComponent::CommandPaletteComponent(std::unique_ptr<ModMatrixPicker> picker)
    : picker_(std::move(picker))
    , infoHint_(std::make_unique<InfoHint>()) {
    setComponentID("commandPalette");
    setTitle("Command palette");
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    addAndMakeVisible(*infoHint_);
    addAndMakeVisible(*picker_);
    picker_->setMouseCursor(juce::MouseCursor::NormalCursor);
    constrainer_.setMinimumOnscreenAmounts(0xffffff, 0xffffff, 0xffffff, 0xffffff); // never past the parent's edges
    setSize(kWidth, kHeight);
}

CommandPaletteComponent::~CommandPaletteComponent() = default;

juce::Point<int> CommandPaletteComponent::defaultPosition(juce::Rectangle<int> parent, int topInset) {
    return clampedPosition({parent.getCentreX() - kWidth / 2, parent.getY() + topInset}, parent);
}

juce::Point<int> CommandPaletteComponent::clampedPosition(juce::Point<int> topLeft, juce::Rectangle<int> parent) {
    const int x = juce::jlimit(parent.getX(), juce::jmax(parent.getX(), parent.getRight() - kWidth), topLeft.x);
    const int y = juce::jlimit(parent.getY(), juce::jmax(parent.getY(), parent.getBottom() - kHeight), topLeft.y);
    return {x, y};
}

// The bar rises out of nothing where it sits: it has no button to grow from when the key opened it.
void CommandPaletteComponent::fadeIn() {
    fade_.snapTo(false);
    fade_.setShown(true);
}

void CommandPaletteComponent::fadeOut(std::function<void()> onHidden) {
    fade_.onHidden = onHidden;
    fade_.setShown(false);
    if (!isVisible() && onHidden) {
        fade_.onHidden = nullptr; // landed at once (nothing animates): no fade will report it
        onHidden();
    }
}

void CommandPaletteComponent::paint(juce::Graphics& g) {
    const auto c = coloursFor(*this);
    const auto area = getLocalBounds().toFloat();
    juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 14, {0, 4})
        .drawForRectangle(g, getLocalBounds().reduced(kFrame));
    g.setColour(c.frame);
    g.fillRoundedRectangle(area, c.radius);
    g.setColour(c.border);
    g.drawRoundedRectangle(area.reduced(0.5f), c.radius, 1.0f);
    // Two rows of grip dots say the frame can be dragged.
    g.setColour(c.muted.withAlpha(0.6f));
    const float cx = area.getCentreX();
    for (int row = 0; row < 2; ++row)
        for (int i = -2; i <= 2; ++i)
            g.fillEllipse(cx + (float)i * 6.0f - 1.0f, 6.0f + (float)row * 4.0f, 2.0f, 2.0f);
    g.setColour(c.muted);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(kTitle, juce::Rectangle<int>(kFrame + 4, 2, 200, kTitleStrip), juce::Justification::centredLeft, true);
}

void CommandPaletteComponent::resized() {
    auto inner = getLocalBounds().reduced(kFrame, 0);
    auto strip = inner.removeFromTop(kTitleStrip + 2);
    infoHint_->setBounds(strip.removeFromRight(kHintSize + 4).withSizeKeepingCentre(kHintSize, kHintSize));
    inner.removeFromBottom(kFrame);
    picker_->setBounds(inner);
}

// Only the frame itself drags: the picker and the hint take their own mouse events.
void CommandPaletteComponent::mouseDown(const juce::MouseEvent& e) {
    dragging_ = true;
    dragger_.startDraggingComponent(this, e);
    followDragCursor(*this, true);
}

void CommandPaletteComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_)
        dragger_.dragComponent(this, e, &constrainer_);
}

void CommandPaletteComponent::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    followDragCursor(*this, false);
    if (onMoved)
        onMoved(getPosition());
}

} // namespace synth::ui
