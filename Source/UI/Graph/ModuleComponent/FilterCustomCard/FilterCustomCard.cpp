// FilterCustomCard.cpp -- the Filter card's Custom section: what shows when, the layout, and the editor window.
#include "FilterCustomCard.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMapWidth = 252;
constexpr int kMapHeight = 150;
constexpr float kCursorEpsilon = 1.0e-4f;

int blockHeight(const synth::CardBlockFade& block, bool settling, int full) {
    return block.isAttached() ? block.height(full, settling) : full;
}
} // namespace

FilterCustomCard::FilterCustomCard(Host host, FilterModule& module, std::shared_ptr<FilterShapesAdapter> adapter)
    : host_(std::move(host))
    , module_(module)
    , adapter_(std::move(adapter)) {
    controller_ = std::make_unique<ShapeMapController>(map_, *adapter_);
    controller_->onEdit = [this](int index) { openEditor(index); };
    controller_->onChanged = [this] {
        lastGeneration_ = adapter_->getGeneration();
        sync(); // the count may have crossed the map's default
    };
    map_.showMenu = host_.showMenu;
    map_.setShowLabels(false);
    map_.setCursorVisible(true);
    controller_->refresh();

    editButton_.setTooltip("Edit shapes: open the shape editor window");
    editButton_.setTitle("Edit shapes");
    editButton_.onClick = [this] { openEditor(nearestShapeToCursor()); };
    toggle_.setTooltip("Show Morph Map: the map of shapes and the morph cursor under the response view");
    toggle_.onClick = [this] {
        const bool show = toggle_.getToggleState();
        host_.rememberView([show](synth::CardViewState& state) { state.showMorphMap = show; });
        sync();
    };
    for (juce::Component* part : {static_cast<juce::Component*>(&map_), static_cast<juce::Component*>(&editButton_),
                                  static_cast<juce::Component*>(&toggle_)})
        host_.card->addChildComponent(part);

    auto attach = [this](synth::CardBlockFade& fade, std::vector<juce::Component*> targets) {
        fade.attach(
            std::move(targets), [this] { host_.relayout(); },
            [this] {
                host_.relayout();
                host_.makeRoom();
            });
    };
    attach(editFade_, {&editButton_, &toggle_});
    attach(mapFade_, {&map_});
    lastGeneration_ = adapter_->getGeneration();
}

FilterCustomCard::~FilterCustomCard() { detach(); }

void FilterCustomCard::detach() {
    if (window_ != nullptr)
        delete window_.getComponent();
    window_ = nullptr;
    editFade_.reset();
    mapFade_.reset();
}

bool FilterCustomCard::customActive() const { return module_.getCurrentFilterType() == FilterModule::kCustomTypeIndex; }

/** The remembered choice, else shown from two shapes up (one shape has nothing to arrange). */
bool FilterCustomCard::mapWanted() const {
    const auto state = module_.getCardViewState();
    return state.showMorphMap.value_or(module_.getCustomShapes().shapes.size() >= 2);
}

/** In Custom the button and the pill are there and the map follows the choice; its sliders stand in for the Morph X
    and Y knobs, which are hidden while it shows and back when it is hidden, so the module's CV targets always have a
    landing widget. */
void FilterCustomCard::sync() {
    const bool custom = customActive();
    lastCustom_ = custom;
    const bool wanted = mapWanted();
    toggle_.setToggleState(wanted, juce::dontSendNotification);
    editFade_.setShown(custom, *host_.settling, host_.relayout, host_.makeRoom);
    mapFade_.setShown(custom && wanted, *host_.settling, host_.relayout, host_.makeRoom);
    host_.hideMorphKnobs(custom && wanted);
}

