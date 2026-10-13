#include "BuildStepsCard.h"
#include "AIChatComponent.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth {

using songsteps::StepStatus;
using synth::ui::StepIconState;

namespace {

constexpr int kRowHeight = 24;
constexpr int kBarHeight = 5;
constexpr int kNowIconSize = 34;
constexpr float kHeightMs = 300.0f;
constexpr float kRowBaseDelayMs = 60.0f;
constexpr float kRowFadeMs = 200.0f;
constexpr float kRowSlidePx = 4.0f;

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

int wrapped(const juce::Font& font, const juce::String& text, int width) {
    return AIChatComponent::computeWrappedTextHeight(font, text, width);
}

void styleLabel(juce::Label& label, float size, bool bold) {
    label.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setMinimumHorizontalScale(1.0f);
    label.setJustificationType(juce::Justification::topLeft);
    label.setInterceptsMouseClicks(false, false);
}

StepIconState iconStateFor(StepStatus status) {
    switch (status) {
    case StepStatus::waiting:
        return StepIconState::waiting;
    case StepStatus::running:
        return StepIconState::running;
    case StepStatus::fixing:
        return StepIconState::fixing;
    case StepStatus::done:
        return StepIconState::done;
    case StepStatus::skipped:
        return StepIconState::skipped;
    }
    return StepIconState::waiting;
}

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

} // namespace

// -- one row of the list
// -------------------------------------------------------------------------------------------------

class BuildStepsCard::Row
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    Row() {
        addAndMakeVisible(icon);
        setWantsKeyboardFocus(false);
        setAccessible(true);
    }

    void update(const songsteps::StepState& state) {
        step = state;
        icon.setKind(step.info.kind);
        icon.setState(iconStateFor(step.status));
        const juce::String words = statusWords(step.status);
        setTitle(step.info.label + ", " + words);
        const juce::String tip = step.summary.isNotEmpty() ? step.summary : step.info.label + ", " + words;
        setDescription(tip);
        setTooltip(tip);
        repaint();
    }

    const songsteps::StepState& state() const { return step; }
    synth::ui::StepIconView& iconView() { return icon; }

    void resized() override { icon.setBounds(0, 2, kRowHeight - 4, kRowHeight - 4); }

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        const bool running = step.status == StepStatus::running;
        juce::Colour text = c.textMuted;
        if (running)
            text = c.textPrimary;
        if (step.status == StepStatus::fixing)
            text = c.warning;
        if (step.status == StepStatus::waiting)
            text = c.textMuted.withAlpha(0.55f);
        icon.setColour(step.status == StepStatus::done      ? c.success
                       : step.status == StepStatus::fixing  ? c.warning
                       : step.status == StepStatus::running ? c.hueAmber
                                                            : c.textMuted);
        auto area = getLocalBounds().withTrimmedLeft(kRowHeight + 2);
        g.setColour(text);
        g.setFont(juce::Font(13.0f, running ? juce::Font::bold : juce::Font::plain));
        const juce::String time = step.ms >= 0 ? formatClock((juce::uint32)step.ms) : juce::String();
        const int timeWidth = time.isEmpty() ? 0 : 36;
        g.drawText(step.info.label, area.withTrimmedRight(timeWidth), juce::Justification::centredLeft, true);
        if (timeWidth > 0) {
            g.setColour(c.textMuted);
            g.setFont(juce::Font(11.0f));
            g.drawText(time, area.removeFromRight(timeWidth), juce::Justification::centredRight);
        }
        synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
    }

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey) {
            moveKeyboardFocusToSibling(key == juce::KeyPress::downKey);
            return true;
        }
        return false;
    }

private:
    songsteps::StepState step;
    synth::ui::StepIconView icon;
};

// -- the card
// ------------------------------------------------------------------------------------------------------------

