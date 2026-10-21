// StepGridTools.cpp -- the tools row: eight chip buttons with a drawn icon and an optional label. The look is the
// shared chip (paintChip); icons are paths, so they draw the same in a headless build.
#include "StepGridTools.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using Tool = StepGridTools::Tool;

namespace {

constexpr float kFontHeight = 11.0f;
constexpr int kPadding = 8;
constexpr int kIconSize = 10;
constexpr int kIconGap = 4;
constexpr int kButtonGap = 4;

struct ToolInfo {
    const char* label; ///< Empty = icon only.
    const char* tooltip;
};

const ToolInfo& infoOf(Tool tool) {
    static const ToolInfo kInfo[StepGridTools::kToolCount] = {
        {"Randomize", "Randomize notes in the scale"},
        {"", "Shift pattern one step left"},
        {"", "Shift pattern one step right"},
        {"Reverse", "Reverse pattern"},
        {"Double", "Copy the steps into a pattern twice as long"},
        {"", "Move every note up one scale note"},
        {"", "Move every note down one scale note"},
        {"Fit to scale", "Rewrite the notes into the scale"},
    };
    return kInfo[(int)tool];
}

void strokeArrow(juce::Path& p, juce::Rectangle<float> r, bool toRight) {
    const float y = r.getCentreY();
    const float tip = toRight ? r.getRight() : r.getX();
    const float tail = toRight ? r.getX() : r.getRight();
    const float head = toRight ? -3.0f : 3.0f;
    p.startNewSubPath(tail, y);
    p.lineTo(tip, y);
    p.startNewSubPath(tip + head, y - 3.0f);
    p.lineTo(tip, y);
    p.lineTo(tip + head, y + 3.0f);
}

// Draws the tool's icon in `area` (a kIconSize square).
void paintIcon(juce::Graphics& g, Tool tool, juce::Rectangle<float> area, juce::Colour colour) {
    g.setColour(colour);
    const auto c = area.getCentre();
    juce::Path path;
    switch (tool) {
    case Tool::Randomize: {
        const auto die = area.reduced(0.5f);
        g.drawRoundedRectangle(die, 2.0f, 1.2f);
        for (const float offset : {-2.4f, 0.0f, 2.4f})
            g.fillEllipse(c.x + offset - 1.0f, c.y + offset - 1.0f, 2.0f, 2.0f);
        return;
    }
    case Tool::ShiftLeft:
    case Tool::ShiftRight: {
        const bool right = tool == Tool::ShiftRight;
        path.addTriangle(right ? area.getX() + 2.0f : area.getRight() - 2.0f, area.getY() + 1.0f,
                         right ? area.getX() + 2.0f : area.getRight() - 2.0f, area.getBottom() - 1.0f,
                         right ? area.getRight() - 1.0f : area.getX() + 1.0f, c.y);
        break;
    }
    case Tool::Up:
    case Tool::Down: {
        const bool up = tool == Tool::Up;
        path.addTriangle(area.getX() + 1.0f, up ? area.getBottom() - 2.0f : area.getY() + 2.0f, area.getRight() - 1.0f,
                         up ? area.getBottom() - 2.0f : area.getY() + 2.0f, c.x,
                         up ? area.getY() + 1.0f : area.getBottom() - 1.0f);
        break;
    }
    case Tool::Reverse:
        strokeArrow(path, area.withTrimmedBottom(5.0f), true);
        strokeArrow(path, area.withTrimmedTop(5.0f), false);
        g.strokePath(path, juce::PathStrokeType(1.2f));
        return;
    case Tool::Double:
        g.setFont(theme::AppLookAndFeel::uiSemiBoldFont(kFontHeight));
        g.drawText("x2", area.expanded(3.0f, 0.0f), juce::Justification::centred);
        return;
    case Tool::FitToScale: {
        // A sharp sign: two slanted verticals, two horizontals.
        for (const float dx : {-1.8f, 1.8f}) {
            path.startNewSubPath(c.x + dx + 0.8f, area.getY() + 0.5f);
            path.lineTo(c.x + dx - 0.8f, area.getBottom() - 0.5f);
        }
        for (const float dy : {-1.8f, 1.8f}) {
            path.startNewSubPath(area.getX() + 0.5f, c.y + dy + 0.7f);
            path.lineTo(area.getRight() - 0.5f, c.y + dy - 0.7f);
        }
        g.strokePath(path, juce::PathStrokeType(1.2f));
        return;
    }
    }
    g.fillPath(path);
}

} // namespace

