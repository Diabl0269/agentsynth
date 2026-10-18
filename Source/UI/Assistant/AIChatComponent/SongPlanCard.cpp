#include "SongPlanCard.h"
#include "AIChatComponent.h"
#include "SoundPacks/CoreSounds.h"
#include "UI/Layout/FocusRing.h"
#include <cmath>
#include <numeric>

namespace synth {

namespace {

constexpr int kStripHeight = 26;
constexpr int kRowHeight = 24;
constexpr int kButtonHeight = 30;
constexpr int kLinkHeight = 20;
constexpr int kChipHeight = 20;
constexpr int kChipGap = 6;
constexpr int kBarHeight = 6;

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

juce::String usesText(int units) { return juce::String(units) + (units == 1 ? " use" : " uses"); }

int wrapped(const juce::Font& font, const juce::String& text, int width) {
    return AIChatComponent::computeWrappedTextHeight(font, text, width);
}

void styleLabel(juce::Label& label, float size, bool bold, bool accessible = true) {
    label.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setMinimumHorizontalScale(1.0f);
    label.setJustificationType(juce::Justification::topLeft);
    label.setAccessible(accessible);
    label.setInterceptsMouseClicks(false, false);
}

} // namespace

// -- one section of the strip --------------------------------------------------------------------------------------

class SongPlanCard::Segment
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    Segment(const songsteps::PlanSection& section, const juce::String& tip)
        : name(section.name)
        , energy(section.energy) {
        setTitle(section.name + ", " + juce::String(section.bars) + " bars");
        setDescription(tip);
        setTooltip(tip);
        setWantsKeyboardFocus(true);
    }

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        const juce::Colour fills[] = {c.hueViolet.withAlpha(0.55f), c.accent.withAlpha(0.45f),
                                      c.hueAmber.withAlpha(0.8f), c.hueGreen.withAlpha(0.85f), c.accent};
        const auto fill = fills[juce::jlimit(1, 5, energy) - 1];
        g.setColour(fill);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(energy >= 3 ? c.iconInk : c.textPrimary);
        const juce::Font font(10.0f);
        g.setFont(font);
        g.drawText(shownLabel(), getLocalBounds(), juce::Justification::centred);
        synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
    }

    // The full name when it fits, else its first four, else three letters, else nothing (the tooltip and the
    // accessible name carry the full name and bars). Never a single letter, which reads as noise.
    juce::String shownLabel() const {
        const juce::Font font(10.0f);
        const float room = (float)getWidth() - 6.0f;
        for (const auto& candidate : {name, name.substring(0, 4), name.substring(0, 3)})
            if (candidate.length() >= 3 && juce::GlyphArrangement::getStringWidth(font, candidate) <= room)
                return candidate;
        return {};
    }

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::leftKey) {
            moveKeyboardFocusToSibling(key == juce::KeyPress::rightKey);
            return true;
        }
        return false;
    }

private:
    juce::String name;
    int energy;
};

class SongPlanCard::Strip : public juce::Component {
public:
    Strip(const songsteps::SongPlan& plan) {
        setTitle("Sections");
        setDescription("The sections of the song, in order");
        for (const auto& section : plan.sections) {
            juce::String tip = section.name + dot() + juce::String(section.bars) + " bars";
            if (section.description.isNotEmpty())
                tip << dot() << section.description;
            segments.push_back(std::make_unique<Segment>(section, tip));
            bars.push_back(juce::jmax(1, section.bars));
            addAndMakeVisible(*segments.back());
        }
    }

    void resized() override {
        const int gap = 2;
        const int n = (int)segments.size();
        const int total = std::accumulate(bars.begin(), bars.end(), 0);
        const float usable = (float)juce::jmax(1, getWidth() - gap * (n - 1));
        float x = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float w = usable * (float)bars[(size_t)i] / (float)total;
            const int left = juce::roundToInt(x);
            const int right = i == n - 1 ? getWidth() : juce::roundToInt(x + w);
            segments[(size_t)i]->setBounds(left, 0, juce::jmax(2, right - left), getHeight());
            x += w + (float)gap;
        }
    }

    std::vector<std::unique_ptr<Segment>> segments;

private:
    std::vector<int> bars;
};

