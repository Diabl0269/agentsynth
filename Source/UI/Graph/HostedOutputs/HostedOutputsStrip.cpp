// HostedOutputsStrip.cpp -- see HostedOutputsStrip.h. The aux jacks of a multi-output plugin sit under the main jacks:
// first the fold line (row m), then, while open, one row per jack. Each aux jack carries its own row and alpha, so a
// jack that is added, removed, folded or moved by a join glides and fades instead of popping, and a cable end that
// asks the card for it follows.
#include "HostedOutputsStrip.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kRowStep = 20;    // one jack row, as the card's gutter draws it
constexpr int kLineWidth = 140; // the fold line's box, ending 20 px in from the card's right edge
constexpr int kLineHeight = 18;
constexpr int kGutterRight = 20; // a jack label ends here
constexpr float kLineFontHeight = 12.0f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

// ---- OutputFoldLine --------------------------------------------------------------------------------------------

OutputFoldLine::OutputFoldLine()
    : juce::Button("outputsFoldLine") {
    setComponentID("outputsFoldLine");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void OutputFoldLine::setState(int count, float open) {
    if (count == count_ && std::abs(open - open_) < 0.001f)
        return;
    count_ = count;
    open_ = open;
    repaint();
}

// Return and Space toggle at once, so a keyboard toggle and its re-layout land in the key's own message-loop turn.
bool OutputFoldLine::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
        if (onToggle)
            onToggle();
        return true;
    }
    return juce::Button::keyPressed(key);
}

void OutputFoldLine::clicked() {
    if (onToggle)
        onToggle();
}

void OutputFoldLine::focusGained(FocusChangeType cause) {
    juce::Button::focusGained(cause);
    repaint();
}

void OutputFoldLine::focusLost(FocusChangeType cause) {
    juce::Button::focusLost(cause);
    repaint();
}

// "+ N more outputs", the plus losing its vertical bar as the line opens, so the glyph turns into a minus with the
// jacks instead of swapping. Right-aligned, where the jacks' labels end.
void OutputFoldLine::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto area = getLocalBounds().toFloat();
    const juce::String text = juce::String(count_) + (count_ == 1 ? " more output" : " more outputs");
    const auto font = synth::theme::AppLookAndFeel::uiFont(kLineFontHeight);
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text);
    constexpr float glyph = 8.0f;
    constexpr float gap = 6.0f;

    const auto ink = (highlighted || down || hasKeyboardFocus(false)) ? colours.textPrimary : colours.textMuted;
    g.setColour(ink);
    g.setFont(font);
    const float textLeft = area.getRight() - 2.0f - textWidth;
    g.drawText(text, juce::Rectangle<float>(textLeft, area.getY(), textWidth + 2.0f, area.getHeight()),
               juce::Justification::centredLeft, false);

    const float cx = textLeft - gap - glyph * 0.5f;
    const float cy = area.getCentreY();
    g.drawLine(cx - glyph * 0.5f, cy, cx + glyph * 0.5f, cy, 1.5f);
    const float vertical = 1.0f - open_;
    if (vertical > 0.01f) {
        g.setColour(ink.withMultipliedAlpha(vertical));
        g.drawLine(cx, cy - glyph * 0.5f * vertical, cx, cy + glyph * 0.5f * vertical, 1.5f);
    }
    synth::ui::paintFocusRing(g, area.reduced(0.0f, 1.0f), *this, 3.0f);
}

// ---- HostedOutputsStrip ----------------------------------------------------------------------------------------

HostedOutputsStrip::HostedOutputsStrip(juce::Component& card, HostedPluginModule& module, Callbacks callbacks)
    : card_(card)
    , module_(module)
    , cb_(std::move(callbacks)) {
    foldLine_.onToggle = [this] {
        if (cb_.toggleFold)
            cb_.toggleFold();
    };
    foldLine_.addMouseListener(&card_, false); // a right click on it reaches the card's menu, like the header buttons
    card_.addChildComponent(foldLine_);

    button_ = std::make_unique<juce::DrawableButton>("Outputs", juce::DrawableButton::ImageFitted);
    button_->setComponentID("outputsButton");
    button_->setTitle("Outputs");
    button_->setTooltip("Outputs - choose which of this plugin's outputs show on the card");
    button_->onClick = [this] {
        if (cb_.openPanel)
            cb_.openPanel();
    };
    button_->addMouseListener(&card_, false);
    card_.addChildComponent(*button_);
    snapToModel();
}

HostedOutputsStrip::~HostedOutputsStrip() {
    driver_.stop();
    card_.removeChildComponent(&foldLine_);
    if (button_ != nullptr)
        card_.removeChildComponent(button_.get());
}

int HostedOutputsStrip::rowY(float row) const {
    const int portOffset = module_.producesMidi() ? 20 : 0; // clears MIDI Out, as the card's gutter does
    return juce::roundToInt((float)(ModuleComponent::kPortGutterHeaderHeight + portOffset + 20) +
                            row * (float)kRowStep);
}

