// TimelineDocStretchOriginSerialization.cpp -- the saved form of Clip::stretchOrigin: an optional "stretchOrigin"
// object on an audio clip. A project saved before it existed loads with none.
#include "TimelineDocInternal.h"

namespace synth {
namespace detail {

bool isValidStretchOrigin(const Clip::StretchOrigin& origin) {
    return origin.assetRef.isNotEmpty() && isValidAssetRefString(origin.assetRef) &&
           isFiniteAtOrAfterZero(origin.offsetSeconds) && isFinitePositive(origin.ratio);
}

void writeClipStretchOrigin(juce::DynamicObject& obj, const Clip& clip) {
    if (!clip.stretchOrigin || !isValidStretchOrigin(*clip.stretchOrigin))
        return;
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("assetRef", clip.stretchOrigin->assetRef);
    o->setProperty("offsetSeconds", clip.stretchOrigin->offsetSeconds);
    o->setProperty("ratio", clip.stretchOrigin->ratio);
    obj.setProperty("stretchOrigin", juce::var(o.get()));
}

// The origin only improves a later stretch, so anything unreadable or unsafe loads as none, never a failed load.
void readClipStretchOrigin(const juce::DynamicObject& obj, Clip& clip) {
    clip.stretchOrigin = std::nullopt;
    const auto* o = obj.getProperty("stretchOrigin").getDynamicObject();
    if (o == nullptr || clip.assetRef.isEmpty())
        return;
    const juce::var ref = o->getProperty("assetRef");
    const juce::var offset = o->getProperty("offsetSeconds");
    const juce::var ratio = o->getProperty("ratio");
    if (!ref.isString() || !(offset.isDouble() || offset.isInt() || offset.isInt64()) ||
        !(ratio.isDouble() || ratio.isInt() || ratio.isInt64()))
        return;
    const Clip::StretchOrigin origin{ref.toString(), static_cast<double>(offset), static_cast<double>(ratio)};
    if (isValidStretchOrigin(origin))
        clip.stretchOrigin = origin;
}

} // namespace detail
} // namespace synth
