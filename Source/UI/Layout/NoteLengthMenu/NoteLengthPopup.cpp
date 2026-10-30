#include "UI/Layout/NoteLengthMenu/NoteLengthPopup.h"

#include "TestMode/AutomationHiding.h"
#include "UI/Layout/ButtonStateEase.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <optional>
#include <set>

// Concern: the note-length popup window -- its letter buttons and chips, their keys, and how the window opens and
// closes. Which names fall in which group is NoteLengthMenuModel; the closed box is NoteLengthBox.

namespace synth::ui {

namespace {

constexpr int kShadow = 10; // transparent margin around the panel, for its shadow
constexpr int kPad = 8;     // panel edge to the first control
constexpr int kGap = 2;     // between controls in a row
constexpr int kKeyWidth = 28;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 6;
constexpr int kNumberWidth = 26;
constexpr int kExtraMinWidth = 34;
constexpr int kDividerWidth = 9;
constexpr int kPrefixWidth = 14;
constexpr float kInStartScale = 0.6f;
constexpr float kOutEndScale = 0.7f;
constexpr float kFadeInFraction = 0.7f;
constexpr float kLabelHeight = 12.0f;

bool g_skipDesktop = false;

juce::Font chipFont() { return synth::theme::AppLookAndFeel::uiFont(kLabelHeight); }

const char* const kKindLetters[3] = {"S", "T", "D"};

} // namespace

// ----------------------------------------------------------------------------------------------------------------
// Item: one letter button or chip. A toggle-state button so the shared ease glides its hover and selection.
// ----------------------------------------------------------------------------------------------------------------
class NoteLengthPopup::Item final : public juce::Button {
public:
    Item(NoteLengthPopup& owner, const juce::String& label)
        : juce::Button(label)
        , owner_(owner)
        , label_(label) {
        setClickingTogglesState(false);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    /** The same choice whatever the kind (an extra, a bar length, a letter). */
    void setForAllKinds(const NoteLengthChip& chip) {
        for (auto& entry : byKind_)
            entry = chip;
        showKind(NoteKind::straight);
    }
    void setForKind(NoteKind kind, const NoteLengthChip& chip) { byKind_[static_cast<size_t>(kind)] = chip; }

    /** Points the chip at its value of `kind`; false (and the chip stays unusable) when the list has none. */
    bool showKind(NoteKind kind) {
        const auto& entry = byKind_[static_cast<size_t>(kind)];
        available_ = entry.has_value();
        if (available_) {
            name_ = entry->name;
            setTitle(entry->accessibleName);
            setTooltip(entry->tooltip);
        }
        return available_;
    }

    bool isAvailable() const noexcept { return available_; }
    const juce::String& name() const noexcept { return name_; }
    const juce::String& label() const noexcept { return label_; }
    int preferredWidth() const {
        return juce::jmax(kNumberWidth, juce::GlyphArrangement::getStringWidthInt(chipFont(), label_) + 16);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto& theme = synth::theme::themeOf(*this);
        const auto& c = theme.colors;
        const bool enabled = isEnabled();
        const auto amounts = ButtonStateEase::update(*this, highlighted && enabled, down && enabled);
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto wash = ButtonStateEase::blend(amounts, [&](bool on, bool hot, bool pressed) {
            if (on)
                return c.accent.withAlpha(pressed ? 0.38f : (hot ? 0.30f : 0.22f));
            return hot ? c.surfaceHi.withAlpha(pressed ? 1.0f : 0.8f) : c.surfaceHi.withAlpha(0.0f);
        });
        g.setColour(wash);
        g.fillRoundedRectangle(area, theme.metrics.cornerRadiusSmall);
        if (getToggleState()) {
            g.setColour(c.accent.withAlpha(enabled ? 0.9f : 0.4f));
            g.drawRoundedRectangle(area, theme.metrics.cornerRadiusSmall, theme.metrics.borderWidth);
        }
        g.setColour(!enabled ? c.textDisabled : (getToggleState() ? c.accent : c.textPrimary));
        g.setFont(chipFont());
        g.drawFittedText(label_, getLocalBounds(), juce::Justification::centred, 1);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, theme.metrics.cornerRadiusSmall);
    }

