// EQBandStrip.cpp -- see EQBandStrip.h.

#include "EQBandStrip.h"

#include "EQBandKinds.h"
#include "ModuleViewAccessibility.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/FadeAmount.h"
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
constexpr float kGlyphW = 14.0f;
constexpr float kGlyphH = 10.0f;
constexpr int kNumberGap = 4;
constexpr float kChipFont = 11.0f;
} // namespace

// One tween per chip key: a KeyedTween runs ONE clock for all its keys and a retarget restarts it, so chips sharing a
// tween would all slow down and stretch whenever another chip is retargeted (every step of a dot drag past neighbours).
// A tween is created on first use and kept (at most one per band, leaving band and the + chip).
struct EQBandStrip::Glide {
    Glide(juce::Component& ownerIn, std::function<void()> onDirtyIn)
        : owner(ownerIn)
        , onDirty(std::move(onDirtyIn)) {}

    KeyedRectTween& at(int key) {
        auto& slot = tweens[key];
        if (slot == nullptr) {
            slot = std::make_unique<KeyedRectTween>(owner);
            slot->forceAnimateForTest(forceAnimate);
            slot->onDirty = [this](juce::Rectangle<int>) { onDirty(); };
        }
        return *slot;
    }
    KeyedRectTween* find(int key) const {
        const auto it = tweens.find(key);
        return it == tweens.end() ? nullptr : it->second.get();
    }
    juce::Rectangle<float> offsetFor(int key) const {
        const auto* t = find(key);
        return t != nullptr ? t->offsetFor(key) : juce::Rectangle<float>();
    }
    juce::Rectangle<float> drawnAt(int key, juce::Rectangle<float> resting) const {
        return TweenMath<juce::Rectangle<float>>::add(resting, offsetFor(key));
    }
    void erase(int key) {
        if (auto* t = find(key))
            t->erase(key);
    }

    juce::Component& owner;
    std::function<void()> onDirty;
    std::map<int, std::unique_ptr<KeyedRectTween>> tweens;
    bool forceAnimate = false;
};

// One band's chip (or the + chip, band -1): its number then the type's response-curve glyph (the label "7 Bell" is
// what test mode reports; the tooltip and the screen-reader title name the type), tinted by the band's kind. The
// selected chip is solid with an accent outline; an off band that is still selected is drawn at 35%.
class EQBandStrip::Chip : public juce::Button {
public:
    explicit Chip(int band)
        : juce::Button("Band")
        , band_(band) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        glyphFade_.snapTo(true);
    }

    int band() const noexcept { return band_; }
    /** Asked at hover time, so a tooltip that names a key follows a rebind. */
    std::function<juce::String()> liveTooltip;
    juce::String getTooltip() override { return liveTooltip ? liveTooltip() : juce::Button::getTooltip(); }
    const juce::String& label() const noexcept { return label_; }

    void setContent(const juce::String& label, synth::eq::BandType type, bool on, bool selected) {
        if (label == label_ && type == type_ && on == on_ && selected == getToggleState())
            return;
        if (type != type_ && !label_.isEmpty()) { // the old glyph fades out as the new one fades in
            fromType_ = type_;
            glyphFade_.snapTo(false);
            glyphFade_.setShown(true);
        }
        label_ = label;
        type_ = type;
        on_ = on;
        setToggleState(selected, juce::dontSendNotification);
        repaint();
    }

    int idealWidth() const {
        if (band_ < 0)
            return juce::GlyphArrangement::getStringWidthInt(juce::Font(kChipFont), label_) + 2 * kChipPad;
        return juce::GlyphArrangement::getStringWidthInt(juce::Font(kChipFont), numberText()) + kNumberGap +
               (int)kGlyphW + 2 * kChipPad;
    }

    /** Tests: the glyph's box in the chip, and how far the new type's glyph has faded in (1 at rest). */
    juce::Rectangle<float> glyphArea() const {
        const float numberW = (float)juce::GlyphArrangement::getStringWidthInt(juce::Font(kChipFont), numberText());
        const float total = numberW + (float)kNumberGap + kGlyphW;
        const float x = ((float)getWidth() - total) * 0.5f + numberW + (float)kNumberGap;
        return juce::Rectangle<float>(kGlyphW, kGlyphH).withCentre({x + kGlyphW * 0.5f, (float)getHeight() * 0.5f});
    }
    float glyphFadeValue() const noexcept { return glyphFade_.value(); }

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
        const auto ink = (selected ? eqBandKindInk(colors) : colors.textPrimary).withMultipliedAlpha(alpha);
        g.setColour(ink);
        g.setFont(juce::Font(kChipFont));
        if (band_ < 0) {
            g.drawText(label_, getLocalBounds(), juce::Justification::centred, false);
        } else {
            const float numberW = (float)juce::GlyphArrangement::getStringWidthInt(juce::Font(kChipFont), numberText());
            const float total = numberW + (float)kNumberGap + kGlyphW;
            const float x = ((float)getWidth() - total) * 0.5f;
            g.drawText(numberText(), juce::Rectangle<float>(x, 0.0f, numberW, (float)getHeight()),
                       juce::Justification::centredLeft, false);
            const float fade = glyphFade_.value();
            if (fade < 1.0f)
                paintEqBandTypeGlyph(g, glyphArea(), ink.withMultipliedAlpha(1.0f - fade), fromType_);
            paintEqBandTypeGlyph(g, glyphArea(), ink.withMultipliedAlpha(fade), type_);
        }
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
    juce::String numberText() const { return juce::String(band_ + 1); }

    int band_;
    juce::String label_;
    synth::eq::BandType type_ = synth::eq::BandType::Peak;
    synth::eq::BandType fromType_ = synth::eq::BandType::Peak;
    FadeAmount glyphFade_{*this};
    bool on_ = true;
};

