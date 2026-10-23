#pragma once

// ShapeListEditorInternal.h -- the shape chips (a colour dot and a name per shape) and the strip that wraps them.
#include "ShapeListEditor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

/** The 30 Hz tick that reads the live spectrum. It exists apart from the editor's own poll so that it can stop: it
    runs only while the Show spectrum toggle is on and the editor is visible. */
class ShapeListEditor::SpectrumTicker : private juce::Timer {
public:
    static constexpr int kHz = 30;

    explicit SpectrumTicker(std::function<void()> onTick)
        : onTick_(std::move(onTick)) {}
    ~SpectrumTicker() override { stopTimer(); }

    void setRunning(bool run) {
        if (run && !isTimerRunning())
            startTimerHz(kHz);
        else if (!run)
            stopTimer();
    }
    bool isRunning() const { return isTimerRunning(); }

private:
    void timerCallback() override { onTick_(); }

    std::function<void()> onTick_;
};

/** One chip: pressing it selects its shape. A radio button, so exactly one chip is lit. */
class ShapeListEditor::Chip
    : public juce::Button
    , public SoundDropAdapter
    , private ShapeDropTarget {
public:
    static constexpr int kHeight = 26;

    /** What the chip asks of the editor when a library shape is dragged over it. */
    struct DropHooks {
        std::function<bool(synth::shapes::ShapeKind)> accepts;
        std::function<void(const synth::shapes::LibraryShape&)> dropped;
    };

    Chip(int shapeIndex, const juce::String& shapeName, DropHooks hooks)
        : juce::Button(shapeName)
        , index_(shapeIndex)
        , hooks_(std::move(hooks)) {
        setClickingTogglesState(true);
        setRadioGroupId(7411);
        setWantsKeyboardFocus(true);
        setTitle(shapeName + " shape");
        setTooltip("Select " + shapeName);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int getIndex() const noexcept { return index_; }
    int getIdealWidth() const {
        return 34 + juce::GlyphArrangement::getStringWidthInt(juce::Font(juce::FontOptions(12.0f)), getButtonText());
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(getToggleState() ? colors.surfaceHi : colors.bg1);
        g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
        g.setColour(getToggleState() ? colors.accent : colors.border.withAlpha(highlighted ? 1.0f : 0.7f));
        g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, 1.0f);
        if (dropOver_ || isShowingSoundOutline()) {
            g.setColour(colors.accent.withAlpha(dropOver_ ? 1.0f : 0.35f));
            g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, dropOver_ ? 2.0f : 1.0f);
        }
        const float dot = 10.0f;
        g.setColour(morphShapeColour(colors, index_));
        g.fillEllipse(bounds.getX() + 9.0f, bounds.getCentreY() - dot * 0.5f, dot, dot);
        g.setColour(colors.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(25).withTrimmedRight(8),
                   juce::Justification::centredLeft, true);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, bounds.getHeight() * 0.5f);
    }

    ShapeDropTarget* shapeDropTarget() override { return this; }
    bool isDropHighlighted() const noexcept { return dropOver_; }

private:
    bool acceptsShapeDrop(synth::shapes::ShapeKind kind) const override {
        return hooks_.accepts && hooks_.accepts(kind);
    }
    void shapeDragOver(bool over, const juce::String& name) override {
        dropOver_ = over;
        setTooltip(over ? "Drop to stamp " + name + " onto " + getButtonText() : "Select " + getButtonText());
        repaint();
    }
    void shapeDropped(const synth::shapes::LibraryShape& shape) override {
        if (hooks_.dropped)
            hooks_.dropped(shape);
    }

    int index_;
    DropHooks hooks_;
    bool dropOver_ = false;
};

/** The chips in a wrapping row; it grows downwards as shapes are added. */
class ShapeListEditor::ChipStrip : public juce::Component {
public:
    explicit ChipStrip(ShapeListEditor& owner)
        : owner_(owner) {
        setTitle("Shapes");
        setWantsKeyboardFocus(false);
    }

    void setShapes(const std::vector<ShapeItem>& shapes) {
        bool same = shapes.size() == chips_.size();
        for (size_t i = 0; same && i < shapes.size(); ++i)
            same = chips_[i]->getButtonText() == shapes[i].name;
        if (same)
            return;
        chips_.clear();
        for (int i = 0; i < (int)shapes.size(); ++i) {
            Chip::DropHooks hooks;
            hooks.accepts = [this](synth::shapes::ShapeKind kind) { return owner_.acceptsLibraryShape(kind); };
            hooks.dropped = [this, i](const synth::shapes::LibraryShape& shape) { owner_.stampLibraryShape(i, shape); };
            auto chip = std::make_unique<Chip>(i, shapes[(size_t)i].name, std::move(hooks));
            chip->onClick = [this, i] { owner_.setSelected(i); };
            addAndMakeVisible(*chip);
            chips_.push_back(std::move(chip));
        }
        resized();
    }

    void setSelected(int index) {
        for (auto& chip : chips_)
            chip->setToggleState(chip->getIndex() == index, juce::dontSendNotification);
    }

    Chip* getChip(int index) const {
        return index >= 0 && index < (int)chips_.size() ? chips_[(size_t)index].get() : nullptr;
    }

    int getRequiredHeight(int width) const { return layout(width, false); }

    void resized() override { layout(getWidth(), true); }

private:
    int layout(int width, bool apply) const {
        constexpr int kGap = 6;
        int x = 0, y = 0;
        for (const auto& chip : chips_) {
            const int w = juce::jmin(chip->getIdealWidth(), juce::jmax(40, width));
            if (x > 0 && x + w > width) {
                x = 0;
                y += Chip::kHeight + kGap;
            }
            if (apply)
                chip->setBounds(x, y, w, Chip::kHeight);
            x += w + kGap;
        }
        return y + Chip::kHeight;
    }

    ShapeListEditor& owner_;
    std::vector<std::unique_ptr<Chip>> chips_;
};

} // namespace synth::ui