    void focusGained(FocusChangeType) override {
        owner_.noteFocus(*this);
        repaint();
    }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    NoteLengthPopup& owner_;
    juce::String label_;
    juce::String name_;
    bool available_ = true;
    std::optional<NoteLengthChip> byKind_[3];
};

// ----------------------------------------------------------------------------------------------------------------
// Body: the panel, the dividers and the "1/" prefix; the buttons are its children.
// ----------------------------------------------------------------------------------------------------------------
class NoteLengthPopup::Body final : public juce::Component {
public:
    Body() {
        setOpaque(false);
        setWantsKeyboardFocus(false);
    }

    void paint(juce::Graphics& g) override {
        const auto& theme = synth::theme::themeOf(*this);
        const auto& c = theme.colors;
        const auto panel = getLocalBounds().reduced(kShadow).toFloat();
        juce::Path path;
        path.addRoundedRectangle(panel, theme.metrics.cornerRadius * 0.6f);
        juce::DropShadow(juce::Colours::black.withAlpha(0.35f), 8, {0, 2}).drawForPath(g, path);
        g.setColour(c.surface);
        g.fillPath(path);
        g.setColour(c.border);
        g.strokePath(path, juce::PathStrokeType(theme.metrics.borderWidth));
        g.setColour(c.border);
        for (const int x : dividers)
            g.fillRect((float)x, (float)prefix.getY() + 4.0f, 1.0f, (float)prefix.getHeight() - 8.0f);
        if (!prefix.isEmpty()) {
            g.setColour(c.textMuted);
            g.setFont(chipFont());
            g.drawFittedText("1/", prefix, juce::Justification::centredRight, 1);
        }
    }