// -- the instrument chips ----------------------------------------------------------------------------------------

class SongPlanCard::Chips : public juce::Component {
public:
    explicit Chips(const songsteps::SongPlan& plan) {
        for (const auto& track : plan.tracks)
            names.add(track.name);
        setTitle("Instruments");
        setDescription(names.joinIntoString(", "));
    }

    int heightFor(int width) const {
        int rows = 1, x = 0;
        for (const auto& name : names) {
            const int w = chipWidth(name);
            if (x > 0 && x + w > width) {
                ++rows;
                x = 0;
            }
            x += w + kChipGap;
        }
        return rows * kChipHeight + (rows - 1) * 4;
    }

    void paint(juce::Graphics& g) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        int x = 0, y = 0;
        g.setFont(juce::Font(12.0f));
        for (const auto& name : names) {
            const int w = chipWidth(name);
            if (x > 0 && x + w > getWidth()) {
                x = 0;
                y += kChipHeight + 4;
            }
            const juce::Rectangle<float> r((float)x, (float)y, (float)w, (float)kChipHeight);
            g.setColour(c.border);
            g.drawRoundedRectangle(r.reduced(0.5f), (float)kChipHeight / 2.0f, 1.0f);
            g.setColour(c.textPrimary);
            g.drawText(name, r.toNearestInt(), juce::Justification::centred);
            x += w + kChipGap;
        }
    }

private:
    static int chipWidth(const juce::String& name) {
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(juce::Font(12.0f), name)) + 18;
    }
    juce::StringArray names;
};

// -- a row of exclusive choices (Length, Detail)
// ------------------------------------------------------------------------

class SongPlanCard::Choice : public juce::Component {
public:
    Choice(const juce::String& what, const juce::StringArray& labels, int groupId, int selected,
           std::function<void(int)> onPicked)
        : picked(std::move(onPicked)) {
        for (int i = 0; i < labels.size(); ++i) {
            auto button = std::make_unique<Button>(labels[i]);
            button->setRadioGroupId(groupId);
            button->setClickingTogglesState(false);
            button->setTitle(what + ": " + labels[i]);
            button->setTooltip(what + " " + labels[i] +
                               (what == "Length" ? ": plan a song about this long" : ": how much detail"));
            button->onClick = [this, i] {
                if (i != current)
                    picked(i);
            };
            addAndMakeVisible(*button);
            buttons.push_back(std::move(button));
        }
        select(selected);
    }

    void select(int index) {
        current = index;
        for (int i = 0; i < (int)buttons.size(); ++i)
            buttons[(size_t)i]->setToggleState(i == index, juce::dontSendNotification);
    }

    void resized() override {
        const int n = (int)buttons.size();
        const int gap = 2;
        const int w = (getWidth() - gap * (n - 1)) / juce::jmax(1, n);
        for (int i = 0; i < n; ++i)
            buttons[(size_t)i]->setBounds(i * (w + gap), 0, w, getHeight());
    }

    void setEnabledAll(bool enabled) {
        for (auto& b : buttons)
            b->setEnabled(enabled);
    }
    juce::Button* button(int i) { return buttons[(size_t)i].get(); }

private:
    class Button : public juce::Button {
    public:
        explicit Button(const juce::String& text)
            : juce::Button(text) {
            setWantsKeyboardFocus(true);
        }
        void paintButton(juce::Graphics& g, bool highlighted, bool) override {
            const auto& c = synth::theme::themeOf(*this).colors;
            const auto r = getLocalBounds().toFloat();
            const bool on = getToggleState();
            g.setColour(on ? c.accent : (highlighted ? c.surfaceHi.brighter(0.1f) : c.surfaceHi));
            g.fillRoundedRectangle(r, 6.0f);
            g.setColour(on ? c.iconInk : c.textPrimary);
            g.setFont(juce::Font(12.0f, on ? juce::Font::bold : juce::Font::plain));
            g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
            synth::ui::paintFocusRing(g, r, *this, 6.0f);
        }
    };
    std::vector<std::unique_ptr<Button>> buttons;
    std::function<void(int)> picked;
    int current = 0;
};

// -- the card
// ------------------------------------------------------------------------------------------------------------

