#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

// A section header that folds. Left folds it, Right unfolds it (ArrowKeyNavigation); a header class
// implements this next to juce::Button so the helper can reach it without knowing the owner.
struct FoldableHeader {
    virtual ~FoldableHeader() = default;
    virtual bool isFolded() const = 0;
    virtual void setFolded(bool folded) = 0; // idempotent; message thread only
};

// List-style arrow keys for a scope full of controls (a Settings tab, a dialog), as the module
// library has for its rows: Up/Down move keyboard focus to the previous/next control in the order Tab
// walks (clamped at the ends, no wrap), Right/Left tick/untick a focused check box or unfold/fold a
// focused FoldableHeader. Every other key, and any arrow with a modifier, passes through. The keys are
// fixed, not ShortcutManager actions. Native controls that own the arrows (ComboBox, Slider,
// TextEditor, a rebind button capturing a key) consume them before they bubble here.
//
// A juce::Viewport consumes Up/Down when a key bubbles through it, so one that sits inside the
// scope must be watched too (watchViewport / watchViewportsInScope): a listener on the viewport runs
// before the viewport's own keyPressed. Scrolling the newly focused control into view is the
// owner's ScrollIntoViewOnFocus.
class ArrowKeyNavigation : public juce::KeyListener {
public:
    // Listens on `scope`. The scope must outlive this object or be destroyed first without issue:
    // both are tracked weakly.
    explicit ArrowKeyNavigation(juce::Component& scope);
    ~ArrowKeyNavigation() override;

    void watchViewport(juce::Viewport& viewport);
    // Watches every plain juce::Viewport already under the scope (not a ListBox's or TextEditor's own).
    void watchViewportsInScope();

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // Where the focus comes from and goes to; the defaults are JUCE's own. A headless test has no
    // native window to hold real keyboard focus, so it supplies both.
    struct FocusHooks {
        std::function<juce::Component*()> focused;
        std::function<void(juce::Component&)> moveFocusTo;
    };
    void setFocusHooksForTest(FocusHooks hooks);
    bool isListeningOnForTest(const juce::Component* component) const;

private:
    std::vector<juce::Component*> focusStops() const;
    bool moveFocus(juce::Component& from, int direction);
    bool handleHorizontal(juce::Component& focused, bool rightKey);

    juce::Component::SafePointer<juce::Component> scope_;
    std::vector<juce::Component::SafePointer<juce::Component>> listeningOn_;
    FocusHooks hooks_;
};

} // namespace synth::ui
