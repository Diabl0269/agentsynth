// EQBandStrip.cpp -- see EQBandStrip.h.

#include "EQBandStrip.h"

#include "EQBandKinds.h"
#include "ModuleViewAccessibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kChipH = 22;
constexpr int kGap = 6;
constexpr int kChipPad = 10;
constexpr float kOffAlpha = 0.35f;
} // namespace

struct EQBandStrip::Glide {
    explicit Glide(juce::Component& owner)
        : tween(owner) {}
    KeyedRectTween tween;
};

// One band's chip (or the + chip, band -1): its label is "7 Bell", tinted by the band's kind. The selected chip is
// solid with an accent outline; an off band that is still selected is drawn at 35%.
class EQBandStrip::Chip : public juce::Button {
public:
    explicit Chip(int band)
        : juce::Button("Band")
        , band_(band) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    int band() const noexcept { return band_; }

    void setContent(const juce::String& label, synth::eq::BandType type, bool on, bool selected) {
        label_ = label;
        type_ = type;
        on_ = on;
        setToggleState(selected, juce::dontSendNotification);
        repaint();
    }

    int idealWidth() const {
        return juce::GlyphArrangement::getStringWidthInt(juce::Font(11.0f), label_) + 2 * kChipPad;
    }

    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const float radius = area.getHeight() * 0.5f;
        const float alpha = on_ || band_ < 0 ? 1.0f : kOffAlpha;
        const bool selected = getToggleState();
        const auto tint = band_ < 0 ? colors.textMuted : eqBandKindColour(colors, eqBandKindOf(type_));

        g.setColour(tint.withAlpha((selected ? 1.0f : over ? 0.34f : 0.22f) * alpha));
        g.fillRoundedRectangle(area, radius);
        g.setColour((selected ? colors.accent : tint.withAlpha(0.7f)).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(area, radius, selected ? 2.0f : 1.0f);
        g.setColour((selected ? eqBandKindInk(colors) : colors.textPrimary).withMultipliedAlpha(alpha));
        g.setFont(juce::Font(11.0f));
        g.drawText(label_, getLocalBounds(), juce::Justification::centred, false);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, radius);
    }

    // Left/Right walk the chips; Up/Down are the strip's ArrowKeyNavigation. Return and Space press the chip.
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
            moveKeyboardFocusToSibling(key == juce::KeyPress::rightKey);
            return true;
        }
        if (key == juce::KeyPress::returnKey) {
            if (onClick)
                onClick();
            return true;
        }
        return juce::Button::keyPressed(key);
    }

private:
    int band_;
    juce::String label_;
    synth::eq::BandType type_ = synth::eq::BandType::Peak;
    bool on_ = true;
};

EQBandStrip::EQBandStrip(EQCurveComponent& curve)
    : curve_(curve)
    , glide_(std::make_unique<Glide>(*this))
    , arrows_(*this) {
    setTitle("EQ bands");
    addChip_ = std::make_unique<Chip>(-1);
    addChip_->setContent("+", synth::eq::BandType::Peak, true, false);
    addChip_->setTitle("Add band");
    addChip_->setTooltip("Add a band in the widest gap between the others");
    addChip_->onClick = [this] { curve_.addBandInLargestGap(); };
    addAndMakeVisible(*addChip_);

    glide_->tween.onDirty = [this](juce::Rectangle<int>) { applyDrawnBounds(); };
    curve_.addListener(this);
    refresh();
}

EQBandStrip::~EQBandStrip() { curve_.removeListener(this); }

KeyedRectTween& EQBandStrip::getGlideForTest() noexcept { return glide_->tween; }

void EQBandStrip::eqCurveChanged() { refresh(); }

// The bands that are on, plus the selected band while it is off, in frequency order; equal frequencies keep slot order.
std::vector<int> EQBandStrip::shownBandsInOrder() const {
    const auto& bands = curve_.getBands();
    std::vector<int> shown;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        if (bands[(size_t)b].enabled || b == curve_.getSelectedBand())
            shown.push_back(b);
    std::stable_sort(shown.begin(), shown.end(),
                     [&bands](int a, int b) { return bands[(size_t)a].freqHz < bands[(size_t)b].freqHz; });
    return shown;
}

std::vector<int> EQBandStrip::getChipBands() const { return shownBandsInOrder(); }