    std::vector<int> dividers; ///< x of each 1 px divider line
    juce::Rectangle<int> prefix;
};

// ----------------------------------------------------------------------------------------------------------------
// Construction and layout
// ----------------------------------------------------------------------------------------------------------------
NoteLengthPopup::NoteLengthPopup(juce::Component& anchor, Options options)
    : anchor_(&anchor)
    , options_(std::move(options))
    , model_(options_.choices) {
    setOpaque(false);
    setWantsKeyboardFocus(true);
    setTitle(options_.title.isNotEmpty() ? options_.title : juce::String("Note length"));
    setLookAndFeel(&anchor.getLookAndFeel());

    body_ = std::make_unique<Body>();
    addAndMakeVisible(*body_);

    const NoteKind kinds[3] = {NoteKind::straight, NoteKind::triplet, NoteKind::dotted};
    for (const NoteKind kind : kinds) {
        const auto index = static_cast<size_t>(kind);
        auto item = std::make_unique<Item>(*this, kKindLetters[index]);
        const auto name = NoteLengthMenuModel::kindName(kind);
        const bool available = model_.hasKind(kind);
        item->setForAllKinds({name, kKindLetters[index], name, name});
        item->onClick = [this, kind] { setKind(kind); };
        if (!available) {
            const auto& reason = options_.unavailableTip[index];
            item->setTooltip(reason.isNotEmpty() ? reason : name + " values are not offered here");
            item->setEnabled(false);
        }
        body_->addAndMakeVisible(*item);
        kindButtons_[index] = std::move(item);
    }
    buildChips();

    kind_ = model_.kindOf(options_.current);
    if (!model_.hasKind(kind_))
        kind_ = model_.hasKind(NoteKind::straight)
                    ? NoteKind::straight
                    : (model_.hasKind(NoteKind::triplet) ? NoteKind::triplet : NoteKind::dotted);

    int widest = 3 * kKeyWidth + 2 * kGap;
    for (const NoteKind kind : kinds)
        widest = juce::jmax(widest, rowWidth(kind));
    setSize(widest + 2 * kPad + 2 * kShadow, 2 * kPad + 2 * kRowHeight + kRowGap + 2 * kShadow);

    arrowKeys_ = std::make_unique<ArrowKeyNavigation>(*this);
    applyKind();

    // The keyboard cursor starts on the chip holding the current value, else on the first chip.
    const auto chips = shownChips();
    for (auto* chip : chips)
        if (active_ == nullptr && chip->getToggleState())
            active_ = chip;
    if (active_ == nullptr)
        active_ = !chips.empty() ? chips.front() : kindButtons_[static_cast<size_t>(kind_)].get();
}

NoteLengthPopup::~NoteLengthPopup() {
    stopTimer();
    if (listening_)
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    tooltipWindow_.reset();
    arrowKeys_.reset();
    setLookAndFeel(nullptr);
}

void NoteLengthPopup::buildChips() {
    const auto addChip = [this](std::vector<std::unique_ptr<Item>>& into, const NoteLengthChip& chip) {
        auto item = std::make_unique<Item>(*this, chip.label);
        item->setForAllKinds(chip);
        item->onClick = [this, item = item.get()] { pickChip(item->name()); };
        body_->addAndMakeVisible(*item);
        into.push_back(std::move(item));
    };
    for (const auto& chip : model_.leading())
        addChip(leading_, chip);
    for (const auto& chip : model_.trailing())
        addChip(trailing_, chip);

    // One chip per bottom number any kind has: a kind switch re-points it, so the row never rebuilds.
    std::set<int> denominators;
    for (const NoteKind kind : {NoteKind::straight, NoteKind::triplet, NoteKind::dotted})
        for (const auto& chip : model_.numbers(kind))
            denominators.insert(chip.label.getIntValue());
    for (const int denominator : denominators) {
        auto item = std::make_unique<Item>(*this, juce::String(denominator));
        for (const NoteKind kind : {NoteKind::straight, NoteKind::triplet, NoteKind::dotted})
            for (const auto& chip : model_.numbers(kind))
                if (chip.label.getIntValue() == denominator)
                    item->setForKind(kind, chip);
        item->onClick = [this, item = item.get()] { pickChip(item->name()); };
        body_->addAndMakeVisible(*item);
        numbers_.push_back(std::move(item));
    }
    // Longest note first (1, 2, 4 ... 128).
    std::sort(numbers_.begin(), numbers_.end(),
              [](const auto& a, const auto& b) { return a->label().getIntValue() < b->label().getIntValue(); });
}

void NoteLengthPopup::applyKind() {
    for (size_t i = 0; i < kindButtons_.size(); ++i)
        kindButtons_[i]->setToggleState(static_cast<size_t>(kind_) == i, juce::dontSendNotification);
    for (auto& chip : numbers_)
        chip->showKind(kind_);
    for (auto* group : {&leading_, &trailing_})
        for (auto& chip : *group)
            chip->showKind(NoteKind::straight);
    for (auto* group : {&leading_, &numbers_, &trailing_})
        for (auto& chip : *group)
            chip->setToggleState(chip->isAvailable() && chip->name() == options_.current, juce::dontSendNotification);
    layoutRows();
}

int NoteLengthPopup::rowWidth(NoteKind kind) const {
    int width = 0;
    const auto add = [&width](int w) { width += w + kGap; };
    for (const auto& chip : leading_)
        add(chip->preferredWidth());
    if (!leading_.empty())
        width += kDividerWidth;
    const int count = static_cast<int>(model_.numbers(kind).size());
    if (count > 0)
        width += kPrefixWidth + count * (kNumberWidth + kGap);
    if (!trailing_.empty()) {
        width += kDividerWidth;
        for (const auto& chip : trailing_)
            add(chip->preferredWidth());
    }
    return width;
}

void NoteLengthPopup::layoutRows() {
    const int x0 = kShadow + kPad;
    const int y1 = kShadow + kPad;
    const int y2 = y1 + kRowHeight + kRowGap;
    for (size_t i = 0; i < kindButtons_.size(); ++i)
        kindButtons_[i]->setBounds(x0 + (int)i * (kKeyWidth + kGap), y1, kKeyWidth, kRowHeight);

    body_->dividers.clear();
    body_->prefix = {};
    int x = x0;
    for (auto& chip : leading_) {
        chip->setBounds(x, y2, chip->preferredWidth(), kRowHeight);
        x += chip->preferredWidth() + kGap;
    }
    if (!leading_.empty()) {
        body_->dividers.push_back(x + kDividerWidth / 2);
        x += kDividerWidth;
    }
    bool anyNumber = false;
    for (auto& chip : numbers_)
        anyNumber = anyNumber || chip->isAvailable();
    if (anyNumber) {
        body_->prefix = {x, y2, kPrefixWidth, kRowHeight};
        x += kPrefixWidth;
    }
    for (auto& chip : numbers_) {
        chip->setVisible(chip->isAvailable());
        if (!chip->isAvailable())
            continue;
        chip->setBounds(x, y2, kNumberWidth, kRowHeight);
        x += kNumberWidth + kGap;
    }
    if (!trailing_.empty()) {
        body_->dividers.push_back(x + kDividerWidth / 2);
        x += kDividerWidth;
        for (auto& chip : trailing_) {
            chip->setBounds(x, y2, chip->preferredWidth(), kRowHeight);
            x += chip->preferredWidth() + kGap;
        }
    }
    body_->repaint();
}

void NoteLengthPopup::resized() {
    if (body_ != nullptr)
        body_->setBounds(getLocalBounds());
}

juce::Button& NoteLengthPopup::kindButton(NoteKind kind) { return *kindButtons_[static_cast<size_t>(kind)]; }

std::vector<juce::Button*> NoteLengthPopup::shownChips() const {
    std::vector<juce::Button*> chips;
    for (auto& chip : leading_)
        chips.push_back(chip.get());
    for (auto& chip : numbers_)
        if (chip->isAvailable())
            chips.push_back(chip.get());
    for (auto& chip : trailing_)
        chips.push_back(chip.get());
    return chips;
}

std::vector<juce::Button*> NoteLengthPopup::focusOrder() const {
    std::vector<juce::Button*> order;
    for (auto& key : kindButtons_)
        if (key->isEnabled())
            order.push_back(key.get());
    for (auto* chip : shownChips())
        order.push_back(chip);
    return order;
}

juce::Button* NoteLengthPopup::activeItem() const { return active_.getComponent(); }

void NoteLengthPopup::setKind(NoteKind kind) {
    if (kind == kind_ || !model_.hasKind(kind))
        return;
    kind_ = kind;
    applyKind();
    // The chip under the cursor keeps it when it is still there; a chip the new kind lacks hands it to the first one.
    const auto order = focusOrder();
    if (active_ == nullptr || std::find(order.begin(), order.end(), active_.getComponent()) == order.end())
        active_ = order.empty() ? nullptr : order.front();
    if (active_ != nullptr && isShowing())
        active_->grabKeyboardFocus();
}

void NoteLengthPopup::noteFocus(juce::Button& item) { active_ = &item; }

void NoteLengthPopup::stepActive(int direction) {
    const auto order = focusOrder();
    if (order.empty())
        return;
    const auto it = std::find(order.begin(), order.end(), active_.getComponent());
    const int from = it == order.end() ? 0 : static_cast<int>(it - order.begin());
    const int to = juce::jlimit(0, static_cast<int>(order.size()) - 1, from + direction);
    active_ = order[static_cast<size_t>(to)];
    active_->grabKeyboardFocus();
    repaint();
}

void NoteLengthPopup::pickChip(const juce::String& name) {
    if (closing_)
        return;
    restoreFocus_ = true;
    if (onPick)
        onPick(name);
    close();
}

// ----------------------------------------------------------------------------------------------------------------
// Keys
// ----------------------------------------------------------------------------------------------------------------
bool NoteLengthPopup::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        restoreFocus_ = true;
        close();
        return true;
    }
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    switch (juce::CharacterFunctions::toUpperCase(key.getTextCharacter())) { // not-ui-text: key matching
    case 'S':
        setKind(NoteKind::straight);
        return true;
    case 'T':
        setKind(NoteKind::triplet);
        return true;
    case 'D':
        setKind(NoteKind::dotted);
        return true;
    default:
        break;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        stepActive(key == juce::KeyPress::rightKey ? 1 : -1);
        return true;
    }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
        if (auto* item = activeItem())
            if (item->onClick)
                item->onClick();
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------------------------------------------
// Opening and closing
// ----------------------------------------------------------------------------------------------------------------
void NoteLengthPopup::setSkipDesktopForTest(bool skip) { g_skipDesktop = skip; }