BuildStepsCard::BuildStepsCard(SongSession& sessionIn, Actions actionsIn)
    : SongCardBase(sessionIn.fadeInPending)
    , session(sessionIn)
    , actions(std::move(actionsIn)) {
    session.fadeInPending = false;
    setTitle("Song build");

    for (auto* l : {&headerLabel, &clockLabel, &nowLabel, &subLabel, &summaryLabel, &outcomeLabel})
        addAndMakeVisible(*l);
    styleLabel(headerLabel, 12.0f, true);
    styleLabel(clockLabel, 12.0f, false);
    clockLabel.setJustificationType(juce::Justification::topRight);
    styleLabel(nowLabel, 14.0f, true);
    styleLabel(subLabel, 12.0f, false);
    styleLabel(summaryLabel, 12.0f, false);
    styleLabel(outcomeLabel, 12.0f, false);
    headerLabel.setAccessible(false);
    clockLabel.setAccessible(false);
    outcomeLabel.setVisible(false);
    nowLabel.setTitle("Current step");
    subLabel.setTitle("Progress");
    summaryLabel.setTitle("Steps done");
    addAndMakeVisible(nowIconView);

    addAndMakeVisible(toggleButton);
    toggleButton.setTitle("Show all steps");
    toggleButton.setTooltip("Show every step of this build");
    toggleButton.onClick = [this] {
        setListOpen(openTargetMs <= 0.0f, true);
        if (actions.onListToggled)
            actions.onListToggled(openTargetMs > 0.0f);
    };
    addAndMakeVisible(cancelButton);
    cancelButton.setTitle("Cancel song build");
    cancelButton.setTooltip("Stop building; the uses come back");
    cancelButton.onClick = [this] {
        if (actions.onCancel)
            actions.onCancel();
    };
    cancelFade_.onFrame = [this] { layoutChanged(); };

    syncRows();
    setListOpen(session.listOpen, false);
    progressShown = session.model.progress();
    refresh();
}

BuildStepsCard::~BuildStepsCard() {
    listAnim.stop(vblank);
    barAnim.stop(vblank);
}

juce::Component* BuildStepsCard::row(int i) const { return rows[(size_t)i].get(); }
StepIconState BuildStepsCard::rowIconState(int i) const { return rows[(size_t)i]->iconView().getState(); }
juce::String BuildStepsCard::nowText() const { return nowLabel.getText(); }
juce::String BuildStepsCard::progressText() const { return subLabel.getText(); }
juce::String BuildStepsCard::headerText() const { return headerLabel.getText(); }

void BuildStepsCard::setElapsedMs(juce::uint32 ms) {
    const auto text = formatClock(ms);
    if (clockLabel.getText() != text)
        clockLabel.setText(text, juce::dontSendNotification);
}

void BuildStepsCard::syncRows() {
    const auto& steps = session.model.steps();
    while (rows.size() < steps.size()) {
        rows.push_back(std::make_unique<Row>());
        addChildComponent(*rows.back());
    }
    for (size_t i = 0; i < steps.size(); ++i)
        rows[i]->update(steps[i]);
}

juce::String BuildStepsCard::stateLine() const {
    const auto& m = session.model;
    const juce::String count = "step " + juce::String(m.currentNumber()) + " of " + juce::String(m.total());
    if (m.isFixing())
        return "Checking the result and trying again" + dot() + count;
    return m.total() > 0 ? count : juce::String();
}

