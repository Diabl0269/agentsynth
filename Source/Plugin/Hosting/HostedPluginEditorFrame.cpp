// HostedPluginEditorFrame.cpp -- the plugin editor window's content: the editor below a strip that carries the
// "Adding controls" tab. docs/control/plugin-card-layout.md#add-by-moving-a-control-in-the-plugin.
#include "HostedPluginEditorFrame.h"

#include "UI/Graph/ModDot/ModDotMotion.h"
#include "UI/Graph/ModDot/ModDotPalette.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/UIAnimation.h"

namespace synth {

namespace {
constexpr double kShowMs = 190.0; // easeOutBack: out of the top edge with a small bounce
constexpr double kHideMs = 120.0; // easeInCubic: back into it
} // namespace

// The tab: "Adding controls" and a Done button, centred in the strip.
class HostedPluginEditorFrame::Tab final : public juce::Component {
public:
    Tab() {
        setTitle("Adding controls");
        setInterceptsMouseClicks(false, true);
        addAndMakeVisible(done_);
    }

    juce::Button& done() noexcept { return done_; }

    void paint(juce::Graphics& g) override {
        const auto p = synth::ui::modDotPaletteFor(*this);
        g.fillAll(p.panel);
        g.setColour(p.accent);
        g.fillRect(getLocalBounds().removeFromBottom(2));
        g.setColour(p.text);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("Adding controls", textArea(), juce::Justification::centredRight, true);
    }

    void resized() override { done_.setBounds(doneArea()); }

private:
    static constexpr int kDoneWidth = 64;
    static constexpr int kGap = 10;
    static constexpr int kTextWidth = 112;

    // The text and the button as one group, centred on the strip.
    juce::Rectangle<int> groupArea() const {
        const int w = kTextWidth + kGap + kDoneWidth;
        return juce::Rectangle<int>(w, getHeight() - 2).withCentre({getWidth() / 2, (getHeight() - 2) / 2});
    }
    juce::Rectangle<int> textArea() const { return groupArea().withWidth(kTextWidth); }
    juce::Rectangle<int> doneArea() const { return groupArea().withTrimmedLeft(kTextWidth + kGap).reduced(0, 3); }

    class Done final : public juce::Button {
    public:
        Done()
            : juce::Button("Done")
            , hover_(*this) {
            setTitle("Done adding controls");
            setTooltip("Stop adding controls. Esc does the same");
            setWantsKeyboardFocus(true);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
                triggerClick();
                return true;
            }
            return false;
        }
        void mouseEnter(const juce::MouseEvent& e) override {
            juce::Button::mouseEnter(e);
            hover_.setHovered(true);
        }
        void mouseExit(const juce::MouseEvent& e) override {
            juce::Button::mouseExit(e);
            hover_.setHovered(false);
        }
        void paintButton(juce::Graphics& g, bool, bool down) override {
            const auto p = synth::ui::modDotPaletteFor(*this);
            const auto area = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(p.accent.withAlpha(down ? 0.34f : 0.16f + 0.12f * hover_.value()));
            g.fillRoundedRectangle(area, 6.0f);
            g.setColour(p.accent);
            g.setFont(juce::Font(juce::FontOptions(12.5f)));
            g.drawText("Done", getLocalBounds(), juce::Justification::centred, false);
            synth::ui::paintFocusRing(g, area, *this, 6.0f);
        }

    private:
        synth::ui::ModDotHoverFade hover_;
    };

    Done done_;
};

HostedPluginEditorFrame::HostedPluginEditorFrame(std::unique_ptr<juce::Component> inner)
    : inner_(std::move(inner))
    , tab_(std::make_unique<Tab>())
    , updater_(this) {
    addAndMakeVisible(*inner_);
    addChildComponent(*tab_);
    tab_->done().onClick = [this] {
        if (onDone)
            onDone();
    };
    setOpaque(false);
    setSize(juce::jmax(1, inner_->getWidth()), juce::jmax(1, inner_->getHeight()));
    laidOut_ = true;
}

HostedPluginEditorFrame::~HostedPluginEditorFrame() { anim_.stop(updater_); }

juce::Button& HostedPluginEditorFrame::doneButton() noexcept { return tab_->done(); }

bool HostedPluginEditorFrame::hasTabForTest() const noexcept { return tab_->isVisible(); }

void HostedPluginEditorFrame::paint(juce::Graphics& g) {
    // Behind the tab while the bounce overshoots the strip, so no gap shows above it.
    g.setColour(synth::ui::modDotPaletteFor(*this).panel);
    g.fillRect(0, 0, getWidth(), strip_);
}

// The strip on top, the editor under it at the size the frame has left.
void HostedPluginEditorFrame::resized() {
    const juce::ScopedValueSetter<bool> guard(layingOut_, true);
    inner_->setBounds(0, strip_, getWidth(), juce::jmax(0, getHeight() - strip_));
    tab_->setBounds(0, strip_ - kTabHeight, getWidth(), kTabHeight);
}

// The editor asked for another size (the plugin resized itself): the frame follows, keeping the strip.
void HostedPluginEditorFrame::childBoundsChanged(juce::Component* child) {
    if (child != inner_.get() || layingOut_ || !laidOut_)
        return;
    const int wantedW = inner_->getWidth();
    const int wantedH = inner_->getHeight() + strip_;
    if (wantedW != getWidth() || wantedH != getHeight())
        setSize(wantedW, wantedH);
}

// Grows or shrinks the frame by the change in the strip, so the editor underneath keeps its size.
void HostedPluginEditorFrame::setStrip(int pixels) {
    pixels = juce::jmax(0, pixels);
    if (pixels == strip_)
        return;
    const int delta = pixels - strip_;
    strip_ = pixels;
    tab_->setVisible(pixels > 0);
    setSize(getWidth(), juce::jmax(1, getHeight() + delta));
    resized(); // the size may not have changed (a clamped window), the strip did
    repaint();
}

void HostedPluginEditorFrame::setAddingControls(bool on) {
    if (adding_ == on)
        return;
    adding_ = on;
    animateTo(on ? 1.0f : 0.0f);
}

// Nothing moves unanimated; off screen or under Reduce Motion it lands at once. A retarget starts from where the
// tab is, never from its end.
void HostedPluginEditorFrame::animateTo(float target) {
    const float from = amount_;
    const auto land = [this, target] {
        amount_ = target;
        setStrip(juce::roundToInt((float)kTabHeight * target));
    };
    if (!synth::ui::modDotMotionAllowed(*this)) {
        anim_.stop(updater_);
        land();
        return;
    }
    const bool show = target > from;
    anim_.start(
        updater_, show ? kShowMs : kHideMs, show ? synth::ui::easeOutBack : synth::ui::easeInCubic,
        [this, from, target](float t) {
            amount_ = from + (target - from) * t;
            setStrip(juce::roundToInt((float)kTabHeight * amount_));
        },
        land);
}

} // namespace synth
