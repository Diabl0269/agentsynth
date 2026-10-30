// PointMarkerProbe.cpp -- the recording renderer behind PointMarkerProbe.h.
#include "PointMarkerProbe.h"
#include "UI/Layout/FocusRing.h"
#include <cmath>
#include <gtest/gtest.h>
#include <sstream>

namespace point_marker_test {

namespace {

constexpr float kOnDotPx = 1.0f;     // a marker this close to the dot's centre is on it
constexpr float kNearDotPx = 14.0f;  // a ring-sized marker this close to the dot belongs to the point
constexpr float kNearStalePx = 6.0f; // and so does one painted at (or by) the place the point left
constexpr float kMaxMarkerPx = 40.0f;

/** A software renderer that also notes each marker shape, mapped back to the painted component's coordinates through
    the origin and transform stack the component tree sets up while it paints. */
class Recorder : public juce::LowLevelGraphicsSoftwareRenderer {
public:
    Recorder(const juce::Image& image, std::vector<Mark>& out)
        : juce::LowLevelGraphicsSoftwareRenderer(image)
        , out_(out) {}

    void setOrigin(juce::Point<int> o) override {
        top().transform = juce::AffineTransform::translation(o.toFloat()).followedBy(top().transform);
        LowLevelGraphicsSoftwareRenderer::setOrigin(o);
    }
    void addTransform(const juce::AffineTransform& t) override {
        top().transform = t.followedBy(top().transform);
        LowLevelGraphicsSoftwareRenderer::addTransform(t);
    }
    void saveState() override {
        states_.push_back(top());
        LowLevelGraphicsSoftwareRenderer::saveState();
    }
    void restoreState() override {
        pop();
        LowLevelGraphicsSoftwareRenderer::restoreState();
    }
    void beginTransparencyLayer(float opacity) override {
        states_.push_back(top());
        LowLevelGraphicsSoftwareRenderer::beginTransparencyLayer(opacity);
    }
    void endTransparencyLayer() override {
        pop();
        LowLevelGraphicsSoftwareRenderer::endTransparencyLayer();
    }
    void setFill(const juce::FillType& fill) override {
        top().alpha = fill.isColour() ? fill.colour.getFloatAlpha() : 1.0f;
        LowLevelGraphicsSoftwareRenderer::setFill(fill);
    }

    void fillEllipse(const juce::Rectangle<float>& area) override {
        note(Mark::Shape::ellipse, true, area);
        LowLevelGraphicsSoftwareRenderer::fillEllipse(area);
    }
    void drawEllipse(const juce::Rectangle<float>& area, float thickness) override {
        note(Mark::Shape::ellipse, false, area);
        LowLevelGraphicsSoftwareRenderer::drawEllipse(area, thickness);
    }
    void fillRoundedRectangle(const juce::Rectangle<float>& area, float corner) override {
        note(Mark::Shape::roundedRect, true, area);
        LowLevelGraphicsSoftwareRenderer::fillRoundedRectangle(area, corner);
    }
    void drawRoundedRectangle(const juce::Rectangle<float>& area, float corner, float thickness) override {
        note(Mark::Shape::roundedRect, false, area);
        LowLevelGraphicsSoftwareRenderer::drawRoundedRectangle(area, corner, thickness);
    }
    void drawRect(const juce::Rectangle<float>& area, float thickness) override {
        note(Mark::Shape::rect, false, area);
        LowLevelGraphicsSoftwareRenderer::drawRect(area, thickness);
    }

private:
    struct State {
        juce::AffineTransform transform;
        float alpha = 1.0f;
    };
    State& top() { return states_.back(); }
    void pop() {
        if (states_.size() > 1)
            states_.pop_back();
    }
    void note(Mark::Shape shape, bool filled, juce::Rectangle<float> area) {
        if (top().alpha <= 0.02f)
            return; // invisible: not a marker anyone sees
        out_.push_back({shape, filled, area.transformedBy(top().transform), top().alpha});
    }