std::vector<HostedOutputsStrip::Entry> HostedOutputsStrip::targetEntries() const {
    std::vector<Entry> list;
    const int main = module_.getMainOutputJackCount();
    const int total = module_.getVisibleOutputPortCount();
    const bool folded = module_.getOutputsFolded();
    if (module_.getShownAuxOutputBusCount() == 0)
        return list;
    for (int jack = main; jack < total; ++jack) {
        Entry e;
        const auto targets = module_.getJackTargets(jack, /*isInput=*/false);
        e.raw = targets.empty() ? jack : targets.front().rawHeadChannel;
        e.jack = jack;
        e.label = module_.getOutputPortLabel(jack);
        e.row1 = folded ? (float)main : (float)(jack + 1); // the fold line takes row `main`, open jacks follow it
        e.alpha1 = folded ? 0.0f : 1.0f;
        list.push_back(e);
    }
    return list;
}

// Merges the module's current output set into what is drawn: a jack still there keeps its drawn row and alpha and
// glides to its new ones, a new one fades in where it will sit, one that went hidden stays where it was and fades out.
void HostedOutputsStrip::applyTargets(bool animate) {
    auto next = targetEntries();
    for (auto& n : next) {
        const auto old = std::find_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return e.jack >= 0 && e.raw == n.raw; });
        n.row0 = old != entries_.end() ? old->row : n.row1;
        n.alpha0 = old != entries_.end() ? old->alpha : 0.0f;
        n.row = n.row0;
        n.alpha = n.alpha0;
    }
    for (const auto& old : entries_) {
        const bool kept = std::any_of(next.begin(), next.end(), [&](const Entry& n) { return n.raw == old.raw; });
        if (kept || old.alpha <= 0.0f)
            continue;
        Entry ghost = old;
        ghost.jack = -1;
        ghost.row0 = ghost.row1 = old.row;
        ghost.alpha0 = old.alpha;
        ghost.alpha1 = 0.0f;
        next.push_back(ghost);
    }
    entries_ = std::move(next);

    const int main = module_.getMainOutputJackCount();
    const int total = module_.getVisibleOutputPortCount();
    const bool folded = module_.getOutputsFolded();
    const int aux = module_.getShownAuxOutputBusCount();
    block0_ = block_;
    open0_ = open_;
    lineAlpha0_ = lineAlpha_;
    block1_ = aux > 0 ? 1.0f + (folded ? 0.0f : (float)(total - main)) : 0.0f;
    open1_ = folded ? 0.0f : 1.0f;
    lineAlpha1_ = aux > 0 ? 1.0f : 0.0f;
    if (aux > 0)
        lineCount_ = aux;

    if (!animate) {
        land();
        return;
    }
    refreshLine();
}

void HostedOutputsStrip::snapToModel() {
    driver_.stop();
    entries_.clear();
    block_ = open_ = lineAlpha_ = 0.0f;
    applyTargets(/*animate*/ false);
}

void HostedOutputsStrip::modelChanged(const std::function<void()>& relayoutAtFinal) {
    const bool animate = FadeVisibility::canAnimateIn(&card_);
    driver_.stop();
    applyTargets(animate);
    if (!animate) {
        if (relayoutAtFinal)
            relayoutAtFinal();
        return;
    }
    const bool growing = block1_ > block_ || open1_ > open_;
    if (growing) { // the neighbours make room once, for the final footprint
        settling_ = true;
        if (relayoutAtFinal)
            relayoutAtFinal();
        settling_ = false;
    } else if (relayoutAtFinal) {
        relayoutAtFinal();
    }

    const double ms = synth::ui::motionMs(growing ? FadeVisibility::kFadeInMs : FadeVisibility::kFadeOutMs,
                                          FadeVisibility::kReducedMs);
    if (ms <= 0.0) {
        land();
        return;
    }
    if (updater_ == nullptr)
        updater_ = std::make_unique<juce::VBlankAnimatorUpdater>(&card_);
    const bool reduced = synth::ui::prefersReducedMotion();
    driver_.start(
        *updater_, ms,
        [growing, reduced](float t) {
            return reduced ? t : (growing ? synth::ui::easeOutCubic(t) : synth::ui::easeInCubic(t));
        },
        [this](float e) { frame(e); },
        [this] {
            land();
            if (cb_.relayout)
                cb_.relayout();
            if (cb_.makeRoom)
                cb_.makeRoom();
        });
}

void HostedOutputsStrip::frame(float e) {
    for (auto& entry : entries_) {
        entry.row = lerp(entry.row0, entry.row1, e);
        entry.alpha = lerp(entry.alpha0, entry.alpha1, e);
    }
    block_ = lerp(block0_, block1_, e);
    open_ = lerp(open0_, open1_, e);
    lineAlpha_ = lerp(lineAlpha0_, lineAlpha1_, e);
    refreshLine();
    if (cb_.relayout)
        cb_.relayout();
}

