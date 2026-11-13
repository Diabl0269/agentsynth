#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerKeyStrip.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace synth::ui {

namespace {

constexpr int kMinKeys = 12;
constexpr float kKeysHeightShare = 0.4f;
constexpr float kBandGap = 1.0f;
constexpr float kSoftAccentDarken = 0.55f;
constexpr float kBlackKeyHeightShare = 0.62f;
constexpr int kWideVelocityRange = 100;
constexpr int kLoudFromVelocity = 64;
constexpr int kSoftestKeyVelocity = 64; // at the top of the strip; the bottom plays 127
constexpr int kLoudestKeyVelocity = 127;

bool isBlackKey(int note) {
    const int pitchClass = note % 12;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

SamplerKeyStrip::Band bandFor(const synth::sampler::Region& region) {
    SamplerKeyStrip::Band band;
    band.lowKey = region.loKey;
    band.highKey = region.hiKey;
    band.single = region.hiVel - region.loVel >= kWideVelocityRange;
    band.loud = (region.loVel + region.hiVel) / 2 >= kLoudFromVelocity;
    return band;
}

} // namespace

SamplerKeyStrip::SamplerKeyStrip(SamplerModule& sampler)
    : sampler_(sampler) {
    setWantsKeyboardFocus(false);
    setTooltip("Key strip: the keys each recording covers, the loud layer above and the soft layer below. Playing "
               "notes light up. Double-click to edit the map. Click a key to play it.");
    refresh();
}

// A release-sound recording sounds on key-up, so it adds no band of its own.
void SamplerKeyStrip::readInstrument() {
    bands_.clear();
    recordings_ = 0;
    softAndLoud_ = false;
    int low = 127, high = 0;
    std::set<juce::String> files;
    if (const auto instrument = sampler_.getInstrument()) {
        for (const auto& region : instrument->regions) {
            files.insert(region.samplePath);
            if (region.releaseTrigger)
                continue;
            bands_.push_back(bandFor(region));
            low = std::min(low, region.loKey);
            high = std::max(high, region.hiKey);
            softAndLoud_ = softAndLoud_ || !bands_.back().single;
        }
        recordings_ = files.count({}) > 0 ? (int)instrument->regions.size() : (int)files.size();
    }
    if (bands_.empty())
        return;
    const int missing = kMinKeys - (high - low + 1);
    if (missing > 0) {
        low = std::max(0, low - missing / 2);
        high = std::min(127, low + kMinKeys - 1);
        low = std::max(0, high - kMinKeys + 1);
    }
    span_ = {low, high};
}

juce::String SamplerKeyStrip::getCaptionText() const {
    if (recordings_ == 0)
        return {};
    const juce::String count =
        recordings_ == 1 ? juce::String("1 recording") : juce::String(recordings_) + " recordings";
    const juce::String dot = juce::CharPointer_UTF8("\xc2\xb7");
    return count + " " + dot + (softAndLoud_ ? " soft and loud" : " one layer");
}

void SamplerKeyStrip::refresh() {
    if (const int generation = sampler_.getInstrumentGeneration(); generation != generation_) {
        generation_ = generation;
        readInstrument();
        setTitle(recordings_ == 0 ? juce::String("No instrument")
                                  : juce::String(recordings_) + (recordings_ == 1 ? " recording" : " recordings") +
                                        (softAndLoud_ ? ", soft and loud" : ", one layer"));
        repaint();
    }
    setLit(soundingWithHeld());
}

void SamplerKeyStrip::poll() {
    if (const int generation = sampler_.getInstrumentGeneration(); generation != generation_)
        refresh();
    else if (lit_.any() || sampler_.isPlaying())
        setLit(soundingWithHeld());
}

// The held key lights at once, without waiting for the processor to report it as sounding.
std::bitset<128> SamplerKeyStrip::soundingWithHeld() const {
    auto notes = sampler_.getSoundingNotes();
    if (heldKey_ >= 0)
        notes.set((size_t)heldKey_);
    return notes;
}

// Same geometry as paint: the keys span the strip's inner width, and the whole strip is the click target.
int SamplerKeyStrip::keyAt(int x) const {
    if (bands_.empty())
        return -1;
    const auto inner = getLocalBounds().toFloat().reduced(2.0f);
    const float keyWidth = inner.getWidth() / (float)(span_.getEnd() - span_.getStart() + 1);
    if (keyWidth <= 0.0f)
        return -1;
    const int note = span_.getStart() + (int)std::floor(((float)x - inner.getX()) / keyWidth);
    return juce::jlimit(span_.getStart(), span_.getEnd(), note);
}

// Louder toward the bottom of the strip, as the map's keyboard answers.
int SamplerKeyStrip::velocityAt(int y) const {
    const float height = std::max(1.0f, (float)getHeight());
    const float down = juce::jlimit(0.0f, 1.0f, (float)y / height);
    return kSoftestKeyVelocity + juce::roundToInt(down * (float)(kLoudestKeyVelocity - kSoftestKeyVelocity));
}

void SamplerKeyStrip::playHeld(int note, int velocity) {
    heldKey_ = note;
    sampler_.noteInbox().push(note, velocity, 0);
    setLit(soundingWithHeld());
}

// No push from the destructor: the processor can be freed before the card.
void SamplerKeyStrip::releaseHeld() {
    if (heldKey_ < 0)
        return;
    const int note = std::exchange(heldKey_, -1);
    sampler_.noteInbox().push(note, 0, 0);
    setLit(soundingWithHeld());
}

void SamplerKeyStrip::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown() || e.mods.isPopupMenu() || bands_.empty())
        return;
    if (const int note = keyAt(e.x); note >= 0)
        playHeld(note, velocityAt(e.y));
}