SongPlanCard::SongPlanCard(SongSession& sessionIn, Actions actionsIn)
    : SongCardBase(sessionIn.fadeInPending)
    , session(sessionIn)
    , actions(std::move(actionsIn))
    , shownPhase(sessionIn.phase) {
    session.fadeInPending = false;
    setTitle("Song plan");

    for (auto* l : {&headerLabel, &titleLabel, &metaLabel, &specialLabel, &noticeLabel, &usesLabel, &detailsLabel,
                    &lengthLabel, &detailLabel})
        addAndMakeVisible(*l);
    addChildComponent(soundsLabel);
    addChildComponent(soundsProgress);
    addChildComponent(continueLink);
    styleLabel(soundsLabel, 12.0f, false);
    soundsLabel.setTitle("Real instruments");
    soundsProgress.setSubject("Downloading the core instruments");
    continueLink.setTitle("Continue with synths");
    continueLink.setTooltip("Build now with synth sounds; they switch to the real instruments once the core "
                            "instruments are installed");
    continueLink.onClick = [this] { continueWithSynths(); };
    styleLabel(headerLabel, 12.0f, true, false);
    headerLabel.setText("Song plan", juce::dontSendNotification);
    styleLabel(titleLabel, 15.0f, true);
    styleLabel(metaLabel, 12.0f, false);
    styleLabel(specialLabel, 12.0f, false);
    styleLabel(noticeLabel, 12.0f, false);
    styleLabel(usesLabel, 11.0f, false);
    styleLabel(detailsLabel, 12.0f, false);
    styleLabel(lengthLabel, 12.0f, false, false);
    styleLabel(detailLabel, 12.0f, false, false);
    lengthLabel.setText("Length", juce::dontSendNotification);
    detailLabel.setText("Detail", juce::dontSendNotification);

    const auto& plan = session.reply.plan;
    strip = std::make_unique<Strip>(plan);
    chips = std::make_unique<Chips>(plan);
    lengthChoice =
        std::make_unique<Choice>("Length", juce::StringArray{"2 min", "3 min", "4+"}, 4101, session.lengthIndex,
                                 [this](int i) { actions.onReplan(i, session.detailIndex); });
    detailChoice =
        std::make_unique<Choice>("Detail", juce::StringArray{"Light", "Full", "Rich"}, 4102, session.detailIndex,
                                 [this](int i) { actions.onReplan(session.lengthIndex, i); });
    for (auto* c :
         std::initializer_list<juce::Component*>{strip.get(), chips.get(), lengthChoice.get(), detailChoice.get()})
        addAndMakeVisible(*c);

    addAndMakeVisible(buildButton);
    buildButton.onClick = [this] {
        if (offerNeeded())
            startDownload();
        else if (actions.onBuild)
            actions.onBuild();
    };
    addAndMakeVisible(cancelLink);
    cancelLink.setTitle("Cancel song plan");
    cancelLink.setTooltip("Dismiss this plan; nothing is spent");
    cancelLink.onClick = [this] {
        if (session.buildAfterDownload) {
            session.buildAfterDownload = false;
            installer_->cancel(synth::soundpacks::kCorePackId);
        }
        if (actions.onCancel)
            actions.onCancel();
    };
    addAndMakeVisible(detailsLink);
    detailsLink.setTitle("Show details");
    detailsLink.setTooltip("Show what happens in each section");
    detailsLink.onClick = [this] {
        detailsOpen = !detailsOpen;
        detailsLink.setButtonText(detailsOpen ? "Hide details" : "Details");
        detailsLink.setTitle(detailsOpen ? "Hide details" : "Show details");
        detailsLink.setTooltip(detailsOpen ? "Hide the details" : "Show what happens in each section");
        detailsFade_.setShown(detailsOpen);
        layoutChanged();
    };
    detailsLabel.setText(detailsText(), juce::dontSendNotification);
    detailsLabel.setVisible(false);
    noticeLabel.setVisible(false);

    controlsFade_ = std::make_unique<synth::ui::FadeVisibility>(std::vector<juce::Component*>{
        strip.get(), chips.get(), &specialLabel, &lengthLabel, lengthChoice.get(), &detailLabel, detailChoice.get(),
        &buildButton, &usesLabel, &cancelLink, &detailsLink});
    controlsFade_->onFrame = [this] { layoutChanged(); };
    detailsFade_.onFrame = [this] { layoutChanged(); };
    noticeFade_.onFrame = [this] { layoutChanged(); };
    soundsFade_.onFrame = [this] { layoutChanged(); };
    soundsBarFade_.onFrame = [this] { layoutChanged(); };
    installer_->addListener(this);
    refresh();
    // Download first was pressed on an earlier card of this session (the chat rebuilt its bubbles) and the pack has
    // landed since: build now, after the chat has taken this card.
    if (session.buildAfterDownload && coreInstalled()) {
        session.buildAfterDownload = false;
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SongPlanCard>(this)] {
            if (safe != nullptr && safe->actions.onBuild)
                safe->actions.onBuild();
        });
    }
}