void BuildStepsCard::refresh() {
    syncRows();
    const auto& m = session.model;
    const auto phase = session.phase;
    const bool building = phase == SongSession::Phase::building;
    auto& icon = nowIconView;
    const auto& c = colours();

    if (building) {
        headerLabel.setText("Building your song", juce::dontSendNotification);
        const int cur = m.currentIndex();
        if (cur >= 0) {
            const auto& step = m.steps()[(size_t)cur];
            nowLabel.setText(step.info.label, juce::dontSendNotification);
            icon.setKind(m.isFixing() ? songsteps::StepKind::fix : step.info.kind);
            icon.setColour(m.isFixing() ? c.warning : c.hueAmber);
            icon.setState(m.isFixing() ? StepIconState::fixing : StepIconState::running);
        } else {
            nowLabel.setText("Getting ready", juce::dontSendNotification);
            icon.setKind(songsteps::StepKind::plan);
            icon.setColour(c.hueAmber);
            icon.setState(StepIconState::running);
        }
        subLabel.setText(stateLine(), juce::dontSendNotification);
    } else if (phase == SongSession::Phase::built) {
        headerLabel.setText("Your song is ready", juce::dontSendNotification);
        nowLabel.setText(m.resultLine(), juce::dontSendNotification);
        subLabel.setText("Applied as one undo step.", juce::dontSendNotification);
        icon.setColour(c.success);
        icon.setState(StepIconState::done);
    } else {
        const bool cancelled = session.cancelled;
        headerLabel.setText(cancelled ? "Build cancelled" : "Build stopped", juce::dontSendNotification);
        nowLabel.setText(cancelled ? juce::String("You cancelled this build.") : session.failureText,
                         juce::dontSendNotification);
        const int units = session.reply.units;
        subLabel.setText(m.wasRefunded() || cancelled
                             ? "Your " + juce::String(units) + (units == 1 ? " use was" : " uses were") + " returned."
                             : juce::String(),
                         juce::dontSendNotification);
        icon.setKind(songsteps::StepKind::fix);
        icon.setColour(c.warning);
        icon.setState(StepIconState::still);
    }
    nowLabel.setColour(juce::Label::textColourId, c.textPrimary);
    headerLabel.setColour(juce::Label::textColourId, c.textMuted);
    clockLabel.setColour(juce::Label::textColourId, c.textMuted);
    subLabel.setColour(juce::Label::textColourId, c.textMuted);
    summaryLabel.setColour(juce::Label::textColourId, c.textMuted);

    const int done = m.finishedCount();
    summaryLabel.setText(m.total() == 0 ? juce::String()
                                        : juce::String(done) + (done == 1 ? " step done" : " steps done"),
                         juce::dontSendNotification);
    toggleButton.setVisible(m.total() > 0);
    cancelFade_.setShown(building);
    setElapsedMs(session.elapsedMs(juce::Time::getMillisecondCounter()));

    const float target = m.progress();
    if (std::abs(target - progressShown) > 0.001f) {
        const float from = progressShown;
        barAnim.start(
            vblank, synth::ui::motionMs(200.0, 80.0), synth::ui::easeOutCubic,
            [this, from, target](float t) {
                progressShown = from + (target - from) * t;
                repaint();
            },
            [this, target] {
                progressShown = target;
                repaint();
            });
    }
    layoutChanged();
}

void BuildStepsCard::setListOpen(bool open, bool animate) {
    const int n = (int)rows.size();
    const float step = n > 0 ? juce::jmin(35.0f, 150.0f / (float)n) : 0.0f;
    const float total = juce::jmax(kHeightMs, kRowBaseDelayMs + (float)juce::jmax(0, n - 1) * step + kRowFadeMs);
    openTargetMs = open ? total : 0.0f;
    session.listOpen = open;
    toggleButton.setButtonText(open ? "Hide steps" : "Show all steps");
    toggleButton.setTitle(open ? "Hide steps" : "Show all steps");
    toggleButton.setTooltip(open ? "Hide the list of steps" : "Show every step of this build");
    chevron.setOpen(open);

    const bool canAnimate =
        animate && synth::ui::FadeVisibility::canAnimateIn(this) && !synth::ui::prefersReducedMotion();
    if (!canAnimate) {
        listAnim.stop(vblank);
        openMs = openTargetMs;
        layoutChanged();
        return;
    }
    const float from = openMs;
    const float to = openTargetMs;
    listAnim.start(
        vblank, juce::jmax(1.0, (double)std::abs(to - from)), [](float t) { return t; },
        [this, from, to](float t) {
            openMs = from + (to - from) * t;
            layoutChanged();
        },
        [this, to] {
            openMs = to;
            layoutChanged();
        });
}

float BuildStepsCard::heightReveal() const {
    return synth::ui::easeOutCubic(juce::jlimit(0.0f, 1.0f, openMs / kHeightMs));
}