void SamplerKeyStrip::mouseDrag(const juce::MouseEvent& e) {
    if (heldKey_ < 0)
        return;
    const int next = keyAt(e.x);
    if (next < 0 || next == heldKey_)
        return;
    releaseHeld();
    playHeld(next, velocityAt(e.y));
}

void SamplerKeyStrip::mouseUp(const juce::MouseEvent&) { releaseHeld(); }

void SamplerKeyStrip::setLit(const std::bitset<128>& notes) {
    if (notes == lit_)
        return;
    lit_ = notes;
    repaint();
}

float SamplerKeyStrip::keyX(int note, juce::Rectangle<float> area) const {
    const float keyWidth = area.getWidth() / (float)(span_.getEnd() - span_.getStart() + 1);
    return area.getX() + (float)(note - span_.getStart()) * keyWidth;
}

void SamplerKeyStrip::paintKeys(juce::Graphics& g, juce::Rectangle<float> area,
                                const synth::theme::Theme& theme) const {
    const float keyWidth = area.getWidth() / (float)(span_.getEnd() - span_.getStart() + 1);
    const auto& colors = theme.colors;
    for (const bool black : {false, true})
        for (int note = span_.getStart(); note <= span_.getEnd(); ++note) {
            if (isBlackKey(note) != black)
                continue;
            auto key = juce::Rectangle<float>(keyX(note, area), area.getY(), keyWidth, area.getHeight());
            if (black)
                key = key.withHeight(area.getHeight() * kBlackKeyHeightShare);
            g.setColour(lit_.test((size_t)note) ? colors.accent
                                                : (black ? colors.pianoKeyBlack : colors.pianoKeyWhite));
            g.fillRect(key.reduced(black ? 0.0f : 0.25f, 0.0f));
            if (!black) {
                g.setColour(colors.border);
                g.fillRect(key.withWidth(0.5f));
            }
        }
}

void SamplerKeyStrip::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme.colors.bg1);
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall);
    if (bands_.empty()) {
        g.setColour(theme.colors.textDisabled);
        g.setFont(12.0f);
        g.drawText("No instrument loaded", bounds, juce::Justification::centred, false);
        return;
    }

    auto inner = bounds.reduced(2.0f);
    const float keysHeight = inner.getHeight() * kKeysHeightShare;
    const auto keysArea = inner.removeFromBottom(keysHeight);
    const auto bandsArea = inner.withTrimmedBottom(keysHeight + kBandGap);
    const float halfBand = bandsArea.getHeight() * 0.5f;
    const float keyWidth = inner.getWidth() / (float)(span_.getEnd() - span_.getStart() + 1);
    for (const auto& band : bands_) {
        const float x = keyX(std::max(band.lowKey, span_.getStart()), inner);
        const float right = keyX(std::min(band.highKey, span_.getEnd()), inner) + keyWidth;
        const float width = std::max(1.0f, right - x - kBandGap);
        const auto loudColour = theme.colors.accent;
        const auto softColour = theme.colors.accent.darker(kSoftAccentDarken);
        if (band.single) {
            g.setColour(loudColour);
            g.fillRect(x, bandsArea.getY(), width, bandsArea.getHeight());
        } else {
            g.setColour(band.loud ? loudColour : softColour);
            g.fillRect(x, band.loud ? bandsArea.getY() : bandsArea.getY() + halfBand + kBandGap * 0.5f, width,
                       halfBand - kBandGap * 0.5f);
        }
    }
    paintKeys(g, keysArea, theme);
}

} // namespace synth::ui
