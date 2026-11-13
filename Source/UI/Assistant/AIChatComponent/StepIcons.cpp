#include "StepIcons.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth::ui {

using songsteps::StepKind;

namespace {

constexpr double kCycleMs = 1600.0;
constexpr float kStillPhase = 0.9f;

float frac(float v) { return v - std::floor(v); }
// 0 to 1 and back, over one cycle.
float pingPong(float p) { return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f; }
float easeInOut(float t) { return t * t * (3.0f - 2.0f * t); }

struct Pen {
    juce::Graphics& g;
    juce::Colour colour;
    void stroke(const juce::Path& path, float width, float alpha = 1.0f) const {
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void line(float x1, float y1, float x2, float y2, float width, float alpha = 1.0f) const {
        juce::Path p;
        p.startNewSubPath(x1, y1);
        p.lineTo(x2, y2);
        stroke(p, width, alpha);
    }
    void outline(juce::Rectangle<float> r, float radius, float width, float alpha = 1.0f) const {
        juce::Path p;
        p.addRoundedRectangle(r, radius);
        stroke(p, width, alpha);
    }
    void fill(juce::Rectangle<float> r, float radius, float alpha = 1.0f) const {
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(r, radius);
    }
    void dot(float cx, float cy, float radius, float alpha = 1.0f) const {
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    }
};

// A cubic Bezier sampled into points, drawn only up to `fraction` of its length: the "line draws itself" motion.
juce::Path partialCurve(const std::vector<juce::Point<float>>& points, float fraction) {
    juce::Path path;
    const int last = juce::jmax(1, (int)std::lround(fraction * (float)(points.size() - 1)));
    path.startNewSubPath(points.front());
    for (int i = 1; i <= last && i < (int)points.size(); ++i)
        path.lineTo(points[(size_t)i]);
    return path;
}

juce::Point<float> cubic(juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::Point<float> d,
                         float t) {
    const float u = 1.0f - t;
    return a * (u * u * u) + b * (3.0f * u * u * t) + c * (3.0f * u * t * t) + d * (t * t * t);
}

void drawPlan(const Pen& pen, float p) {
    pen.outline({4.0f, 3.5f, 16.0f, 17.0f}, 2.5f, 1.8f);
    const float lengths[] = {8.0f, 8.0f, 5.0f};
    const float fade = p > 0.92f ? juce::jmax(0.0f, (1.0f - p) / 0.08f) : 1.0f;
    for (int k = 0; k < 3; ++k) {
        const float f = juce::jlimit(0.0f, 1.0f, (p - 0.15f * (float)k) / 0.35f);
        if (f > 0.0f)
            pen.line(8.0f, 9.0f + 4.0f * (float)k, 8.0f + lengths[k] * f, 9.0f + 4.0f * (float)k, 1.8f, fade);
    }
}

void drawTrack(const Pen& pen, float p) {
    const float s = easeInOut(pingPong(p));
    pen.outline({3.0f, 5.0f, 18.0f, 5.0f}, 1.8f, 1.8f);
    pen.fill({3.0f - 4.0f * (1.0f - s), 13.0f, 18.0f, 5.0f}, 1.8f, 0.5f + 0.5f * s);
}

// Piano-roll notes drifting sideways, fading in at one edge and out at the other.
void drawPattern(const Pen& pen, float p, bool moving) {
    pen.outline({2.5f, 3.5f, 19.0f, 17.0f}, 2.5f, 1.4f, 0.45f);
    const juce::Rectangle<float> notes[] = {
        {5.0f, 7.0f, 6.0f, 2.6f}, {11.0f, 11.0f, 5.0f, 2.6f}, {7.0f, 15.0f, 7.0f, 2.6f}};
    for (int i = 0; i < 3; ++i) {
        const float q = moving ? frac(p + (float)i / 3.0f) : 0.5f;
        const float alpha = q < 0.2f ? q / 0.2f : (q > 0.8f ? (1.0f - q) / 0.2f : 1.0f);
        pen.fill(notes[i].translated(-7.0f + 14.0f * q, 0.0f), 1.2f, alpha);
    }
}

void drawSections(const Pen& pen, float p) {
    const juce::Rectangle<float> blocks[] = {
        {3.0f, 6.0f, 5.0f, 12.0f}, {9.5f, 6.0f, 8.0f, 12.0f}, {19.0f, 6.0f, 3.0f, 12.0f}};
    for (int i = 0; i < 3; ++i) {
        const float s = easeInOut(pingPong(frac(p - 0.12f * (float)i)));
        pen.fill(blocks[i].translated(-4.0f * (1.0f - s), 0.0f), 1.5f, 0.5f + 0.5f * s);
    }
}

void drawAutomation(const Pen& pen, float p, bool moving) {
    pen.line(3.0f, 20.0f, 21.0f, 20.0f, 1.4f, 0.4f);
    pen.line(3.0f, 4.0f, 3.0f, 20.0f, 1.4f, 0.4f);
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= 16; ++i)
        pts.push_back(cubic({4.0f, 17.0f}, {8.0f, 17.0f}, {8.0f, 8.0f}, {12.0f, 8.0f}, (float)i / 16.0f));
    for (int i = 1; i <= 16; ++i)
        pts.push_back(cubic({12.0f, 8.0f}, {16.0f, 8.0f}, {16.0f, 16.0f}, {20.0f, 16.0f}, (float)i / 16.0f));
    pen.stroke(partialCurve(pts, moving ? juce::jlimit(0.0f, 1.0f, p / 0.6f) : 1.0f), 2.0f);
    pen.dot(12.0f, 8.0f, 1.8f * (moving ? 1.0f + 0.25f * pingPong(p) : 1.0f));
}

void drawModule(const Pen& pen, float p, bool moving) {
    const float s = moving ? pingPong(p) : 0.0f;
    pen.outline({3.5f, 4.5f, 17.0f, 15.0f}, 3.0f, 1.8f);
    pen.dot(9.0f, 12.0f, 2.6f * (1.0f + 0.25f * s), 1.0f - 0.4f * s);
    pen.dot(16.0f, 12.0f, 1.6f, 0.6f);
}

void drawConnect(const Pen& pen, float p, bool moving) {
    pen.dot(5.0f, 12.0f, 2.6f);
    pen.dot(19.0f, 12.0f, 2.6f);
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= 24; ++i)
        pts.push_back(cubic({7.6f, 12.0f}, {11.0f, 6.0f}, {13.0f, 18.0f}, {16.4f, 12.0f}, (float)i / 24.0f));
    pen.stroke(partialCurve(pts, moving ? juce::jlimit(0.0f, 1.0f, p / 0.7f) : 1.0f), 2.0f);
}

