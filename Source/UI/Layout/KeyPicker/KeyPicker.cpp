#include "KeyPicker.h"

#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/KeyPicker/KeyPickerKeyboard.h"
#include "UI/Layout/KeyPicker/KeyPickerPalette.h"
#include "UI/Layout/KeyPicker/KeyPickerParts.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/SearchMatch.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kPad = 6;
constexpr int kGap = 4;
constexpr int kFollowHeight = 26;
constexpr int kSearchHeight = 26;
constexpr int kKeyboardHeight = 44;
constexpr int kListHeight = 190;
constexpr int kStatusHeight = 16;
constexpr int kButtonHeight = 26;
constexpr int kDetectWidth = 112;
constexpr int kNoKeyWidth = 64;
constexpr float kFontSize = 13.0f;
constexpr float kSmallFontSize = 11.0f;
} // namespace

int KeyPicker::preferredHeight(bool followRow) {
    const int follow = followRow ? kFollowHeight + kGap : 0;
    return kPad + follow + kSearchHeight + kGap + kKeyboardHeight + kGap + kListHeight + kGap + kStatusHeight + kGap +
           kButtonHeight + kPad;
}

KeyPicker::KeyPicker(Options options, std::function<void(std::optional<MusicalScale>)> onPickCallback)
    : onPick(std::move(onPickCallback))
    , options_(std::move(options))
    , search_(std::make_unique<NavigationSearchField>())
    , keyboard_(std::make_unique<KeyPickerKeyboard>()) {
    root_ = options_.current.has_value() ? ((options_.current->rootPitchClass % 12) + 12) % 12 : 0;
    setComponentID("keyPicker");
    setTitle(options_.what + " picker");
    setWantsKeyboardFocus(false);
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    if (options_.showFollowRow) {
        follow_ = std::make_unique<KeyPickerFollowRow>("Follow the song key again");
        follow_->setState(options_.following, options_.followDetail);
        follow_->setExplicitFocusOrder(1);
        follow_->onClick = [this] {
            auto callback = onFollow;
            endAudition();
            dismiss();
            if (callback)
                callback();
        };
        addAndMakeVisible(*follow_);
    }

    search_->setComponentID("keyPickerSearch");
    search_->setTitle("Search keys");
    search_->setTooltip("Type a root and a scale, like d dor. Up and Down move, Return picks, Escape closes.");
    search_->setMultiLine(false);
    search_->setReturnKeyStartsNewLine(false);
    search_->setSelectAllWhenFocused(true);
    search_->setJustification(juce::Justification::centredLeft);
    search_->setBorder(juce::BorderSize<int>(0));
    search_->setIndents(6, 0);
    search_->setFont(juce::Font(juce::FontOptions(kFontSize)));
    search_->setExplicitFocusOrder(2);
    search_->onTextChange = [this] { applyFilter(); };
    search_->onNavigationKey = [this](const juce::KeyPress& key) { return sendKey(key); };
    addAndMakeVisible(*search_);

    keyboard_->onRootClicked = [this](int pitchClass) { clickKeyboardRoot(pitchClass); };
    addAndMakeVisible(*keyboard_);

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&rowColumn_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);
    viewport_.addMouseListener(this, true);

    status_.setComponentID("keyPickerStatus");
    status_.setTitle("Key status");
    status_.setFont(juce::Font(juce::FontOptions(kSmallFontSize)));
    status_.setJustificationType(juce::Justification::centredLeft);
    status_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(status_);

    relative_ = std::make_unique<KeyPickerButton>("Relative key", "Switch to the relative major or minor key");
    relative_->setExplicitFocusOrder(3);
    relative_->onClick = [this] { clickRelative(); };
    addAndMakeVisible(*relative_);

    detectButton_ = std::make_unique<KeyPickerButton>("Detect from notes",
                                                      "Suggest the key that fits the notes in your MIDI clips");
    detectButton_->setExplicitFocusOrder(4);
    detectButton_->onClick = [this] { clickDetect(); };
    addChildComponent(*detectButton_);

    noKey_ = std::make_unique<KeyPickerButton>("No key", "Turn the song key off");
    noKey_->setExplicitFocusOrder(5);
    noKey_->onClick = [this] { clickNoKey(); };
    addChildComponent(*noKey_);
    noKey_->setVisible(options_.showNoKey);

    buildRows();
    applyColours();
    setSize(kWidth, preferredHeight(options_.showFollowRow));
    ready_ = true;
}

KeyPicker::~KeyPicker() {
    stopTimer();
    viewport_.removeMouseListener(this);
    endAudition();
    if (onClosed)
        onClosed();
}

