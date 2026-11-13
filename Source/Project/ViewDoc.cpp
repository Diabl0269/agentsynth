#include "Project/ViewDoc.h"
#include <cmath>

namespace synth {

namespace {
constexpr const char* kZoom = "zoom";
constexpr const char* kPanX = "panX";
constexpr const char* kPanY = "panY";

bool readNumber(const juce::DynamicObject& obj, const char* key, double& out) {
    const auto v = obj.getProperty(key);
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}
} // namespace

juce::var ViewDoc::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty(kZoom, static_cast<double>(zoom));
    obj->setProperty(kPanX, static_cast<double>(panX));
    obj->setProperty(kPanY, static_cast<double>(panY));
    return juce::var(obj);
}

// Strict about shape (every key present, a finite number), forgiving about range: a hand-edited zoom of 50
// is still a usable view once clamped, whereas a string or a missing key means the file is not ours.
bool ViewDoc::fromVar(const juce::var& v) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    double z = 0.0, px = 0.0, py = 0.0;
    if (!readNumber(*obj, kZoom, z) || !readNumber(*obj, kPanX, px) || !readNumber(*obj, kPanY, py))
        return false;

    zoom = juce::jlimit(kMinZoom, kMaxZoom, static_cast<float>(z));
    panX = juce::jlimit(-kMaxPan, kMaxPan, static_cast<float>(px));
    panY = juce::jlimit(-kMaxPan, kMaxPan, static_cast<float>(py));
    return true;
}

bool ViewDoc::operator==(const ViewDoc& o) const noexcept { return zoom == o.zoom && panX == o.panX && panY == o.panY; }

} // namespace synth