float BuildStepsCard::rowReveal(int index) const {
    const int n = juce::jmax(1, (int)rows.size());
    const float step = juce::jmin(35.0f, 150.0f / (float)n);
    return juce::jlimit(0.0f, 1.0f, (openMs - (kRowBaseDelayMs + (float)index * step)) / kRowFadeMs);
}

int BuildStepsCard::layoutFor(int width, bool apply) {
    const int inner = juce::jmax(60, width - kPadding * 2);
    int y = kPadding;
    if (apply) {
        headerLabel.setBounds(kPadding, y, inner - 44, 16);
        clockLabel.setBounds(kPadding + inner - 44, y, 44, 16);
    }
    y += 16 + kGap;

    const int textX = kNowIconSize + kGap;
    const int textW = inner - textX;
    const int nowH = wrapped(nowLabel.getFont(), nowLabel.getText(), textW);
    const int subH = subLabel.getText().isEmpty() ? 0 : wrapped(subLabel.getFont(), subLabel.getText(), textW);
    const int nowBlock = juce::jmax(kNowIconSize, nowH + (subH > 0 ? subH + 2 : 0));
    if (apply) {
        nowIconView.setBounds(kPadding, y + (nowBlock - kNowIconSize) / 2, kNowIconSize, kNowIconSize);
        nowLabel.setBounds(kPadding + textX, y + (nowBlock - nowH - (subH > 0 ? subH + 2 : 0)) / 2, textW, nowH);
        subLabel.setBounds(kPadding + textX, nowLabel.getBottom() + 2, textW, subH);
    }
    y += nowBlock + kGap;

    if (apply)
        barBounds = {kPadding, y, inner, kBarHeight};
    y += kBarHeight + kGap;

    const int listFull = (int)rows.size() * kRowHeight;
    const int listNow = juce::roundToInt((float)listFull * heightReveal());
    if (apply) {
        for (size_t i = 0; i < rows.size(); ++i) {
            const float reveal = rowReveal((int)i);
            auto& r = *rows[i];
            r.setVisible(reveal > 0.0f);
            r.setAlpha(reveal);
            r.setWantsKeyboardFocus(openTargetMs > 0.0f);
            r.setBounds(kPadding, y + (int)i * kRowHeight - juce::roundToInt(kRowSlidePx * (1.0f - reveal)), inner,
                        kRowHeight);
        }
    }
    y += listNow;
    if (listNow > 0)
        y += 2;

    const bool hasToggle = !session.model.steps().empty();
    if (apply) {
        summaryLabel.setBounds(kPadding, y + 3, inner / 2, 16);
        toggleButton.setBounds(kPadding + inner / 2, y, inner - inner / 2 - 14, 20);
        chevronBounds = juce::Rectangle<int>(kPadding + inner - 12, y + 4, 12, 12);
    }
    if (hasToggle)
        y += 20 + kGap;

    const int cancelH = juce::roundToInt((float)(28 + kGap) * cancelFade_.progress());
    if (apply)
        cancelButton.setBounds(kPadding, y, inner, 28);
    y += cancelH;
    return (cancelH > 0 || hasToggle ? y - kGap : y) + kPadding;
}

void BuildStepsCard::paint(juce::Graphics& g) {
    SongCardBase::paint(g);
    const auto& c = colours();
    const auto bar = barBounds.toFloat();
    g.setColour(c.border);
    g.fillRoundedRectangle(bar, bar.getHeight() / 2.0f);
    g.setColour(session.phase == SongSession::Phase::failed ? c.warning : c.accent);
    g.fillRoundedRectangle(bar.withWidth(juce::jmax(bar.getHeight(), bar.getWidth() * progressShown)),
                           bar.getHeight() / 2.0f);
    if (toggleButton.isVisible())
        synth::theme::paintDisclosureChevron(g, chevronBounds.toFloat(), chevron.openness(),
                                             synth::theme::themeOf(*this), false);
}

} // namespace synth
