#pragma once

// ShapeListEditorInternal.h -- the shape chips (a colour dot and a name per shape) and the strip that wraps them.
#include "ShapeListEditor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/ModuleViews/MorphMap/MorphMapPalette.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

/** One chip: pressing it selects its shape. A radio button, so exactly one chip is lit. */
class ShapeListEditor::Chip : public juce::Button {
public:
    static constexpr int kHeight = 26;

    Chip(int shapeIndex, const juce::String& shapeName)
        : juce::Button(shapeName)
        , index_(shapeIndex) {
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
        const float dot = 10.0f;
        g.setColour(morphShapeColour(colors, index_));
        g.fillEllipse(bounds.getX() + 9.0f, bounds.getCentreY() - dot * 0.5f, dot, dot);
        g.setColour(colors.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(getButtonText(), getLocalBounds().withTrimmedLeft(25).withTrimmedRight(8),
                   juce::Justification::centredLeft, true);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, bounds.getHeight() * 0.5f);
    }

private:
    int index_;
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
            auto chip = std::make_unique<Chip>(i, shapes[(size_t)i].name);
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
