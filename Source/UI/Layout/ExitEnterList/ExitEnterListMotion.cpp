// ExitEnterListMotion.cpp: the overlay that draws an ExitEnterListPlan and the clock that steps it.
#include "ExitEnterListMotion.h"

#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kOutlineRadius = 3.0f;
}

class ExitEnterListOverlay final : public juce::Component {
public:
    explicit ExitEnterListOverlay(ExitEnterListMotion::Job job)
        : job_(std::move(job)) {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
        setAccessible(false);
        setOpaque(true);
        setBounds(job_.area);
    }

    void setFrame(const ExitEnterTimeline::Frame& frame, bool reduced) {
        frame_ = frame;
        reduced_ = reduced;
        repaint();
    }

    // The slot of `item` as it is drawn now, in this component's coordinates (before scaling into the slot).
    juce::Rectangle<float> slotOf(const ExitEnterListPlan::Drawn& d) const {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        return job_.axis == ListAxis::Vertical ? juce::Rectangle<float>(0.0f, d.start, w, d.extent)
                                               : juce::Rectangle<float>(d.start, 0.0f, d.extent, h);
    }

    static juce::Rectangle<float> scaled(juce::Rectangle<float> r, float s) {
        const auto c = r.getCentre();
        return juce::Rectangle<float>(r.getWidth() * s, r.getHeight() * s).withCentre(c);
    }

    // Where the slice is drawn now, in this component's coordinates.
    std::optional<juce::Rectangle<float>> rectOf(const ExitEnterListItem& item) const {
        const auto d = ExitEnterListPlan::drawnAt(item, frame_, reduced_);
        if (!d)
            return std::nullopt;
        return scaled(slotOf(*d), d->scale);
    }

    const ExitEnterListItem* itemFor(const juce::String& key) const {
        const auto it = std::find_if(job_.items.begin(), job_.items.end(),
                                     [&key](const ExitEnterListItem& i) { return i.key == key; });
        return it == job_.items.end() ? nullptr : &*it;
    }

    ExitEnterTimeline::Frame frame() const noexcept { return frame_; }
    const std::vector<ExitEnterListItem>& items() const noexcept { return job_.items; }

    void paint(juce::Graphics& g) override {
        g.fillAll(job_.background);
        if (job_.picture.isNull())
            return;
        const float px = job_.pictureScale;
        for (const auto& item : job_.items) {
            const auto d = ExitEnterListPlan::drawnAt(item, frame_, reduced_);
            if (!d || item.extent <= 0.0f)
                continue;
            const auto slot = slotOf(*d);
            if (d->outlineAlpha > 0.0f) {
                g.setColour(job_.accent.withAlpha(d->outlineAlpha));
                g.drawRoundedRectangle(slot.reduced(0.5f), kOutlineRadius, 1.0f);
            }
            const auto src =
                job_.axis == ListAxis::Vertical
                    ? juce::Rectangle<float>(0.0f, item.srcStart, static_cast<float>(getWidth()), item.extent)
                    : juce::Rectangle<float>(item.srcStart, 0.0f, item.extent, static_cast<float>(getHeight()));
            const auto pixels = (src * px).getSmallestIntegerContainer().getIntersection(job_.picture.getBounds());
            if (pixels.isEmpty())
                continue;
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(slot.getSmallestIntegerContainer());
            g.setOpacity(d->alpha);
            g.drawImage(job_.picture.getClippedImage(pixels), scaled(slot, d->scale),
                        juce::RectanglePlacement::stretchToFit);
        }
    }

private:
    ExitEnterListMotion::Job job_;
    ExitEnterTimeline::Frame frame_;
    bool reduced_ = false;
};

ExitEnterListMotion::ExitEnterListMotion(juce::Component& host)
    : host_(host)
    , updater_(&host) {}

ExitEnterListMotion::~ExitEnterListMotion() {
    driver_.stop(updater_);
    finishNow();
}

juce::Image ExitEnterListMotion::pictureOf(juce::Component& source, juce::Rectangle<int> area, float& scaleOut) {
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    scaleOut = displays.getPrimaryDisplay() != nullptr ? static_cast<float>(displays.getPrimaryDisplay()->scale) : 1.0f;
    scaleOut = std::clamp(scaleOut, 1.0f, 2.0f);
    return source.createComponentSnapshot(area, true, scaleOut);
}

bool ExitEnterListMotion::start(Job job) {
    driver_.stop(updater_);
    finishNow();
    timeline_ = ExitEnterListPlan::timelineFor(job.items);
    if (timeline_.totalMs() <= 0.0 || job.area.isEmpty() || job.picture.isNull())
        return false;
    reduced_ = prefersReducedMotion();
    overlay_ = std::make_unique<ExitEnterListOverlay>(std::move(job));
    host_.addAndMakeVisible(*overlay_);
    overlay_->toFront(false);
    applyAtMs(0.0);
    const double total = timeline_.totalMs();
    driver_.start(
        updater_, total, [](float t) { return t; },
        [this, total](float t) { applyAtMs(static_cast<double>(t) * total); }, [this] { finishNow(); });
    return overlay_ != nullptr;
}

void ExitEnterListMotion::finishNow() {
    if (overlay_ == nullptr)
        return;
    host_.removeChildComponent(overlay_.get());
    overlay_.reset();
    host_.repaint();
}

void ExitEnterListMotion::applyAtMs(double elapsedMs) {
    if (overlay_ != nullptr)
        overlay_->setFrame(timeline_.at(elapsedMs), reduced_);
}

juce::Component* ExitEnterListMotion::overlayComponent() const noexcept { return overlay_.get(); }

int ExitEnterListMotion::exitGhostCount() const {
    if (overlay_ == nullptr)
        return 0;
    return static_cast<int>(std::count_if(overlay_->items().begin(), overlay_->items().end(), [this](const auto& i) {
        return i.role == ExitEnterListItem::Role::Exit &&
               ExitEnterListPlan::drawnAt(i, overlay_->frame(), reduced_).has_value();
    }));
}

int ExitEnterListMotion::enterGhostCount() const {
    if (overlay_ == nullptr)
        return 0;
    return static_cast<int>(std::count_if(overlay_->items().begin(), overlay_->items().end(), [this](const auto& i) {
        return i.role == ExitEnterListItem::Role::Enter &&
               ExitEnterListPlan::drawnAt(i, overlay_->frame(), reduced_).has_value();
    }));
}

std::optional<juce::Rectangle<float>> ExitEnterListMotion::drawnRectFor(const juce::String& key) const {
    if (overlay_ == nullptr)
        return std::nullopt;
    const auto* item = overlay_->itemFor(key);
    if (item == nullptr)
        return std::nullopt;
    const auto rect = overlay_->rectOf(*item);
    if (!rect)
        return std::nullopt;
    return rect->translated(static_cast<float>(overlay_->getX()), static_cast<float>(overlay_->getY()));
}

float ExitEnterListMotion::outlineAlphaFor(const juce::String& key) const {
    if (overlay_ == nullptr)
        return 0.0f;
    const auto* item = overlay_->itemFor(key);
    if (item == nullptr)
        return 0.0f;
    const auto d = ExitEnterListPlan::drawnAt(*item, overlay_->frame(), reduced_);
    return d ? d->outlineAlpha : 0.0f;
}

} // namespace synth::ui
