// ToolbarAiSpark.cpp -- the AI button's pulse, glow and spark: keyframes of one 2.8 s cycle and their
// drawing.
#include "ToolbarAiSpark.h"

namespace synth::ui {

namespace {
constexpr float kRestSparkScale = 0.7f;
constexpr float kPulseRadius = 1.7f;
constexpr juce::Point<float> kPlugGlowCentre{17.5f, 7.6f};
constexpr float kPlugGlowRadius = 3.0f;
constexpr juce::Point<float> kSparkCentre{19.2f, 5.0f};
constexpr float kSparkRadius = 3.6f;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// CSS cubic-bezier(x1, y1, x2, y2): the eased value at time fraction `x`.
float cubicBezier(float x1, float y1, float x2, float y2, float x) {
    auto at = [](float a, float b, float t) {
        const float u = 1.0f - t;
        return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
    };
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hi);
        (at(x1, x2, mid) < x ? lo : hi) = mid;
    }
    return at(y1, y2, 0.5f * (lo + hi));
}

// The spark's keyframes, with the bounce (1.4 overshoot) easing each segment.
float bounce(float x) { return cubicBezier(0.34f, 1.4f, 0.64f, 1.0f, x); }

struct SparkKey {
    float at, scale, degrees, alpha;
};
constexpr SparkKey kSparkKeys[] = {
    {0.00f, 0.15f, 0.0f, 0.0f},  {0.48f, 0.15f, 0.0f, 0.0f},  {0.64f, 1.12f, 30.0f, 1.0f},
    {0.82f, 0.95f, 45.0f, 1.0f}, {1.00f, 0.40f, 70.0f, 0.0f},
};

// Linear keyframes through (phase, value) pairs.
float ramp(float phase, std::initializer_list<std::pair<float, float>> keys) {
    const std::pair<float, float>* prev = keys.begin();
    for (const auto& k : keys) {
        if (phase <= k.first)
            return k.first <= prev->first
                       ? k.second
                       : lerp(prev->second, k.second, (phase - prev->first) / (k.first - prev->first));
        prev = &k;
    }
    return prev->second;
}
} // namespace

ToolbarAiFrame toolbarAiFrame(float phase) {
    phase = juce::jlimit(0.0f, 1.0f, phase);
    ToolbarAiFrame f;
    f.pulseAlong = juce::jlimit(0.0f, 1.0f, phase / 0.5f);
    f.pulseAlpha = ramp(phase, {{0.0f, 0.0f}, {0.06f, 1.0f}, {0.5f, 1.0f}, {0.58f, 0.0f}, {1.0f, 0.0f}});
    f.glowAlpha = ramp(phase, {{0.0f, 0.0f}, {0.45f, 0.0f}, {0.58f, 0.9f}, {1.0f, 0.0f}});
    for (size_t i = 1; i < std::size(kSparkKeys); ++i) {
        const auto& a = kSparkKeys[i - 1];
        const auto& b = kSparkKeys[i];
        if (phase <= b.at) {
            const float e = bounce((phase - a.at) / (b.at - a.at));
            f.sparkScale = lerp(a.scale, b.scale, e);
            f.sparkDegrees = lerp(a.degrees, b.degrees, e);
            f.sparkAlpha = juce::jlimit(0.0f, 1.0f, lerp(a.alpha, b.alpha, e));
            break;
        }
    }
    return f;
}

ToolbarAiFrame toolbarAiRestFrame() {
    ToolbarAiFrame f;
    f.sparkScale = kRestSparkScale;
    return f;
}

ToolbarAiFrame toolbarAiBlend(const ToolbarAiFrame& rest, const ToolbarAiFrame& playing, float amount) {
    ToolbarAiFrame f;
    f.pulseAlong = playing.pulseAlong;
    f.pulseAlpha = lerp(rest.pulseAlpha, playing.pulseAlpha, amount);
    f.glowAlpha = lerp(rest.glowAlpha, playing.glowAlpha, amount);
    f.sparkScale = lerp(rest.sparkScale, playing.sparkScale, amount);
    f.sparkDegrees = lerp(rest.sparkDegrees, playing.sparkDegrees, amount);
    f.sparkAlpha = lerp(rest.sparkAlpha, playing.sparkAlpha, amount);
    return f;
}

const juce::Path& toolbarAiCablePath() {
    static const juce::Path path = [] {
        juce::Path p;
        p.startNewSubPath(3.0f, 21.0f);
        p.cubicTo(3.0f, 14.0f, 8.0f, 18.0f, 12.0f, 13.0f);
        return p;
    }();
    return path;
}

juce::Point<float> toolbarAiPulsePoint(float along) {
    const auto& path = toolbarAiCablePath();
    return path.getPointAlongPath(juce::jlimit(0.0f, 1.0f, along) * path.getLength());
}

void paintToolbarAiParts(juce::Graphics& g, const juce::AffineTransform& iconToScreen, const ToolbarAiFrame& frame,
                         const ToolbarAiColours& colours, float alpha) {
    if (frame.glowAlpha > 0.0f) {
        const auto c = kPlugGlowCentre.transformedBy(iconToScreen);
        const auto edge = kPlugGlowCentre.translated(kPlugGlowRadius, 0.0f).transformedBy(iconToScreen);
        const auto base = colours.glow.withMultipliedAlpha(frame.glowAlpha * alpha);
        g.setGradientFill(juce::ColourGradient(base, c, base.withMultipliedAlpha(0.25f), edge, true));
        juce::Path disc;
        disc.addEllipse(kPlugGlowCentre.x - kPlugGlowRadius, kPlugGlowCentre.y - kPlugGlowRadius, 2 * kPlugGlowRadius,
                        2 * kPlugGlowRadius);
        g.fillPath(disc, iconToScreen);
    }
    if (frame.pulseAlpha > 0.0f) {
        const auto p = toolbarAiPulsePoint(frame.pulseAlong);
        juce::Path dot;
        dot.addEllipse(p.x - kPulseRadius, p.y - kPulseRadius, 2 * kPulseRadius, 2 * kPulseRadius);
        g.setColour(colours.pulse.withMultipliedAlpha(frame.pulseAlpha * alpha));
        g.fillPath(dot, iconToScreen);
    }
    if (frame.sparkAlpha > 0.0f && frame.sparkScale > 0.0f) {
        // A four-point star with concave sides, turned and scaled about its own centre.
        juce::Path star;
        star.startNewSubPath(0.0f, -1.0f);
        const juce::Point<float> pts[] = {{0.3f, -0.3f}, {1.0f, 0.0f},  {0.3f, 0.3f},  {0.0f, 1.0f},
                                          {-0.3f, 0.3f}, {-1.0f, 0.0f}, {-0.3f, -0.3f}};
        for (const auto& p : pts)
            star.lineTo(p);
        star.closeSubPath();
        star.applyTransform(juce::AffineTransform::scale(kSparkRadius * frame.sparkScale)
                                .rotated(juce::degreesToRadians(frame.sparkDegrees))
                                .translated(kSparkCentre.x, kSparkCentre.y));
        g.setColour(colours.spark.withMultipliedAlpha(frame.sparkAlpha * alpha));
        g.fillPath(star, iconToScreen);
    }
}

} // namespace synth::ui