void KeyPicker::buildRows() {
    const auto& presets = builtInScalePresets();
    auto addHeader = [this](const juce::String& title) {
        rows_.push_back(std::make_unique<KeyPickerRow>(title));
        rowColumn_.addAndMakeVisible(*rows_.back());
    };
    auto addScale = [this](const juce::String& name, std::uint16_t mask) {
        rows_.push_back(std::make_unique<KeyPickerRow>(name, mask, root_));
        rows_.back()->onClick = [this](KeyPickerRow& r) { pick(r.key()); };
        rows_.back()->onHover = [this](KeyPickerRow& r) { hoverRow(r); };
        rowColumn_.addAndMakeVisible(*rows_.back());
    };
    for (const auto& group : keyScaleGroups()) {
        addHeader(group.title);
        for (const char* name : group.scales) {
            const int index = scalePresetIndex(name);
            if (index >= 0)
                addScale(name, presets[(size_t)index].mask);
        }
    }
    const bool anyUser = std::any_of(options_.userScales.begin(), options_.userScales.end(),
                                     [](const UserScale& s) { return (s.mask & 0xFFF) != 0; });
    if (anyUser) {
        addHeader("Your scales");
        for (const auto& scale : options_.userScales)
            if ((scale.mask & 0xFFF) != 0)
                addScale(scale.name, (std::uint16_t)(scale.mask & 0xFFF));
    }

    std::vector<juce::Component*> components;
    for (const auto& row : rows_)
        components.push_back(row.get());
    fades_.reset(components);
    fades_.onFrame = [this] {
        layoutRowColumn();
        if (!fades_.anyFading())
            setHighlight(highlighted_);
    };
    refreshTicks();
    applyFilter();
}

std::vector<KeyPickerRow*> KeyPicker::visibleItemRows() const {
    std::vector<KeyPickerRow*> out;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (!rows_[i]->isHeader() && fades_.isShown(i))
            out.push_back(rows_[i].get());
    return out;
}

void KeyPicker::setRoot(int root) {
    root = ((root % 12) + 12) % 12;
    root_ = root;
    for (auto& row : rows_)
        if (!row->isHeader())
            row->setRoot(root);
    refreshTicks();
}

void KeyPicker::refreshTicks() {
    for (auto& row : rows_)
        if (!row->isHeader())
            row->setTicked(!options_.following && options_.current.has_value() &&
                           sameKey(row->key(), *options_.current));
}

// A header shows only while a row under it matches. The highlight lands on the best match for the scale words, or
// on the current key's scale when there are none.
void KeyPicker::applyFilter() {
    const auto parsed = parseKeyQuery(search_->getText());
    if (parsed.root >= 0)
        setRoot(parsed.root);
    const auto& words = parsed.scaleWords;

    int pendingHeader = -1;
    bool headerHasMatch = false;
    int bestIndex = -1;
    int bestScore = 0;
    int currentIndex = -1;
    int visibleIndex = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (row->isHeader()) {
            if (pendingHeader >= 0)
                fades_.steer((size_t)pendingHeader, headerHasMatch);
            pendingHeader = (int)i;
            headerHasMatch = false;
            continue;
        }
        const bool matches = searchMatches(row->scaleName(), words);
        fades_.steer(i, matches);
        row->setQuery(words);
        headerHasMatch = headerHasMatch || matches;
        if (!matches)
            continue;
        const int score = searchScore(row->scaleName(), words);
        if (bestIndex < 0 || score < bestScore) {
            bestIndex = visibleIndex;
            bestScore = score;
        }
        if (options_.current.has_value() && row->scaleName() == options_.current->name &&
            row->mask() == (options_.current->mask & 0xFFF))
            currentIndex = visibleIndex;
        ++visibleIndex;
    }
    if (pendingHeader >= 0)
        fades_.steer((size_t)pendingHeader, headerHasMatch);
    fades_.markLaidOut();

    layoutRowColumn();
    if (words.isNotEmpty() && bestIndex >= 0)
        setHighlight(bestIndex);
    else
        setHighlight(currentIndex >= 0 ? currentIndex : 0);
    viewport_.setViewPosition(0, 0);
}

void KeyPicker::layoutRowColumn() {
    const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
    int y = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!fades_.occupies(i))
            continue;
        const int slot = fades_.slot(i, rows_[i]->preferredHeight());
        rows_[i]->setBounds(0, y, width, slot);
        y += slot;
    }
    rowColumn_.setSize(width, juce::jmax(y, 1));
}

