#include "PortConnectionRow.h"

#include "UI/Graph/ModDot/ModDotPalette.h"

namespace synth::ui {

namespace {
constexpr int kSwatch = 10;
constexpr float kFont = 12.5f;
} // namespace

PortConnectionRow::PortConnectionRow(const PortConnection& connection)
    : connection_(connection)
    , hover_(*this)
    , removeButton_(ModDotGlyph::Trash, {}, {}) {
    addAndMakeVisible(removeButton_);
    addMouseListener(this, true); // hover over the button lights the row
    removeButton_.onClick = [this] {
        if (onRemove)
            onRemove(*this);
    };
    removeButton_.onFocusChanged = [this](bool focused) {
        focused_ = focused;
        refreshHighlight();
    };
    if (connection_.isModulation()) {
        mod_ = std::make_unique<ModDotSourceRow>(modSource(), juce::String());
        // "Show in timeline" only when the chain resolves to a knob, the lane it reveals.
        mod_->setLook({kHeight, ModDotSourceRow::kElasticName, connection_.modCard.uid != 0});
        addAndMakeVisible(*mod_);
        removeButton_.setVisible(false);
        mod_->onRemove = [this](ModDotSourceRow&) {
            if (onRemove)
                onRemove(*this);
        };
        mod_->onNavigate = [this](ModDotSourceRow&, int step) {
            if (onNavigate)
                onNavigate(*this, step);
        };
    }
    applyNames();
}

// The chain as listed, under the far end's name: the panel names what is at the other end of the cable.
KnobModSource PortConnectionRow::modSource() const {
    auto source = connection_.modSource;
    source.sourceName = connection_.label;
    source.fullName = connection_.fullLabel;
    return source;
}

void PortConnectionRow::focusOfChildComponentChanged(FocusChangeType) {
    focused_ = hasKeyboardFocus(true);
    refreshHighlight();
}

void PortConnectionRow::applyNames() {
    // The row shows the short form; what is spoken and what a tooltip says is the whole name.
    setTitle(connection_.fullLabel + " connection");
    setTooltip(connection_.fullLabel);
    removeButton_.setTitle("Remove connection to " + connection_.fullLabel);
    removeButton_.setTooltip("Remove connection to " + connection_.fullLabel);
}

void PortConnectionRow::update(const PortConnection& connection) {
    const bool renamed = connection.label != connection_.label || connection.fullLabel != connection_.fullLabel;
    const bool recoloured = connection.colour != connection_.colour;
    connection_ = connection;
    if (renamed)
        applyNames();
    if (mod_ != nullptr)
        mod_->update(modSource());
    if (renamed || recoloured)
        repaint();
}

void PortConnectionRow::refreshHighlight() {
    hover_.setHovered(hovered_);
    const bool now = hovered_ || focused_;
    if (now == highlighted_)
        return;
    highlighted_ = now;
    if (onHighlight)
        onHighlight(*this, now);
}

void PortConnectionRow::mouseEnter(const juce::MouseEvent&) {
    hovered_ = true;
    refreshHighlight();
}

// Leaving a child into the row is not leaving the row.
void PortConnectionRow::mouseExit(const juce::MouseEvent&) {
    hovered_ = isMouseOver(true);
    refreshHighlight();
}

void PortConnectionRow::paint(juce::Graphics& g) {
    if (mod_ != nullptr)
        return; // the mod row draws its own swatch, name and hover
    const auto p = modDotPaletteFor(*this);
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(), (float)kHeight).reduced(2.0f, 1.0f);
    if (hover_.value() > 0.0f) {
        g.setColour(p.hover.withAlpha(hover_.value()));
        g.fillRoundedRectangle(area, 6.0f);
    }
    g.setColour(connection_.colour);
    g.fillEllipse(swatchArea_.toFloat());
    g.setColour(p.text);
    g.setFont(juce::Font(juce::FontOptions(kFont)));
    g.drawText(connection_.label, nameArea_, juce::Justification::centredLeft, true);
}

void PortConnectionRow::resized() {
    // Laid out for the full row height whatever height the panel has given it: a row growing in or shrinking out is
    // revealed and hidden, never squashed.
    if (mod_ != nullptr)
        mod_->setBounds(0, 0, getWidth(), kHeight);
    const int gap = modDotPaletteFor(*this).space;
    auto r = juce::Rectangle<int>(0, 0, getWidth(), kHeight).reduced(ModDotGlyphButton::kRowEdgeInset, 0);
    removeButton_.setBounds(r.removeFromRight(ModDotGlyphButton::kSize)
                                .withSizeKeepingCentre(ModDotGlyphButton::kSize, ModDotGlyphButton::kSize));
    r.removeFromRight(gap);
    swatchArea_ = r.removeFromLeft(kSwatch).withSizeKeepingCentre(kSwatch, kSwatch);
    r.removeFromLeft(gap);
    nameArea_ = r;
}

bool PortConnectionRow::keyPressed(const juce::KeyPress& key) {
    if (onNavigate && (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey))) {
        onNavigate(*this, key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

} // namespace synth::ui