juce::Button* EQBandStrip::getChip(int band) const {
    const auto it = chips_.find(band);
    return it == chips_.end() ? nullptr : it->second.get();
}

juce::Button* EQBandStrip::getAddChip() const { return addChip_.get(); }

EQBandStrip::Chip* EQBandStrip::chipForKey(int key) const {
    if (key == kAddKey)
        return addChip_.get();
    const auto it = chips_.find(key);
    return it == chips_.end() ? nullptr : it->second.get();
}

// Chips fill a row left to right and wrap to the next when the next one would pass the edge.
std::vector<EQBandStrip::Place> EQBandStrip::layoutFor(int width) const {
    std::vector<Place> places;
    int x = 0;
    int y = 0;
    auto order = shownBandsInOrder();
    order.push_back(kAddKey);
    for (const int key : order) {
        const auto* chip = chipForKey(key);
        const int w = chip != nullptr ? chip->idealWidth() : 40;
        if (x > 0 && x + w > width) {
            x = 0;
            y += kChipH + kGap;
        }
        places.push_back({key, {x, y, w, kChipH}});
        x += w + kGap;
    }
    return places;
}

int EQBandStrip::heightForWidth(int width) const {
    const auto places = layoutFor(std::max(width, 1));
    return places.empty() ? kChipH : places.back().bounds.getBottom();
}

void EQBandStrip::refresh() {
    const auto& bands = curve_.getBands();
    const auto shown = shownBandsInOrder();

    for (auto it = chips_.begin(); it != chips_.end();) {
        if (std::find(shown.begin(), shown.end(), it->first) == shown.end()) {
            glide_->tween.erase(it->first);
            resting_.erase(it->first);
            it = chips_.erase(it);
        } else {
            ++it;
        }
    }
    for (const int b : shown) {
        auto& chip = chips_[b];
        if (chip == nullptr) {
            chip = std::make_unique<Chip>(b);
            chip->onClick = [this, b] { curve_.setSelectedBand(b); };
            addAndMakeVisible(*chip);
        }
        const auto& band = bands[(size_t)b];
        const auto typeName = eqBandTypeName(band.type);
        chip->setContent(juce::String(b + 1) + " " + typeName, band.type, band.enabled, b == curve_.getSelectedBand());
        chip->setTitle(describeEqBandChip(b, typeName, band.freqHz));
        chip->setTooltip(describeEqBand(b, typeName, band.enabled, band.freqHz, band.gainDb, band.q) +
                         ". Press to select it on the curve");
    }
    placeChips(true);
}

// Gives every chip its resting place for the current width. A chip that has moved glides from where it is drawn; a new
// chip grows out of its own middle.
void EQBandStrip::placeChips(bool animate) {
    const auto places = layoutFor(getWidth() > 0 ? getWidth() : 1000);
    for (const auto& place : places) {
        auto* chip = chipForKey(place.key);
        if (chip == nullptr)
            continue;
        const auto oldRest = resting_.find(place.key);
        const auto target = place.bounds.toFloat();
        if (animate && getWidth() > 0 && oldRest == resting_.end()) {
            if (place.key != kAddKey) {
                const auto centre = target.getCentre();
                glide_->tween.retarget(
                    place.key, juce::Rectangle<float>(centre.x, target.getY(), 0.0f, target.getHeight()), target);
            }
        } else if (animate && oldRest != resting_.end() && oldRest->second != place.bounds) {
            glide_->tween.retarget(place.key, glide_->tween.drawnAt(place.key, oldRest->second.toFloat()), target);
        } else if (!animate) {
            glide_->tween.erase(place.key);
        }
        resting_[place.key] = place.bounds;
    }
    applyDrawnBounds();

    const int height = heightForWidth(getWidth() > 0 ? getWidth() : 1000);
    if (height != lastHeight_) {
        lastHeight_ = height;
        if (onPreferredHeightChanged)
            onPreferredHeightChanged();
    }
}

void EQBandStrip::applyDrawnBounds() {
    for (const auto& [key, rest] : resting_) {
        auto* chip = chipForKey(key);
        if (chip == nullptr)
            continue;
        const auto drawn = glide_->tween.drawnAt(key, rest.toFloat());
        chip->setBounds(drawn.toNearestInt());
    }
}

void EQBandStrip::resized() { placeChips(false); }

} // namespace synth::ui