void KeyPicker::resized() {
    auto bounds = getLocalBounds().reduced(kPad);
    if (follow_ != nullptr) {
        follow_->setBounds(bounds.removeFromTop(kFollowHeight));
        bounds.removeFromTop(kGap);
    }
    search_->setBounds(bounds.removeFromTop(kSearchHeight));
    bounds.removeFromTop(kGap);
    keyboard_->setBounds(bounds.removeFromTop(kKeyboardHeight));
    bounds.removeFromTop(kGap);

    auto buttons = bounds.removeFromBottom(kButtonHeight);
    bounds.removeFromBottom(kGap);
    status_.setBounds(bounds.removeFromBottom(kStatusHeight));
    bounds.removeFromBottom(kGap);
    viewport_.setBounds(bounds);

    detectButton_->setVisible(detect != nullptr);
    noKey_->setVisible(options_.showNoKey);
    if (options_.showNoKey)
        noKey_->setBounds(buttons.removeFromRight(kNoKeyWidth));
    if (options_.showNoKey && detect != nullptr)
        buttons.removeFromRight(kGap);
    if (detect != nullptr)
        detectButton_->setBounds(buttons.removeFromRight(kDetectWidth));
    if (options_.showNoKey || detect != nullptr)
        buttons.removeFromRight(kGap);
    relative_->setBounds(buttons);
    layoutRowColumn();
}

void KeyPicker::paint(juce::Graphics& g) {
    const auto p = keypicker::paletteFor(*this);
    auto bounds = getLocalBounds().toFloat();
    g.setColour(p.panel);
    g.fillRoundedRectangle(bounds, p.radius);
    g.setColour(p.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), p.radius, 1.0f);
}

void KeyPicker::paintOverChildren(juce::Graphics& g) { paintFocusRegionOutline(*this, g); }

void KeyPicker::applyColours() {
    const auto p = keypicker::paletteFor(*this);
    search_->setColour(juce::TextEditor::backgroundColourId, p.field);
    search_->setColour(juce::TextEditor::textColourId, p.text);
    search_->setColour(juce::TextEditor::outlineColourId, p.border);
    search_->setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    search_->setTextToShowWhenEmpty("Search keys, e.g. d dor", p.muted);
    status_.setColour(juce::Label::textColourId, p.muted);
    keyboard_->setPalette(p);
    relative_->setPalette(p);
    detectButton_->setPalette(p);
    noKey_->setPalette(p);
    if (follow_ != nullptr)
        follow_->setPalette(p);
    for (const auto& row : rows_)
        row->setPalette(p);
    repaint();
}

void KeyPicker::lookAndFeelChanged() { applyColours(); }

// A CallOutBox attaches its content after construction, so the LookAndFeel and the focus target only become real
// once this component has a parent. Callers wire `detect` after the constructor's first layout, so lay out again
// here or the Detect button stays hidden.
void KeyPicker::parentHierarchyChanged() {
    applyColours();
    resized();
    reveal_.startIfInCallout();
    if (isShowing())
        search_->grabKeyboardFocus();
}

bool KeyPicker::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        dismiss();
        return true;
    }
    return false;
}

void KeyPicker::mouseExit(const juce::MouseEvent&) {
    if (!viewport_.isMouseOver(true))
        endAudition();
}

void KeyPicker::dismiss() {
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        PopupMotion::dismissCallOut(*box);
}

// ---- Highlight and audition ----

void KeyPicker::setHighlight(int index) {
    const auto visible = visibleItemRows();
    if (visible.empty()) {
        highlighted_ = 0;
        highlightChanged();
        return;
    }
    highlighted_ = juce::jlimit(0, (int)visible.size() - 1, index);
    for (int i = 0; i < (int)visible.size(); ++i)
        visible[(size_t)i]->setHighlighted(i == highlighted_);

    const auto rowBounds = visible[(size_t)highlighted_]->getBounds();
    const auto view = viewport_.getViewArea();
    if (rowBounds.getY() < view.getY())
        viewport_.setViewPosition(0, rowBounds.getY());
    else if (rowBounds.getBottom() > view.getBottom())
        viewport_.setViewPosition(0, rowBounds.getBottom() - view.getHeight());
    highlightChanged();
}

void KeyPicker::moveHighlight(int delta) {
    const int count = (int)visibleItemRows().size();
    if (count > 0)
        setHighlight(juce::jlimit(0, count - 1, highlighted_ + (delta < 0 ? -1 : 1)));
}

void KeyPicker::highlightKey(const MusicalScale& key) {
    const auto visible = visibleItemRows();
    for (int i = 0; i < (int)visible.size(); ++i)
        if (visible[(size_t)i]->scaleName() == key.name && visible[(size_t)i]->mask() == (key.mask & 0xFFF)) {
            setHighlight(i);
            return;
        }
}

std::optional<MusicalScale> KeyPicker::getHighlighted() const {
    const auto visible = visibleItemRows();
    if (visible.empty())
        return std::nullopt;
    return visible[(size_t)juce::jlimit(0, (int)visible.size() - 1, highlighted_)]->key();
}