// Ends the glide: every jack on its target, the ghosts gone.
void HostedOutputsStrip::land() {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.jack < 0; }),
                   entries_.end());
    for (auto& entry : entries_) {
        entry.row = entry.row0 = entry.row1;
        entry.alpha = entry.alpha0 = entry.alpha1;
    }
    block_ = block0_ = block1_;
    open_ = open0_ = open1_;
    lineAlpha_ = lineAlpha0_ = lineAlpha1_;
    driver_.stop();
    refreshLine();
}

void HostedOutputsStrip::advanceForTest(float e) {
    if (e >= 1.0f) {
        land();
        if (cb_.relayout)
            cb_.relayout();
        if (cb_.makeRoom)
            cb_.makeRoom();
        return;
    }
    frame(e);
}

void HostedOutputsStrip::refreshLine() {
    foldLine_.setState(lineCount_, open_);
    const bool folded = module_.getOutputsFolded();
    const juce::String noun = lineCount_ == 1 ? "output" : "outputs";
    foldLine_.setTitle(juce::String(lineCount_) + " more " + noun + (folded ? ", folded" : ", shown"));
    foldLine_.setTooltip(folded ? "Show the " + juce::String(lineCount_) + " more " + noun +
                                      " this plugin has switched on (Return or Space)"
                                : "Fold the " + juce::String(lineCount_) + " more " + noun +
                                      " under this line, their cables leave from it (Return or Space)");
}

// ---- geometry --------------------------------------------------------------------------------------------------

bool HostedOutputsStrip::isActive() const { return blockNow() > 0.0f; }

std::optional<juce::Point<int>> HostedOutputsStrip::jackCentre(int jack) const {
    const int main = module_.getMainOutputJackCount();
    if (jack < main || entries_.empty())
        return std::nullopt;
    const int total = module_.getVisibleOutputPortCount();
    const int x = card_.getWidth() - 10;
    if (jack < total) {
        for (const auto& e : entries_)
            if (e.jack == jack)
                return juce::Point<int>(x, rowY(rowNow(e)));
        return std::nullopt;
    }
    int ordinal = jack - total;
    for (const auto& e : entries_)
        if (e.jack < 0 && ordinal-- == 0)
            return juce::Point<int>(x, rowY(rowNow(e)));
    return std::nullopt;
}

float HostedOutputsStrip::jackAlpha(int jack) const {
    if (jack < module_.getMainOutputJackCount())
        return 1.0f;
    const int total = module_.getVisibleOutputPortCount();
    int ordinal = jack - total;
    for (const auto& e : entries_) {
        if (jack < total ? e.jack == jack : (e.jack < 0 && ordinal-- == 0))
            return alphaNow(e);
    }
    return jack < total ? 1.0f : 0.0f;
}

int HostedOutputsStrip::ghostJackCount() const {
    return (int)std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.jack < 0; });
}

juce::String HostedOutputsStrip::ghostLabel(int jack) const {
    int ordinal = jack - module_.getVisibleOutputPortCount();
    for (const auto& e : entries_)
        if (e.jack < 0 && ordinal-- == 0)
            return e.label;
    return {};
}

bool HostedOutputsStrip::jackHittable(int jack) const {
    if (jack < module_.getMainOutputJackCount())
        return true;
    for (const auto& e : entries_)
        if (e.jack == jack)
            return !driver_.isRunning() && e.alpha1 >= 1.0f;
    return jack < module_.getVisibleOutputPortCount() ? false : false;
}

int HostedOutputsStrip::bottomCentreY() const {
    if (!isActive())
        return -1;
    return rowY((float)module_.getMainOutputJackCount() - 1.0f + blockNow());
}

void HostedOutputsStrip::paintFoldDot(juce::Graphics& g, juce::Colour colour) const {
    const float alpha = lineAlphaNow() * (1.0f - openNow());
    if (alpha <= 0.0f)
        return;
    const float x = (float)card_.getWidth() - 10.0f;
    const float y = (float)rowY((float)module_.getMainOutputJackCount());
    g.setColour(colour.withMultipliedAlpha(alpha));
    g.fillEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f);
}

// ---- children --------------------------------------------------------------------------------------------------

void HostedOutputsStrip::layout() {
    const bool shows = lineAlphaNow() > 0.0f || lineAlpha1_ > 0.0f;
    foldLine_.setVisible(shows);
    if (shows) {
        const int y = rowY((float)module_.getMainOutputJackCount());
        foldLine_.setBounds(card_.getWidth() - kGutterRight - kLineWidth, y - kLineHeight / 2, kLineWidth, kLineHeight);
        foldLine_.setAlpha(lineAlphaNow());
        foldLine_.setInterceptsMouseClicks(lineAlpha1_ > 0.0f, false);
    }
    button_->setBounds(card_.getWidth() - 98, 2, 22, 20);
}

void HostedOutputsStrip::syncButton() { button_->setVisible(module_.hasMultipleOutputBuses()); }

void HostedOutputsStrip::setIcon(const juce::Drawable* icon) {
    if (icon != nullptr)
        button_->setImages(icon);
}

} // namespace synth::ui