// One chip: fill and border from the theme's chip, the icon in the muted ink, the label in the primary ink, the
// shared focus ring.
class StepGridTools::ToolButton : public juce::Button {
public:
    explicit ToolButton(Tool tool)
        : juce::Button(infoOf(tool).label)
        , tool_(tool) {
        const juce::String tip = infoOf(tool).tooltip;
        setTitle(tip);
        setTooltip(tip);
        setWantsKeyboardFocus(true);
    }

    int preferredWidth() const {
        const juce::String label = infoOf(tool_).label;
        if (label.isEmpty())
            return 2 * kPadding + kIconSize + 2;
        return 2 * kPadding + kIconSize + kIconGap + theme::AppLookAndFeel::uiTextWidth(label, kFontHeight);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto& look = theme::themeOf(*this);
        const float alpha = isEnabled() ? 1.0f : theme::AppLookAndFeel::kDisabledControlAlpha;
        theme::ChipState state;
        state.hovered = highlighted && isEnabled();
        state.down = down && isEnabled();
        state.raised = true;
        {
            juce::Graphics::ScopedSaveState save(g);
            g.setOpacity(alpha);
            theme::paintChip(g, getLocalBounds().toFloat(), look, state);
            const juce::String label = getButtonText();
            const int contentWidth =
                label.isEmpty() ? kIconSize
                                : kIconSize + kIconGap + theme::AppLookAndFeel::uiTextWidth(label, kFontHeight);
            const float left = (float)(getWidth() - contentWidth) * 0.5f;
            const juce::Rectangle<float> icon(left, ((float)getHeight() - (float)kIconSize) * 0.5f, (float)kIconSize,
                                              (float)kIconSize);
            paintIcon(g, tool_, icon, look.colors.textMuted.withMultipliedAlpha(alpha));
            if (label.isNotEmpty()) {
                g.setColour(look.colors.textPrimary.withMultipliedAlpha(alpha));
                g.setFont(theme::AppLookAndFeel::uiFont(kFontHeight));
                g.drawText(label,
                           juce::Rectangle<float>(icon.getRight() + (float)kIconGap, 0.0f,
                                                  (float)contentWidth - (float)kIconSize - (float)kIconGap + 2.0f,
                                                  (float)getHeight()),
                           juce::Justification::centredLeft);
            }
        }
        paintFocusRing(g, getLocalBounds().toFloat(), *this, look.metrics.cornerRadiusSmall);
    }

private:
    Tool tool_;
};

StepGridTools::StepGridTools() {
    setTitle("Pattern tools");
    for (int i = 0; i < kToolCount; ++i) {
        const auto tool = (Tool)i;
        buttons_[(size_t)i] = std::make_unique<ToolButton>(tool);
        auto& button = *buttons_[(size_t)i];
        button.onClick = [this, tool] {
            if (onTool)
                onTool(tool);
        };
        addAndMakeVisible(button);
    }
}

StepGridTools::~StepGridTools() = default;

juce::Button& StepGridTools::getButton(Tool tool) noexcept { return *buttons_[(size_t)tool]; }

const juce::Button& StepGridTools::getButton(Tool tool) const noexcept { return *buttons_[(size_t)tool]; }

juce::String StepGridTools::tooltipFor(Tool tool) { return infoOf(tool).tooltip; }

void StepGridTools::setAvailability(bool canDouble, bool canFit) {
    getButton(Tool::Double).setEnabled(canDouble);
    getButton(Tool::FitToScale).setEnabled(canFit);
}

// Right-aligned, vertically centred in the row, 4 px apart.
void StepGridTools::resized() {
    int x = getWidth();
    const int y = (getHeight() - kButtonHeight) / 2;
    for (int i = kToolCount - 1; i >= 0; --i) {
        auto& button = *buttons_[(size_t)i];
        const int width = button.preferredWidth();
        x -= width;
        button.setBounds(x, y, width, kButtonHeight);
        x -= kButtonGap;
    }
}

} // namespace synth::ui