// The panel hangs under the box (above it when the screen has no room below), left edges aligned, and is kept on the
// display; the grow pivots on the point of its edge nearest the box.
void NoteLengthPopup::place() {
    if (anchor_ == nullptr)
        return;
    const auto box = anchor_->getScreenBounds();
    const int bodyW = getWidth() - 2 * kShadow;
    const int bodyH = getHeight() - 2 * kShadow;
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(box);
    auto area =
        display != nullptr ? display->userArea : juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
    if (area.isEmpty())
        area = {-100000, -100000, 200000, 200000}; // no display to keep it on (a headless run)
    int top = box.getBottom() + 2;
    const bool above = top + bodyH > area.getBottom() && box.getY() - 2 - bodyH >= area.getY();
    if (above)
        top = box.getY() - 2 - bodyH;
    const int left = juce::jlimit(area.getX(), juce::jmax(area.getX(), area.getRight() - bodyW), box.getX());
    setTopLeftPosition(left - kShadow, top - kShadow);
    const float pivotX = (float)juce::jlimit(left + 8, juce::jmax(left + 8, left + bodyW - 8), box.getCentreX());
    pivot_ = {pivotX - (float)(left - kShadow), above ? (float)(kShadow + bodyH) : (float)kShadow};
    anchorPoint_ = box.getCentre();
}

