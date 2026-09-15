// Concern: FRO16 (P9-10) -- MixerEqThumbnail's listener/cache lifecycle and its curve paint.
#include "MixerEqThumbnail.h"

#include "EqResponseCurve.h"
#include "UI/ModuleViews/FrequencyGrid.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

int MixerEqThumbnail::liveUnbindCallCountForTest_ = 0;

MixerEqThumbnail::MixerEqThumbnail() { setInterceptsMouseClicks(true, false); }

MixerEqThumbnail::~MixerEqThumbnail() {
    // Same ordering as MixerFader's own destructor discipline (unbind before the module pointer
    // can go stale): cancel any queued recompute first, then drop every listener registration.
    cancelPendingUpdate();
    detachListeners();
}

void MixerEqThumbnail::setEqModule(ParametricEQModule* eq, juce::AudioProcessorGraph* graph,
                                   juce::AudioProcessorGraph::NodeID nodeId) {
    if (eq == eq_)
        return;

    cancelPendingUpdate();
    detachListeners();
    eq_ = eq;
    graph_ = graph;
    nodeId_ = nodeId;

    if (eq_ == nullptr) {
        cachedMagnitudesDb_.clear();
        setVisible(false);
        return;
    }

    for (auto* param : eq_->getParameters())
        if (param != nullptr)
            param->addListener(this);

    setVisible(true);
    recompute(); // synchronous, so the first paint after binding already has data
}

void MixerEqThumbnail::detachListeners() {
    if (eq_ == nullptr)
        return;
    // Belt-and-braces (see the header's own comment): if the caller gave us liveness info and the
    // node is already gone from the graph, its parameters were freed along with it -- touching
    // eq_->getParameters() here would be the exact use-after-free this whole mechanism exists to
    // prevent. A null graph_ (no liveness info available) falls through to the unconditional
    // detach every caller got before this existed. Checked BEFORE the live-unbind counter bumps,
    // not after -- getLiveUnbindCallCountForTest() must count only a detach that actually touched
    // live parameters, or a test could pass on this defensive no-op instead of on a caller (e.g.
    // MixerInsertList::onBeforeNodeRemoved) actually unbinding before the node was freed.
    if (graph_ != nullptr && graph_->getNodeForId(nodeId_) == nullptr)
        return;
    ++liveUnbindCallCountForTest_;
    for (auto* param : eq_->getParameters())
        if (param != nullptr)
            param->removeListener(this);
}

void MixerEqThumbnail::parameterValueChanged(int, float) {
    // May land on any thread -- a CV-modulated band's resolved value is written from the audio
    // thread. The only thing this callback may safely do is coalesce onto the message thread.
    triggerAsyncUpdate();
}

void MixerEqThumbnail::parameterGestureChanged(int, bool) {
    // No undo bracketing lives here -- the thumbnail is read-only display, not a control.
}

void MixerEqThumbnail::handleAsyncUpdate() {
    recompute();
    repaint();
}

void MixerEqThumbnail::recompute() {
    if (eq_ == nullptr)
        return;
    cachedMagnitudesDb_ = EqResponseCurve::compute(*eq_);
    cachedBypassed_ = eq_->isBypassed();
    ++recomputeCount_;
}

void MixerEqThumbnail::mouseUp(const juce::MouseEvent&) {
    if (onClicked)
        onClicked();
}

void MixerEqThumbnail::paint(juce::Graphics& g) {
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());
    if (w <= 0.0f || h <= 0.0f || cachedMagnitudesDb_.empty())
        return;

    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto surface = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    const auto disabled = laf != nullptr ? laf->getTheme().colors.textDisabled : juce::Colour(0xff5C6470);

    g.setColour(surface);
    g.fillRect(getLocalBounds());

    // Bypassed reads dimmed -- MixerInsertList::paint's own bypassed convention (textDisabled,
    // here also at reduced alpha since this is a filled shape rather than text).
    const auto fillColour = cachedBypassed_ ? disabled.withAlpha(0.4f) : accent.withAlpha(0.4f);

    const int numPoints = (int)cachedMagnitudesDb_.size();
    const float zeroY =
        juce::jlimit(0.0f, h, FrequencyGrid::dbToY(0.0f, h, EqResponseCurve::kMinDb, EqResponseCurve::kMaxDb));

    juce::Path fillPath;
    fillPath.startNewSubPath(0.0f, zeroY);
    for (int i = 0; i < numPoints; ++i) {
        const float x = FrequencyGrid::freqToX(FrequencyGrid::indexToFreq(i, numPoints), w);
        const float y = juce::jlimit(
            0.0f, h,
            FrequencyGrid::dbToY(cachedMagnitudesDb_[(size_t)i], h, EqResponseCurve::kMinDb, EqResponseCurve::kMaxDb));
        fillPath.lineTo(x, y);
    }
    fillPath.lineTo(w, zeroY);
    fillPath.closeSubPath();

    g.setColour(fillColour);
    g.fillPath(fillPath);
}

} // namespace synth::ui