EQBandStrip::EQBandStrip(EQCurveComponent& curve)
    : curve_(curve)
    , glide_(std::make_unique<Glide>(*this, [this] { applyDrawnBounds(); }))
    , arrows_(*this) {
    setTitle("EQ bands");
    addChip_ = std::make_unique<Chip>(-1);
    addChip_->setContent("+", synth::eq::BandType::Peak, true, false);
    addChip_->setComponentID("eqAddBand");
    addChip_->setTitle("Add band");
    addChip_->liveTooltip = [this] {
        const auto key = EQCurveComponent::keyHint(curve_.getShortcutManager(), "eqAddBand");
        return juce::String("Add a band in the widest gap between the others") +
               (key.isEmpty() ? juce::String() : " (" + key + ")");
    };
    addChip_->onClick = [this] { curve_.addBandInLargestGap(); };
    addAndMakeVisible(*addChip_);

    curve_.addListener(this);
    refresh();
}

EQBandStrip::~EQBandStrip() { curve_.removeListener(this); }

KeyedRectTween* EQBandStrip::getGlideForTest(int key) const noexcept { return glide_->find(key); }

void EQBandStrip::forceAnimateForTest(bool force) {
    glide_->forceAnimate = force;
    for (auto& [key, tween] : glide_->tweens)
        tween->forceAnimateForTest(force);
}

bool EQBandStrip::isGlideAnimatingForTest() const {
    return std::any_of(glide_->tweens.begin(), glide_->tweens.end(),
                       [](const auto& e) { return e.second->isAnimating(); });
}

void EQBandStrip::stepGlideForTest(double ms) {
    for (auto& [key, tween] : glide_->tweens)
        tween->stepForTest(ms);
}

void EQBandStrip::completeGlideForTest() {
    for (auto& [key, tween] : glide_->tweens)
        tween->completeForTest();
}

void EQBandStrip::eqCurveChanged() { refresh(); }

