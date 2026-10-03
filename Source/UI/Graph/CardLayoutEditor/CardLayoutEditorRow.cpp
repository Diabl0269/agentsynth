// CardLayoutEditorRow.cpp -- one row of the card layout editor: a group header or a control's tick,
// grab handle, renameable name and widget choice. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutEditorRow.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kTickWidth = 22;
constexpr int kGripWidth = 18;
constexpr int kWidgetWidth = 116;
constexpr int kGap = 4;
} // namespace

CardLayoutEditorGrip::CardLayoutEditorGrip() { setMouseCursor(dragGrabCursor()); }

// Three short bars: drawn geometry, so no glyph is needed for the handle.
void CardLayoutEditorGrip::paint(juce::Graphics& g) {
    g.setColour(findColour(juce::Label::textColourId).withAlpha(pressed_ ? 0.95f : 0.6f));
    const auto b = getLocalBounds().reduced(4, 8);
    for (int i = 0; i < 3; ++i)
        g.fillRect(b.getX(), b.getY() + i * (b.getHeight() / 2), b.getWidth(), 2);
}

void CardLayoutEditorGrip::mouseDown(const juce::MouseEvent& e) {
    pressed_ = true;
    repaint();
    if (onPress)
        onPress(e);
}

void CardLayoutEditorGrip::mouseDrag(const juce::MouseEvent& e) {
    if (onMove)
        onMove(e);
}

// The release may commit a reorder that rebuilds the whole list, this grip included, so the callback
// runs from a copy and nothing here touches a member afterwards.
void CardLayoutEditorGrip::mouseUp(const juce::MouseEvent&) {
    pressed_ = false;
    repaint();
    const auto released = onRelease;
    if (released)
        released();
}

CardLayoutEditorRow::CardLayoutEditorRow(const CardLayoutEditorModel::Row& row, bool draggable, bool renameable)
    : key_(row.key)
    , header_(row.kind == CardLayoutEditorModel::Row::Kind::Header)
    , tab_(header_ && row.tab)
    , draggable_(draggable && !header_)
    , name_(row.name)
    , fallbackName_(row.fallbackName)
    , labelOverride_(row.label) {
    setWantsKeyboardFocus(true);
    if (tab_) {
        setTitle("Tab: " + shownName());
        setTooltip("A tab of the card's tab strip; click its title to rename the tab (Enter)");
    } else if (header_) {
        setTitle("Group: " + shownName());
        setTooltip("A group of controls on the card; click its title to rename it (Enter)");
    } else {
        setTitle(name_);
        setTooltip(name_ + ": Space shows or hides it, " + platformCommandKeyName() +
                   "+Up or Down moves it, Enter renames it");
    }
    buildTick(row);
    buildName(renameable);
    buildWidgetChoice(row);
    buildGrip();
}

void CardLayoutEditorRow::buildTick(const CardLayoutEditorModel::Row& row) {
    tick_.setComponentID("knobPickerCheck:" + key_);
    tick_.setToggleState(row.shown && row.placed, juce::dontSendNotification);
    tick_.setWantsKeyboardFocus(false); // the row takes Space
    tick_.setTitle("Show " + name_ + " on the card");
    tick_.setTooltip("Show " + name_ + " on the card (Space)");
    tick_.onClick = [this] {
        const auto toggled = onToggled;
        if (toggled)
            toggled(tick_.getToggleState());
    };
    addChildComponent(tick_);
    tick_.setVisible(row.kind == CardLayoutEditorModel::Row::Kind::Param);
}

// A label edited in place is a NonModalLabel (Source/UI/CLAUDE.md). It commits on Return or focus loss
// through onTextChange.
void CardLayoutEditorRow::buildName(bool renameable) {
    nameLabel_.setComponentID("knobPickerLabel:" + key_);
    nameLabel_.setText(shownName(), juce::dontSendNotification);
    nameLabel_.setMinimumHorizontalScale(0.7f);
    nameLabel_.setEditable(renameable, false, false);
    nameLabel_.setInterceptsMouseClicks(renameable, false);
    nameLabel_.setWantsKeyboardFocus(false);
    if (header_) {
        nameLabel_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        nameLabel_.setTooltip("Click to rename this group");
    } else if (renameable) {
        nameLabel_.setTooltip(name_ + ": click to rename it on the card");
    }
    nameLabel_.onEditorShow = [this] {
        if (auto* editor = nameLabel_.getCurrentTextEditor(); editor != nullptr && header_ && name_.isEmpty())
            editor->setText({}, false);
    };
    nameLabel_.onTextChange = [this] { commitName(nameLabel_.getText()); };
    addAndMakeVisible(nameLabel_);
}