void FilterCustomCard::poll() {
    // The type (combo, automation, undo, a preset) is read here rather than from the parameter callback, which may
    // arrive on the audio thread.
    if (const bool custom = customActive(); custom != lastCustom_) {
        lastCustom_ = custom;
        sync();
    }
    if (const int generation = adapter_->getGeneration(); generation != lastGeneration_) {
        lastGeneration_ = generation;
        controller_->refresh();
        sync();
    }
    if (std::abs(map_.getCursor().x - module_.getCurrentMorphX()) > kCursorEpsilon ||
        std::abs(map_.getCursor().y - module_.getCurrentMorphY()) > kCursorEpsilon)
        map_.setCursor(module_.getCurrentMorphX(), module_.getCurrentMorphY());
}

int FilterCustomCard::layout(int y, int x, int width, bool apply) {
    if (!editButton_.isVisible())
        return y;
    const bool settling = *host_.settling;
    if (apply) {
        const int w = juce::jmin(width, synth::cardbody::kNarrowContentWidth);
        editButton_.setBounds(x + (width - w) / 2, y, w, blockHeight(editFade_, settling, synth::cardbody::kRowHeight));
    }
    y += blockHeight(editFade_, settling, synth::cardbody::kRowHeight + 2);
    if (mapFade_.isAttached() && map_.isVisible()) {
        if (apply) {
            const int w = juce::jmin(width, kMapWidth);
            map_.setBounds(x + (width - w) / 2, y, w, blockHeight(mapFade_, settling, kMapHeight));
        }
        y += blockHeight(mapFade_, settling, kMapHeight + 8);
    }
    return y;
}

/** One window per card; asking again selects the shape in the open one. The adapter's title follows a rename. */
void FilterCustomCard::openEditor(int shapeIndex) {
    if (window_ != nullptr) {
        window_->toFront(true);
        if (auto* editor = dynamic_cast<ShapeListEditor*>(window_->getContentComponent()))
            editor->setSelected(shapeIndex);
        return;
    }
    adapter_->setModuleName(host_.title());
    window_ = openShapeEditorWindow(adapter_, shapeIndex, host_.card);
}

int FilterCustomCard::nearestShapeToCursor() const {
    const auto& shapes = module_.getCustomShapes().shapes;
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int i = 0; i < (int)shapes.size(); ++i) {
        const auto& p = shapes[(size_t)i].position;
        const float dx = (p.size() > 0 ? p[0] : 0.5f) - module_.getCurrentMorphX();
        const float dy = (p.size() > 1 ? p[1] : 0.5f) - module_.getCurrentMorphY();
        if (const float d = dx * dx + dy * dy; d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------------------
// Cables
// ---------------------------------------------------------------------------------------------------------------

bool FilterCustomCard::isMapShown() const { return mapFade_.isShown() && map_.isVisible(); }

std::optional<int> FilterCustomCard::channelAt(juce::Point<int> cardPoint) const {
    if (!isMapShown())
        return std::nullopt;
    const auto axis = map_.stripAt((cardPoint - map_.getPosition()).toFloat());
    if (axis == MorphMap::Axis::X)
        return FilterModule::kMorphXCVChannel;
    if (axis == MorphMap::Axis::Y)
        return FilterModule::kMorphYCVChannel;
    return std::nullopt;
}

static MorphMap::Axis axisForChannel(int channel) {
    return channel == FilterModule::kMorphXCVChannel   ? MorphMap::Axis::X
           : channel == FilterModule::kMorphYCVChannel ? MorphMap::Axis::Y
                                                       : MorphMap::Axis::None;
}

std::optional<juce::Point<float>> FilterCustomCard::anchorFor(int channel) const {
    const auto axis = axisForChannel(channel);
    if (axis == MorphMap::Axis::None || !isMapShown())
        return std::nullopt;
    return map_.getPosition().toFloat() + map_.stripAnchor(axis);
}

juce::Rectangle<int> FilterCustomCard::stripBoundsFor(int channel) const {
    return map_.getStripBounds(axisForChannel(channel)).toNearestInt() + map_.getPosition();
}

void FilterCustomCard::setDropChannel(int channel) {
    map_.setDropHighlight(isMapShown() ? axisForChannel(channel) : MorphMap::Axis::None);
}

} // namespace synth::ui
