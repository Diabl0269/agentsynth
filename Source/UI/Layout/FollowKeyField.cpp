#include "UI/Layout/FollowKeyField.h"
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

// U+00B7 MIDDLE DOT between the parts, as the transport bar's design writes it.
const juce::String& dot() {
    static const juce::String text = juce::String::fromUTF8(" \xc2\xb7 ");
    return text;
}

juce::String keyText(const std::optional<MusicalScale>& key, const char* none) {
    return key.has_value() ? keyDisplayName(*key) : juce::String(none);
}

} // namespace

FollowKeyField::FollowKeyField() {
    setTitle("Key");
    onOpen = [this] { openPicker(); };
    refreshLook();
}

FollowKeyField::~FollowKeyField() {
    if (auto* picker = getOpenPicker()) {
        picker->onPick = nullptr;
        picker->onFollow = nullptr;
    }
}

// A follower glows only when the key it plays moved under it (the song key changed, an undo put it back), never on
// its own first look or when the user switches between following and an own key.
void FollowKeyField::setState(bool following, const std::optional<MusicalScale>& songKey,
                              const std::optional<MusicalScale>& ownKey) {
    const bool glow = seeded_ && following && following_ && !sameKey(songKey, songKey_);
    const bool changed =
        !seeded_ || following != following_ || !sameKey(songKey, songKey_) || !sameKey(ownKey, ownKey_);
    seeded_ = true;
    following_ = following;
    songKey_ = songKey;
    ownKey_ = ownKey;
    if (changed)
        refreshLook();
    if (glow)
        startGlow();
}

// The text, its colour, the tooltip and what a screen reader hears all say whether the key is followed or owned.
void FollowKeyField::refreshLook() {
    setKey(following_ ? songKey_ : ownKey_);
    const auto& colours = synth::theme::themeOf(*this).colors;
    juce::String text;
    if (following_) {
        text = "Song key" + dot() + keyText(songKey_, "No key");
        setTooltip("Following the song key. Click to give this its own key.");
        setTitle("Key, following song key");
        setDescription("Following song key, " + keyText(songKey_, "no key"));
        setColour(juce::ComboBox::textColourId, colours.textMuted);
    } else {
        text = keyText(ownKey_, "No scale") + dot() + "own";
        setTooltip("Own key. Choose Follow song key to follow again.");
        setTitle("Key, own key");
        setDescription("Own key, " + keyText(ownKey_, "no scale"));
        setColour(juce::ComboBox::textColourId, colours.textPrimary);
    }
    if (getText() != text)
        setText(text, juce::dontSendNotification);
    repaint();
}

void FollowKeyField::lookAndFeelChanged() {
    KeyField::lookAndFeelChanged();
    refreshLook();
}

KeyPicker* FollowKeyField::getOpenPicker() const noexcept {
    return dynamic_cast<KeyPicker*>(openPicker_.getComponent());
}

// One picker at a time, as on the transport bar. The follow row is ticked while following; otherwise the own key is.
void FollowKeyField::openPicker() {
    if (openPicker_ != nullptr)
        return;
    KeyPicker::Options options;
    options.showFollowRow = true;
    options.following = following_;
    options.followDetail = keyText(songKey_, "No key");
    options.current = following_ ? std::nullopt : ownKey_;
    options.showNoKey = true;
    options.userScales = userScales_;
    options.what = "Key";

    juce::Component::SafePointer<FollowKeyField> self(this);
    auto picker = std::make_unique<KeyPicker>(std::move(options), [self](std::optional<MusicalScale> key) {
        if (self != nullptr && self->onPickOwn)
            self->onPickOwn(key);
    });
    picker->onFollow = [self] {
        if (self != nullptr && self->onFollow)
            self->onFollow();
    };
    openPicker_ = picker.get();
    const auto area = getScreenBounds();
    if (pickerLauncher)
        pickerLauncher(std::move(picker), area);
    else
        juce::CallOutBox::launchAsynchronously(std::move(picker), area, nullptr);
}

// 300 ms from full to nothing; under reduced motion (or Off) the text just changes.
void FollowKeyField::startGlow() {
    if (prefersReducedMotion() || animationsOff())
        return;
    glow_ = 1.0f;
    glowAnim_.start(vblank_, kGlowMs, easeOutCubic, [this](float t) {
        glow_ = 1.0f - t;
        repaint();
    });
    repaint();
}

void FollowKeyField::finishGlowForTest() { glowAnim_.completeForTest(); }

void FollowKeyField::paintOverChildren(juce::Graphics& g) {
    KeyField::paintOverChildren(g);
    if (glow_ <= 0.0f)
        return;
    const auto accent = synth::theme::themeOf(*this).colors.accent;
    const auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(accent.withAlpha(0.25f * glow_));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(accent.withAlpha(glow_));
    g.drawRoundedRectangle(area, 4.0f, 1.5f);
}

} // namespace synth::ui