SongPlanCard::~SongPlanCard() { installer_->removeListener(this); }

juce::String SongPlanCard::detailsText() const {
    const auto& plan = session.reply.plan;
    juce::StringArray lines;
    for (const auto& s : plan.sections)
        lines.add(s.name + ", " + juce::String(s.bars) + " bars" +
                  (s.description.isNotEmpty() ? ": " + s.description : juce::String()));
    if (plan.laneCount > 0)
        lines.add(juce::String(plan.laneCount) + (plan.laneCount == 1 ? " automation lane" : " automation lanes"));
    return lines.joinIntoString("\n");
}

juce::String SongPlanCard::buildButtonText() const { return buildButton.getButtonText(); }
juce::Component* SongPlanCard::sectionSegment(int index) const { return strip->segments[(size_t)index].get(); }
juce::String SongPlanCard::sectionSegmentLabelForTesting(int index) const {
    return strip->segments[(size_t)index]->shownLabel();
}
int SongPlanCard::sectionSegmentCount() const { return (int)strip->segments.size(); }
void SongPlanCard::chooseForTesting(int lengthIndex, int detailIndex) {
    if (lengthIndex != session.lengthIndex)
        lengthChoice->button(lengthIndex)->onClick();
    else if (detailIndex != session.detailIndex)
        detailChoice->button(detailIndex)->onClick();
}
void SongPlanCard::pressBuildForTesting() { buildButton.onClick(); }

void SongPlanCard::refresh() {
    const auto& plan = session.reply.plan;
    titleLabel.setText(plan.title, juce::dontSendNotification);
    metaLabel.setText(songsteps::planMetaText(plan), juce::dontSendNotification);
    metaLabel.setTitle("Length, tempo and sections");
    specialLabel.setText(plan.special, juce::dontSendNotification);
    lengthChoice->select(session.lengthIndex);
    detailChoice->select(session.detailIndex);

    const int units = session.reply.units;
    buildButton.setButtonText("Build song" + dot() + usesText(units));
    buildButton.setTitle("Build song, " + usesText(units));
    buildButton.setTooltip("Spend " + usesText(units) + " to build this song step by step");
    refreshOffer();
    usesLabel.setText(session.usesLeft >= 0 && session.usesLimit > 0
                          ? juce::String(session.usesLeft) + " of " + juce::String(session.usesLimit) + " left"
                          : juce::String(),
                      juce::dontSendNotification);
    applyPhase();
    layoutChanged();
}

// The phase decides what can be pressed and what the line under the buttons says; every change of what is shown goes
// through a fade, and the card's height follows the fades.
void SongPlanCard::applyPhase() {
    const bool replanning = session.phase == SongSession::Phase::replanning;
    const bool dismissed = session.phase == SongSession::Phase::dismissed;
    const bool active = session.phase == SongSession::Phase::ready;
    lengthChoice->setEnabledAll(active);
    detailChoice->setEnabledAll(active);
    buildButton.setEnabled(active && !downloadBusy());

    juce::String notice = session.notice;
    if (replanning)
        notice = "Updating the plan. Nothing is spent.";
    else if (dismissed)
        notice = "Plan dismissed. Nothing was spent.";
    noticeLabel.setText(notice, juce::dontSendNotification);
    noticeLabel.setTitle("Plan status");
    if (notice.isNotEmpty())
        noticeFade_.setShown(true);
    else
        noticeFade_.setShown(false);
    controlsFade_->setShown(!dismissed);
    soundsFade_.setShown(offerNeeded() && !dismissed);
    if (dismissed && detailsOpen) {
        detailsOpen = false;
        detailsFade_.setShown(false);
    }
    shownPhase = session.phase;
}