void CardLayoutEditorRow::buildWidgetChoice(const CardLayoutEditorModel::Row& row) {
    widgetChoices_ = row.widgetChoices;
    widgetCombo_.setComponentID("cardLayoutWidget:" + key_);
    widgetCombo_.setTitle("How " + name_ + " is drawn");
    widgetCombo_.setTooltip("How " + name_ + " is drawn on the card");
    int selected = 1;
    for (int i = 0; i < (int)widgetChoices_.size(); ++i) {
        widgetCombo_.addItem(cardLayoutWidgetName(widgetChoices_[(size_t)i]), i + 1);
        if (widgetChoices_[(size_t)i] == row.widget)
            selected = i + 1;
    }
    widgetCombo_.setSelectedId(selected, juce::dontSendNotification);
    widgetCombo_.onChange = [this] {
        const int index = widgetCombo_.getSelectedId() - 1;
        if (index >= 0 && index < (int)widgetChoices_.size() && onWidgetChosen)
            onWidgetChosen(widgetChoices_[(size_t)index]);
    };
    addChildComponent(widgetCombo_);
    widgetCombo_.setVisible(!header_ && row.placed && widgetChoices_.size() >= 2);
}

void CardLayoutEditorRow::buildGrip() {
    grip_.setTitle("Reorder " + name_);
    grip_.onPress = [this](const juce::MouseEvent& e) {
        if (onDragStarted)
            onDragStarted(e);
    };
    grip_.onMove = [this](const juce::MouseEvent& e) {
        if (onDragUpdated)
            onDragUpdated(e);
    };
    grip_.onRelease = [this] {
        const auto ended = onDragEnded;
        if (ended)
            ended();
    };
    addChildComponent(grip_);
    grip_.setVisible(draggable_);
}

juce::String CardLayoutEditorRow::shownName() const {
    if (header_)
        return name_.isEmpty() ? fallbackName_ : name_;
    return labelOverride_.value_or(name_);
}

// The row shows what the model will store: a header's title (cleared by an empty name or the group's
// fallback name, which then shows again), or a control's override (cleared the same way by the control's
// own name).
void CardLayoutEditorRow::commitName(const juce::String& text) {
    auto trimmed = text.trim();
    if (header_) {
        if (trimmed == fallbackName_)
            trimmed = {};
        name_ = trimmed;
    } else {
        labelOverride_ = trimmed.isEmpty() || trimmed == name_ ? std::optional<juce::String>() : trimmed;
    }
    nameLabel_.setText(shownName(), juce::dontSendNotification);
    if (header_)
        setTitle((tab_ ? "Tab: " : "Group: ") + shownName());
    if (onRenamed)
        onRenamed(trimmed);
}

bool CardLayoutEditorRow::startRename() {
    if (!nameLabel_.isEditable())
        return false;
    nameLabel_.showEditor();
    return true;
}

// The owner may rebuild its list, this row included, from inside a callback, so each one runs from a
// copy and nothing here touches a member afterwards.
bool CardLayoutEditorRow::toggleChecked() {
    if (header_ || !tick_.isVisible())
        return false;
    tick_.setToggleState(!tick_.getToggleState(), juce::dontSendNotification);
    const auto toggled = onToggled;
    if (toggled)
        toggled(tick_.getToggleState());
    return true;
}

bool CardLayoutEditorRow::keyPressed(const juce::KeyPress& key) {
    const auto handler = onKey;
    return handler != nullptr && handler(key);
}

void CardLayoutEditorRow::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

void CardLayoutEditorRow::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (header_) {
        const auto line = laf != nullptr ? laf->getTheme().colors.border : juce::Colours::grey;
        g.setColour(line);
        g.fillRect(getLocalBounds().removeFromBottom(1).reduced(2, 0));
    }
    if (lift_ > 0.0f) {
        const auto surface = synth::theme::themeOf(*this).colors.surfaceHi;
        const auto accent = synth::theme::themeOf(*this).colors.accent;
        paintReorderLift(g, getLocalBounds().toFloat(), lift_, surface, accent);
    }
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f);
}

void CardLayoutEditorRow::resized() {
    auto area = getLocalBounds().reduced(2, 0);
    if (!header_) {
        tick_.setBounds(area.removeFromLeft(kTickWidth));
        area.removeFromLeft(kGap);
        grip_.setBounds(area.removeFromLeft(kGripWidth));
        area.removeFromLeft(kGap);
        if (widgetCombo_.isVisible()) {
            widgetCombo_.setBounds(area.removeFromRight(kWidgetWidth).reduced(0, 2));
            area.removeFromRight(kGap);
        }
    }
    nameLabel_.setBounds(area);
}

} // namespace synth::ui