    std::vector<Mark>& out_;
    std::vector<State> states_{State{}};
};

} // namespace

bool Mark::isPointMarker() const {
    if (shape == Shape::rect)
        return false;
    const float w = area.getWidth(), h = area.getHeight();
    return std::abs(w - h) < 0.75f && w >= 2.0f && w <= kMaxMarkerPx;
}

std::string Mark::describe() const {
    std::ostringstream s;
    s << (filled ? "filled " : "stroked ")
      << (shape == Shape::ellipse       ? "ellipse"
          : shape == Shape::roundedRect ? "rounded rect"
                                        : "rect")
      << " centred at (" << area.getCentreX() << ", " << area.getCentreY() << ") size " << area.getWidth();
    return s.str();
}

std::vector<Mark> paintMarks(juce::Component& c) {
    std::vector<Mark> marks;
    juce::Image image(juce::Image::ARGB, std::max(1, c.getWidth()), std::max(1, c.getHeight()), true,
                      juce::SoftwareImageType());
    synth::ui::setFocusRingsShownForTest(true);
    {
        Recorder recorder(image, marks);
        juce::Graphics g(recorder);
        c.paintEntireComponent(g, false);
    }
    synth::ui::setFocusRingsShownForTest(false);
    return marks;
}

PointReport inspectPoint(const std::vector<Mark>& marks, juce::Point<float> expected, juce::Point<float> stale) {
    PointReport report;
    float best = 1.5f;
    float dotWidth = 0.0f;
    for (const auto& m : marks)
        if (m.isPointMarker() && m.filled && m.shape == Mark::Shape::ellipse &&
            m.area.getCentre().getDistanceFrom(expected) <= best) {
            best = m.area.getCentre().getDistanceFrom(expected);
            report.dot = m.area.getCentre();
            report.dotFound = true;
        }
    if (!report.dotFound)
        return report;
    for (const auto& m : marks)
        if (m.isPointMarker() && m.filled && m.shape == Mark::Shape::ellipse &&
            m.area.getCentre().getDistanceFrom(report.dot) <= kOnDotPx)
            dotWidth = dotWidth == 0.0f ? m.area.getWidth() : std::min(dotWidth, m.area.getWidth());
    for (const auto& m : marks) {
        if (!m.isPointMarker())
            continue;
        const auto centre = m.area.getCentre();
        if (centre.getDistanceFrom(report.dot) <= kOnDotPx) {
            ++report.onDot;
            if (m.area.getWidth() > dotWidth + 1.0f)
                ++report.ringsOnDot;
        } else if ((centre.getDistanceFrom(report.dot) <= kNearDotPx && m.area.getWidth() >= dotWidth - 0.5f) ||
                   centre.getDistanceFrom(stale) <= kNearStalePx) {
            // Near the dot only a ring-sized mark can be one of its markers drawn astray; smaller ones are the
            // neighbours' (a curve's bend handles, the next node).
            report.strays.push_back(m);
        }
    }
    return report;
}

void expectMarkersOnDot(const std::vector<Mark>& marks, juce::Point<float> expected, juce::Point<float> stale,
                        int minRings, const std::string& what) {
    SCOPED_TRACE(what);
    const auto report = inspectPoint(marks, expected, stale);
    ASSERT_TRUE(report.dotFound) << "no dot drawn at (" << expected.x << ", " << expected.y << ")";
    for (const auto& m : report.strays)
        ADD_FAILURE() << "a marker is off the dot at (" << report.dot.x << ", " << report.dot.y
                      << "): " << m.describe();
    EXPECT_GE(report.ringsOnDot, minRings) << "the rings that should sit on the dot were not all painted there";
}

void expectBoxAroundDots(const std::vector<Mark>& marks, const std::vector<juce::Point<float>>& dots,
                         const std::string& what) {
    SCOPED_TRACE(what);
    ASSERT_FALSE(dots.empty());
    const Mark* box = nullptr;
    for (const auto& m : marks)
        if (m.shape == Mark::Shape::rect && !m.filled && m.area.getWidth() > kMaxMarkerPx &&
            (box == nullptr || m.area.getWidth() * m.area.getHeight() > box->area.getWidth() * box->area.getHeight()))
            box = &m;
    ASSERT_NE(box, nullptr) << "no selection box was painted";
    auto bounds = juce::Rectangle<float>(dots.front(), dots.front());
    for (const auto& d : dots)
        bounds = juce::Rectangle<float>::leftTopRightBottom(std::min(bounds.getX(), d.x), std::min(bounds.getY(), d.y),
                                                            std::max(bounds.getRight(), d.x),
                                                            std::max(bounds.getBottom(), d.y));
    EXPECT_NEAR(box->area.getCentreX(), bounds.getCentreX(), 1.0) << "the box is not around the drawn dots";
    EXPECT_NEAR(box->area.getCentreY(), bounds.getCentreY(), 1.0) << "the box is not around the drawn dots";
}

} // namespace point_marker_test