void EQBandStrip::addTestModeState(juce::DynamicObject& into) const {
    juce::Array<juce::var> labels;
    for (const int band : shownBandsInOrder())
        if (const auto* chip = chipForKey(band))
            labels.add(chip->label());
    into.setProperty("chips", labels);
    into.setProperty("selectedBand", curve_.getSelectedBand() + 1);
    into.setProperty("selectedCount", (int)curve_.getSelectedBands().size());
    // The curve beside the strip: its band count, the soloed band (1-based, 0 for none), how strongly that dot's ring
    // shows and the most any other dot is dimmed (0..1), and the selected band's values (0 when none is selected).
    const int solo = curve_.getSoloBand();
    float dimmed = 0.0f;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
        if (b != solo && curve_.getBands()[(size_t)b].enabled)
            dimmed = juce::jmax(dimmed, curve_.getSoloLook(b).y);
    into.setProperty("bandCount", curve_.getModule().getEnabledBandCount());
    into.setProperty("soloBand", solo + 1);
    into.setProperty("soloRing", solo >= 0 ? (double)curve_.getSoloLook(solo).x : 0.0);
    into.setProperty("othersDimmed", (double)dimmed);
    const int sel = curve_.getSelectedBand();
    into.setProperty("freqHz", sel >= 0 ? (double)curve_.getBands()[(size_t)sel].freqHz : 0.0);
    into.setProperty("gainDb", sel >= 0 ? (double)curve_.getBands()[(size_t)sel].gainDb : 0.0);
    into.setProperty("q", sel >= 0 ? (double)curve_.getBands()[(size_t)sel].q : 0.0);
}

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
            retireChip(it->first, std::move(it->second));
            glide_->erase(it->first);
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
            chip->setComponentID("eqChip" + juce::String(b + 1));
            chip->onClick = [this, b] { curve_.setSelectedBand(b); };
            chip->liveTooltip = [this, b] {
                const auto& now = curve_.getBands()[(size_t)b];
                return describeEqBand(b, eqBandTypeName(now.type), now.enabled, now.freqHz, now.gainDb, now.q) +
                       ". Press to select it on the curve";
            };
            addAndMakeVisible(*chip);
        }
        const auto& band = bands[(size_t)b];
        const auto typeName = eqBandTypeName(band.type);
        chip->setContent(juce::String(b + 1) + " " + typeName, band.type, band.enabled, b == curve_.getSelectedBand());
        const auto title = describeEqBandChip(b, typeName, band.freqHz);
        if (chip->getTitle() != title)
            chip->setTitle(title);
    }
    placeChips(true);
}

// A chip whose band left shrinks into its own middle instead of vanishing; once it has landed it is deleted.
void EQBandStrip::retireChip(int band, std::unique_ptr<Chip> chip) {
    const auto drawn = chip->getBounds().toFloat();
    const auto centre = drawn.getCentre();
    const int key = kLeavingKeyBase + band;
    chip->setInterceptsMouseClicks(false, false);
    chip->setWantsKeyboardFocus(false);
    chip->setTitle({});
    glide_->at(key).retarget(key, drawn, juce::Rectangle<float>(centre.x, drawn.getY(), 0.0f, drawn.getHeight()));
    leaving_[band] = std::move(chip);
    leavingRest_[key] = juce::Rectangle<float>(centre.x, drawn.getY(), 0.0f, drawn.getHeight());
}

void EQBandStrip::pruneLeaving() {
    for (auto it = leaving_.begin(); it != leaving_.end();) {
        const int key = kLeavingKeyBase + it->first;
        if (glide_->offsetFor(key) == juce::Rectangle<float>()) {
            leavingRest_.erase(key);
            it = leaving_.erase(it);
        } else {
            ++it;
        }
    }
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
                glide_->at(place.key).retarget(
                    place.key, juce::Rectangle<float>(centre.x, target.getY(), 0.0f, target.getHeight()), target);
            }
        } else if (animate && oldRest != resting_.end() && oldRest->second != place.bounds) {
            glide_->at(place.key).retarget(place.key, glide_->drawnAt(place.key, oldRest->second.toFloat()), target);
        } else if (!animate) {
            glide_->erase(place.key);
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
    for (const auto& [band, chip] : leaving_)
        chip->setBounds(glide_->drawnAt(kLeavingKeyBase + band, leavingRest_[kLeavingKeyBase + band]).toNearestInt());
    pruneLeaving();
    for (const auto& [key, rest] : resting_) {
        auto* chip = chipForKey(key);
        if (chip == nullptr)
            continue;
        const auto drawn = glide_->drawnAt(key, rest.toFloat());
        chip->setBounds(drawn.toNearestInt());
    }
}

void EQBandStrip::resized() { placeChips(false); }

} // namespace synth::ui