popup_motion::Style NoteLengthPopup::motionStyle() const {
    popup_motion::Style style;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.alphaInFraction = kFadeInFraction;
    style.inStartScale = kInStartScale;
    style.outEndScale = kOutEndScale;
    style.anchor = [this] { return anchorPoint_; };
    style.body = [this]() -> juce::Component* { return body_.get(); };
    style.bodyPivot = [this] { return pivot_; };
    return style;
}

// Over a modal dialog (a shape editor window, say) the popup would open behind that window, and the dialog's modal
// state would swallow its clicks as well, since the popup is not inside it. So there it floats on top and becomes
// the topmost modal component itself, the way a juce::PopupMenu does; a press elsewhere then reaches
// inputAttemptWhenModal() instead of the global mouse listener.
void NoteLengthPopup::showOnDesktop() {
    place();
    const bool overModal = juce::Component::getCurrentlyModalComponent() != nullptr;
    if (!g_skipDesktop) {
        if (overModal)
            setAlwaysOnTop(true);
        addToDesktop(juce::ComponentPeer::windowIsTemporary);
        PopupMotion::attach(*this, motionStyle());
        tooltipWindow_ = std::make_unique<AppTooltipWindow>(this, options_.appProperties);
    }
    setVisible(true);
    if (overModal)
        enterModalState(false);
    if (!g_skipDesktop)
        toFront(true);
    if (active_ != nullptr)
        active_->grabKeyboardFocus();
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    listening_ = true;
    startTimer(200);
}

void NoteLengthPopup::close() {
    if (closing_)
        return;
    closing_ = true;
    stopTimer();
    juce::Component::SafePointer<NoteLengthPopup> self(this);
    PopupMotion::dismiss(*this, [self] {
        if (self == nullptr)
            return;
        self->setVisible(false);
        if (self->isCurrentlyModal(false))
            self->exitModalState(0);
        if (self->onClosed)
            self->onClosed();
    });
}

void NoteLengthPopup::mouseDown(const juce::MouseEvent& e) {
    if (closing_ || e.eventComponent == nullptr || e.eventComponent->getTopLevelComponent() == this)
        return;
    close();
}

void NoteLengthPopup::inputAttemptWhenModal() { close(); }

// Like a call-out, the popup does not outlive the app being in front.
void NoteLengthPopup::timerCallback() {
    if (!synth::testmode::appIsInFront())
        close();
}

} // namespace synth::ui