// The highlighted key decides the keyboard strip and the Relative button, and (once the picker has settled) restarts
// the audition wait.
void KeyPicker::highlightChanged() {
    const auto key = getHighlighted();
    keyboard_->setKey(key.has_value() ? key : options_.current);
    updateRelativeButton();
    if (sameKey(key, lastHighlight_))
        return;
    lastHighlight_ = key;
    if (!ready_)
        return;
    if (key.has_value())
        startTimer(kAuditionDelayMs);
    else
        endAudition();
}

void KeyPicker::hoverRow(KeyPickerRow& row) {
    const auto visible = visibleItemRows();
    const auto it = std::find(visible.begin(), visible.end(), &row);
    if (it == visible.end())
        return;
    setHighlight((int)(it - visible.begin()));
    if (!auditioning_ && !isTimerRunning())
        startTimer(kAuditionDelayMs);
}

void KeyPicker::timerCallback() {
    stopTimer();
    const auto key = getHighlighted();
    if (!key.has_value() || !onAudition)
        return;
    auditioning_ = true;
    onAudition(key);
}

void KeyPicker::endAudition() {
    stopTimer();
    if (!auditioning_)
        return;
    auditioning_ = false;
    if (onAudition)
        onAudition(std::nullopt);
}

// ---- Actions ----

void KeyPicker::pick(std::optional<MusicalScale> key) {
    auto callback = onPick;
    endAudition();
    dismiss();
    if (callback)
        callback(key);
}

std::optional<MusicalScale> KeyPicker::relativeTarget() const {
    const auto key = getHighlighted();
    const auto& basis = key.has_value() ? key : options_.current;
    return basis.has_value() ? relativeKey(*basis) : std::nullopt;
}

void KeyPicker::updateRelativeButton() {
    const auto target = relativeTarget();
    relative_->setEnabled(target.has_value());
    relative_->setButtonText(target.has_value() ? "Relative: " + keyDisplayName(*target)
                                                : juce::String("Relative key"));
}

void KeyPicker::setStatus(const juce::String& text) { status_.setText(text, juce::dontSendNotification); }

bool KeyPicker::sendKey(const juce::KeyPress& key) {
    if (key == juce::KeyPress::downKey) {
        moveHighlight(1);
        return true;
    }
    if (key == juce::KeyPress::upKey) {
        moveHighlight(-1);
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        pickHighlighted();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        dismiss();
        return true;
    }
    return false;
}

// ---- Test seams ----

void KeyPicker::setQuery(const juce::String& text) {
    search_->setText(text, juce::dontSendNotification);
    applyFilter();
}

std::vector<MusicalScale> KeyPicker::visibleKeys() const {
    std::vector<MusicalScale> keys;
    for (auto* row : visibleItemRows())
        keys.push_back(row->key());
    return keys;
}

std::vector<juce::String> KeyPicker::visibleGroupTitles() const {
    std::vector<juce::String> titles;
    for (size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i]->isHeader() && fades_.isShown(i))
            titles.push_back(rows_[i]->getTitle());
    return titles;
}

bool KeyPicker::isVisibleKeyTicked(int index) const {
    const auto visible = visibleItemRows();
    return index >= 0 && index < (int)visible.size() && visible[(size_t)index]->isTicked();
}

juce::String KeyPicker::getStatusText() const { return status_.getText(); }
bool KeyPicker::isRelativeEnabled() const { return relative_->isEnabled(); }
juce::String KeyPicker::getRelativeText() const { return relative_->getButtonText(); }

void KeyPicker::pickHighlighted() {
    if (const auto key = getHighlighted())
        pick(key);
}

void KeyPicker::clickRelative() {
    if (const auto target = relativeTarget())
        pick(target);
}

bool KeyPicker::isDetectShown() const { return detectButton_->isVisible(); }

void KeyPicker::clickDetect() {
    if (!detect)
        return;
    const auto key = detect();
    if (!key.has_value()) {
        setStatus("No notes to read yet");
        return;
    }
    search_->setText({}, juce::dontSendNotification);
    setRoot(key->rootPitchClass);
    applyFilter();
    highlightKey(*key);
    setStatus("Detected: " + keyDisplayName(*key));
}

void KeyPicker::clickNoKey() { pick(std::nullopt); }

void KeyPicker::clickFollow() {
    if (follow_ != nullptr && follow_->onClick)
        follow_->onClick();
}

// The root typed in the search field would win on the next keystroke, so a root chosen on the strip replaces it.
void KeyPicker::clickKeyboardRoot(int pitchClass) {
    const auto parsed = parseKeyQuery(search_->getText());
    if (parsed.root >= 0)
        search_->setText(parsed.scaleWords, juce::dontSendNotification);
    setRoot(pitchClass);
    highlightChanged();
}

void KeyPicker::hoverVisibleKey(int index) {
    const auto visible = visibleItemRows();
    if (index >= 0 && index < (int)visible.size())
        hoverRow(*visible[(size_t)index]);
}

} // namespace synth::ui