void drawSettings(const Pen& pen, float p) {
    pen.line(4.0f, 8.0f, 20.0f, 8.0f, 1.8f, 0.5f);
    pen.line(4.0f, 16.0f, 20.0f, 16.0f, 1.8f, 0.5f);
    const float s1 = easeInOut(pingPong(p)), s2 = easeInOut(pingPong(frac(p - 0.15f)));
    pen.dot(9.0f - 4.0f * (1.0f - s1), 8.0f, 2.8f, 0.5f + 0.5f * s1);
    pen.dot(15.0f - 4.0f * (1.0f - s2), 16.0f, 2.8f, 0.5f + 0.5f * s2);
}

void drawModulation(const Pen& pen, float p, bool moving) {
    juce::Path wave;
    const float shift = moving ? p * 8.0f : 0.0f;
    for (int i = 0; i <= 40; ++i) {
        const float x = 2.0f + 20.0f * (float)i / 40.0f;
        const float y = 12.0f - 4.5f * std::sin(juce::MathConstants<float>::twoPi * (x + shift) / 8.0f);
        if (i == 0)
            wave.startNewSubPath(x, y);
        else
            wave.lineTo(x, y);
    }
    pen.stroke(wave, 2.0f);
}

void drawFix(const Pen& pen, float p, bool moving) {
    juce::Path tri;
    tri.addTriangle(12.0f, 4.0f, 21.0f, 20.0f, 3.0f, 20.0f);
    pen.stroke(tri, 1.8f);
    const float alpha = (moving && p >= 0.5f) ? 0.25f : 1.0f;
    pen.line(12.0f, 10.0f, 12.0f, 15.0f, 2.0f, alpha);
    pen.dot(12.0f, 17.6f, 1.1f, alpha);
}

void drawMix(const Pen& pen, float p) {
    const float xs[] = {4.0f, 9.0f, 14.0f, 19.0f};
    for (int i = 0; i < 4; ++i) {
        const float s = easeInOut(pingPong(frac(p - 0.12f * (float)i)));
        const float h = 14.0f * (0.35f + 0.65f * s);
        pen.fill({xs[i], 20.0f - h, i == 3 ? 2.4f : 3.2f, h}, 1.2f);
    }
}

void drawKind(const Pen& pen, StepKind kind, float p, bool moving) {
    switch (kind) {
    case StepKind::plan:
        drawPlan(pen, p);
        break;
    case StepKind::track:
        drawTrack(pen, p);
        break;
    case StepKind::pattern:
        drawPattern(pen, p, moving);
        break;
    case StepKind::sections:
        drawSections(pen, p);
        break;
    case StepKind::automation:
        drawAutomation(pen, p, moving);
        break;
    case StepKind::module:
        drawModule(pen, p, moving);
        break;
    case StepKind::connect:
        drawConnect(pen, p, moving);
        break;
    case StepKind::settings:
        drawSettings(pen, p);
        break;
    case StepKind::modulation:
        drawModulation(pen, p, moving);
        break;
    case StepKind::fix:
        drawFix(pen, p, moving);
        break;
    case StepKind::mix:
        drawMix(pen, p);
        break;
    }
}

