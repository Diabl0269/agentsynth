#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// ButtonStateEase (docs/layout/theming.md#themed-widgets): the one ease every button drawn by the shared button
// drawing (AppLookAndFeel: text, drawable, icon and toggle buttons) gets for hover, press and toggled-on, so a state
// change glides instead of snapping. The look-and-feel calls update() from its draw function with the button's
// current state; it returns how far each state has eased (0 to 1) and the draw blends its colours with blend(). The
// ease lives on the button itself (a component property), so no button owner has code for it.
//
// 110 ms in (easeOutCubic), 90 ms out (easeInCubic), one driver per button, retargeting from the CURRENT amounts;
// 80 ms under Reduce Motion, none under Animations Off. The first draw, and any change while the button is not on
// screen (a headless test, a pixel golden), lands at once, so a draw reads the final state. Frames run only while
// a tween runs and repaint only that button.
namespace synth::ui {

class ButtonStateEase final
    : public juce::ReferenceCountedObject
    , private juce::ComponentListener {
public:
    static constexpr double kInMs = 110.0;
    static constexpr double kOutMs = 90.0;

    /** How far each state has eased: 0 not in it, 1 fully in it. `hover` is hovered OR pressed. */
    struct Amounts {
        float hover = 0.0f;
        float press = 0.0f;
        float on = 0.0f;
    };

    /** Points the button's ease at the given state (hot = hovered or pressed, only while enabled) and returns the
     *  amounts to draw with right now. The toggle state is read from the button. */
    static Amounts update(const juce::Button& button, bool hot, bool down) {
        auto& b = const_cast<juce::Button&>(button); // paint-time cache; the button's own state is untouched
        return ease(b).retarget({hot || down ? 1.0f : 0.0f, down ? 1.0f : 0.0f, button.getToggleState() ? 1.0f : 0.0f});
    }

    /** The amounts without moving them: the button's toggle state at rest when it has never been drawn. */
    static Amounts current(const juce::Button& button) {
        if (auto* e = find(button))
            return e->now_;
        return {0.0f, 0.0f, button.getToggleState() ? 1.0f : 0.0f};
    }

    /** A colour at `t` between `a` and `b`; a fully transparent end takes the other end's colour so a wash fades in
     *  as the same colour rather than through a grey. */
    static juce::Colour mix(juce::Colour a, juce::Colour b, float t) {
        if (t <= 0.0f)
            return a;
        if (t >= 1.0f)
            return b;
        if (a.getAlpha() == 0)
            a = b.withAlpha(0.0f);
        else if (b.getAlpha() == 0)
            b = a.withAlpha(0.0f);
        return a.interpolatedWith(b, t);
    }

    /** The colour at `amounts`, given `colourFor(on, hot, down)` for each settled state. At rest it is exactly
     *  colourFor of the settled state. */
    template <class ColourFor>
    static juce::Colour blend(Amounts amounts, ColourFor&& colourFor) {
        const auto at = [&](bool on) {
            return mix(mix(colourFor(on, false, false), colourFor(on, true, false), amounts.hover),
                       colourFor(on, true, true), amounts.press);
        };
        if (amounts.on <= 0.0f)
            return at(false);
        if (amounts.on >= 1.0f)
            return at(true);
        return mix(at(false), at(true), amounts.on);
    }

    /** Test seam: steps every ease in flight by hand (t in 0..1; 1 finishes them). */
    static void stepAllForTest(float t) {
        const auto all = registry();
        for (auto* e : all) {
            if (std::find(registry().begin(), registry().end(), e) == registry().end() || !e->driver_.isRunning())
                continue;
            if (t >= 1.0f)
                e->land();
            else
                e->frame(t);
        }
    }

    bool isEasing() const noexcept { return driver_.isRunning(); }
    static bool isEasing(const juce::Button& button) {
        auto* e = find(button);
        return e != nullptr && e->isEasing();
    }

    ~ButtonStateEase() override {
        registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end());
        if (button_ != nullptr)
            button_->removeComponentListener(this);
    }

private:
    explicit ButtonStateEase(juce::Button& button)
        : button_(&button) {
        registry().push_back(this);
        button.addComponentListener(this);
    }

    static const juce::Identifier& propertyId() {
        static const juce::Identifier id("buttonStateEase");
        return id;
    }

    static ButtonStateEase* find(const juce::Button& button) {
        return dynamic_cast<ButtonStateEase*>(button.getProperties()[propertyId()].getObject());
    }

    static ButtonStateEase& ease(juce::Button& button) {
        if (auto* e = find(button))
            return *e;
        auto* created = new ButtonStateEase(button);
        button.getProperties().set(propertyId(), juce::var(created));
        return *created;
    }

    static std::vector<ButtonStateEase*>& registry() {
        static std::vector<ButtonStateEase*> instances;
        return instances;
    }

    Amounts retarget(Amounts target) {
        if (!seen_) { // the first draw shows the state as it is
            seen_ = true;
            now_ = to_ = target;
            return now_;
        }
        if (target.hover == to_.hover && target.press == to_.press && target.on == to_.on)
            return now_;
        to_ = target;
        if (button_ == nullptr || !FadeVisibility::canAnimateIn(button_)) {
            settle(); // mid-paint: the draw in progress already reads the final amounts
            return now_;
        }
        from_ = now_;
        const bool leaving = to_.hover < from_.hover && to_.on <= from_.on;
        if (updater_ == nullptr)
            updater_ = std::make_unique<juce::VBlankAnimatorUpdater>(button_);
        driver_.start(
            *updater_, motionMs(leaving ? kOutMs : kInMs, FadeVisibility::kReducedMs),
            [leaving](float t) { return leaving ? easeInCubic(t) : easeOutCubic(t); }, [this](float t) { frame(t); },
            [this] { land(); });
        return now_;
    }

    void frame(float t) {
        const auto lerp = [t](float a, float b) { return a + (b - a) * t; };
        now_ = {lerp(from_.hover, to_.hover), lerp(from_.press, to_.press), lerp(from_.on, to_.on)};
        if (button_ != nullptr)
            button_->repaint();
    }

    // The end of a tween (or a change that lands at once). Stopping the driver destroys the callback that got here,
    // so nothing is read from it afterwards.
    void land() {
        settle();
        if (button_ != nullptr)
            button_->repaint();
    }

    void settle() {
        if (updater_ != nullptr)
            driver_.stop(*updater_);
        now_ = to_;
    }

    // The button is going away (its property still holds this): let go of everything that touches it now.
    void componentBeingDeleted(juce::Component& component) override {
        if (updater_ != nullptr)
            driver_.stop(*updater_);
        updater_.reset();
        component.removeComponentListener(this);
        button_ = nullptr;
    }

    juce::Button* button_;
    std::unique_ptr<juce::VBlankAnimatorUpdater> updater_;
    AnimationDriver driver_;
    Amounts from_, to_, now_;
    bool seen_ = false;
};

} // namespace synth::ui