int SongPlanCard::layoutFor(int width, bool apply) {
    const int inner = juce::jmax(60, width - kPadding * 2);
    int y = kPadding;
    auto place = [&](juce::Component& c, int h, int x = 0, int w = -1) {
        if (apply)
            c.setBounds(kPadding + x, y, w < 0 ? inner : w, h);
        return h;
    };
    auto next = [&](int h) { y += h + kGap; };

    next(place(headerLabel, 16));
    next(place(titleLabel, wrapped(titleLabel.getFont(), titleLabel.getText(), inner)));
    y -= kGap / 2;
    const int metaHeight = place(metaLabel, 16);
    y += metaHeight + kGap;

    // The block the fade hides: everything below the title and meta line.
    const float shown = controlsFade_ ? controlsFade_->progress() : 1.0f;
    int block = 0;
    int by = y;
    auto stack = [&](juce::Component& c, int h, int x = 0, int w = -1) {
        if (apply)
            c.setBounds(kPadding + x, by, w < 0 ? inner : w, h);
        by += h + kGap;
        block += h + kGap;
    };
    stack(*strip, kStripHeight);
    stack(*chips, chips->heightFor(inner));
    if (session.reply.plan.special.isNotEmpty())
        stack(specialLabel, wrapped(specialLabel.getFont(), specialLabel.getText(), inner));
    else if (apply)
        specialLabel.setBounds(0, 0, 0, 0);
    // The offer fades in and out, so each row's height follows its fade.
    {
        const int line = wrapped(soundsLabel.getFont(), soundsLabel.getText(), inner);
        const int lineNow = juce::roundToInt((float)(line + kGap) * soundsFade_.progress());
        if (apply)
            soundsLabel.setBounds(kPadding, by, inner, line);
        by += lineNow;
        block += lineNow;
        const int barNow = juce::roundToInt((float)(kBarHeight + kGap) * soundsBarFade_.progress());
        if (apply)
            soundsProgress.setBounds(kPadding, by, inner, kBarHeight);
        by += barNow;
        block += barNow;
    }
    const int labelW = 52;
    for (auto [label, choice] : {std::pair<juce::Label*, Choice*>{&lengthLabel, lengthChoice.get()},
                                 std::pair<juce::Label*, Choice*>{&detailLabel, detailChoice.get()}}) {
        if (apply) {
            label->setBounds(kPadding, by + 4, labelW, 16);
            choice->setBounds(kPadding + labelW, by, inner - labelW, kRowHeight);
        }
        by += kRowHeight + kGap;
        block += kRowHeight + kGap;
    }
    stack(buildButton, kButtonHeight);
    {
        const int linkNow = juce::roundToInt((float)(kLinkHeight + kGap) * soundsFade_.progress());
        if (apply)
            continueLink.setBounds(kPadding, by, 150, kLinkHeight);
        by += linkNow;
        block += linkNow;
    }
    {
        const int half = inner / 2;
        if (apply) {
            usesLabel.setBounds(kPadding, by, half, kLinkHeight);
            cancelLink.setBounds(kPadding + half, by, inner - half, kLinkHeight);
        }
        by += kLinkHeight + kGap;
        block += kLinkHeight + kGap;
    }
    stack(detailsLink, kLinkHeight, 0, 90);
    const int detailsFull = wrapped(detailsLabel.getFont(), detailsLabel.getText(), inner);
    const int detailsNow = juce::roundToInt((float)detailsFull * detailsFade_.progress());
    if (apply)
        detailsLabel.setBounds(kPadding, by - kGap / 2, inner, detailsFull);
    block += detailsNow;
    y += juce::roundToInt((float)block * shown);

    const int noticeFull = wrapped(noticeLabel.getFont(), noticeLabel.getText(), inner);
    if (apply)
        noticeLabel.setBounds(kPadding, y - kGap / 2, inner, noticeFull);
    y += juce::roundToInt((float)(noticeFull + kGap) * noticeFade_.progress());
    return y - kGap / 2 + kPadding;
}

} // namespace synth