void drawRing(const Pen& pen, bool dashed) {
    juce::Path ring;
    ring.addEllipse(3.5f, 3.5f, 17.0f, 17.0f);
    if (!dashed) {
        pen.stroke(ring, 1.8f);
        return;
    }
    juce::Path dashes;
    const float pattern[] = {3.0f, 3.0f};
    juce::PathStrokeType(1.8f).createDashedStroke(dashes, ring, pattern, 2);
    pen.g.setColour(pen.colour);
    pen.g.fillPath(dashes);
}

} // namespace

void paintStepIcon(juce::Graphics& g, juce::Rectangle<float> area, StepKind kind, StepIconState state, float phase,
                   juce::Colour colour) {
    const float side = juce::jmin(area.getWidth(), area.getHeight());
    if (side <= 0.0f)
        return;
    const auto square = area.withSizeKeepingCentre(side, side);
    juce::Graphics::ScopedSaveState saved(g);
    g.addTransform(juce::AffineTransform::scale(side / 24.0f).translated(square.getX(), square.getY()));
    const Pen pen{g, colour};
    const bool moving = state == StepIconState::running || state == StepIconState::fixing;
    switch (state) {
    case StepIconState::done:
        drawRing(pen, false);
        {
            juce::Path check;
            check.startNewSubPath(8.0f, 12.5f);
            check.lineTo(11.0f, 15.5f);
            check.lineTo(16.0f, 9.0f);
            pen.stroke(check, 2.0f);
        }
        return;
    case StepIconState::waiting:
        drawRing(pen, true);
        return;
    case StepIconState::skipped:
        drawRing(pen, false);
        pen.line(8.0f, 12.0f, 16.0f, 12.0f, 2.0f);
        return;
    default:
        break;
    }
    drawKind(pen, kind, moving ? phase : kStillPhase, moving);
}

// -- StepIconView ----------------------------------------------------------------------------------------------------

StepIconView::StepIconView() {
    setInterceptsMouseClicks(false, false);
    setAccessible(false); // decorative: the row or card that holds it carries the name
}

StepIconView::~StepIconView() { driver_.stop(vblank_); }

void StepIconView::setKind(StepKind kind) {
    if (kind_ == kind)
        return;
    kind_ = kind;
    repaint();
}

void StepIconView::setColour(juce::Colour colour) {
    colour_ = colour;
    repaint();
}

void StepIconView::setDoneColour(juce::Colour colour) {
    doneColour_ = colour;
    repaint();
}

void StepIconView::setState(StepIconState state) {
    if (state_ == state)
        return;
    state_ = state;
    settleAfterCycle_ = false;
    updateLoop();
    repaint();
}

void StepIconView::paint(juce::Graphics& g) {
    const auto colour = (state_ == StepIconState::done && doneColour_) ? *doneColour_ : colour_;
    paintStepIcon(g, getLocalBounds().toFloat().reduced(0.5f), kind_, state_, phase_, colour);
}

// Motion is allowed when the app animates, the user has not asked to reduce it, and the icon is on screen; otherwise
// a running icon sits still in its running colour (the board's reduced-motion rule).
void StepIconView::updateLoop() {
    const bool moving = state_ == StepIconState::running || state_ == StepIconState::fixing;
    const bool allowed = moving && isShowing() && !animationsOff() && !prefersReducedMotion();
    if (allowed == looping_)
        return;
    looping_ = allowed;
    if (looping_) {
        startCycle();
    } else {
        driver_.stop(vblank_);
        phase_ = 0.0f;
        repaint();
    }
}

void StepIconView::startCycle() {
    driver_.start(
        vblank_, kCycleMs, [](float t) { return t; },
        [this](float t) {
            phase_ = t;
            repaint();
        },
        [this] {
            if (settleAfterCycle_) {
                settleAfterCycle_ = false;
                setState(StepIconState::done);
            } else if (looping_) {
                startCycle();
            }
        });
}

void StepIconView::playThenSettle(int delayMs) {
    if (animationsOff() || prefersReducedMotion() || !isShowing()) {
        setState(StepIconState::done);
        return;
    }
    juce::Component::SafePointer<StepIconView> safe(this);
    juce::Timer::callAfterDelay(juce::jmax(1, delayMs), [safe] {
        if (auto* self = safe.getComponent()) {
            self->setState(StepIconState::running);
            self->settleAfterCycle_ = true;
        }
    });
}

} // namespace synth::ui
