// MorphShapesCard.cpp -- a card's morph-map section: what shows when, the layout, the cables and the editor window.
#include "MorphShapesCard.h"
#include "Modules/Shapes/MorphShapes.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMapWidth = 252;
// 150 plus the 9 px the landing dots and modulation bars take from the plot, so the plot keeps its height.
constexpr int kMapHeight = 159;
constexpr float kCursorEpsilon = 1.0e-4f;

int blockHeight(const synth::CardBlockFade& block, bool settling, int full) {
    return block.isAttached() ? block.height(full, settling) : full;
}
} // namespace

MorphShapesCard::MorphShapesCard(Host host, ModuleBase& module, std::shared_ptr<ModuleShapesAdapter> adapter,
                                 int morphXChannel, int morphYChannel)
    : host_(std::move(host))
    , module_(module)
    , adapter_(std::move(adapter))
    , morphXChannel_(morphXChannel)
    , morphYChannel_(morphYChannel) {
    controller_ = std::make_unique<ShapeMapController>(map_, *adapter_);
    controller_->onEdit = [this](int index) { openEditor(index); };
    controller_->onChanged = [this] {
        lastGeneration_ = adapter_->getGeneration();
        sync(); // the count may have crossed the map's default
    };
    map_.showMenu = host_.showMenu;
    map_.setShowLabels(false);
    map_.setLandingDotDiameter(host_.landingDotDiameter);
    map_.pressGesture = [this](const juce::MouseEvent& e, int phase) { return routeMapPress(e, phase); };
    map_.onStripHover = [this](MorphMap::Axis axis) { routeStripHover(axis); };
    map_.extraTooltip = [this](juce::Point<float> point) { return stripTooltipAt(point); };
    map_.setCursorVisible(true);
    controller_->refresh();

    editButton_.setTooltip("Edit shapes: open the shape editor window");
    editButton_.setTitle("Edit shapes");
    editButton_.onClick = [this] { openEditor(nearestShapeToCursor()); };
    toggle_.setTooltip("Show Morph Map: the map of shapes and the morph cursor " + host_.mapPlace);
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

MorphShapesCard::~MorphShapesCard() { detach(); }

void MorphShapesCard::detach() {
    if (window_ != nullptr)
        delete window_.getComponent();
    window_ = nullptr;
    editFade_.reset();
    mapFade_.reset();
}

/** The remembered choice, else shown from two shapes up (one shape has nothing to arrange). */
bool MorphShapesCard::mapWanted() const {
    const auto state = module_.getCardViewState();
    return state.showMorphMap.value_or(getNumShapes() >= 2);
}

/** In Custom the button and the pill are there and the map follows the choice; its sliders stand in for the Morph X
    and Y knobs, which are hidden while it shows and back when it is hidden, so the module's CV targets always have a
    landing widget. */
void MorphShapesCard::sync() {
    const bool custom = host_.customActive();
    lastCustom_ = custom;
    const bool wanted = mapWanted();
    toggle_.setToggleState(wanted, juce::dontSendNotification);
    editFade_.setShown(custom, *host_.settling, host_.relayout, host_.makeRoom);
    mapFade_.setShown(custom && wanted, *host_.settling, host_.relayout, host_.makeRoom);
    host_.hideMorphKnobs(custom && wanted);
}

void MorphShapesCard::poll() {
    // The type (combo, automation, undo, a preset) is read here rather than from the parameter callback, which may
    // arrive on the audio thread.
    if (const bool custom = host_.customActive(); custom != lastCustom_) {
        lastCustom_ = custom;
        sync();
    }
    if (const int generation = adapter_->getGeneration(); generation != lastGeneration_) {
        lastGeneration_ = generation;
        controller_->refresh();
        sync();
    }
    pushStripModulation();
    const auto cursor = adapter_->getCursor();
    if (std::abs(map_.getCursor().x - cursor.x) > kCursorEpsilon ||
        std::abs(map_.getCursor().y - cursor.y) > kCursorEpsilon)
        map_.setCursor(cursor.x, cursor.y);
}

int MorphShapesCard::layout(int y, int x, int width, bool apply) {
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
juce::DialogWindow* MorphShapesCard::openEditor(int shapeIndex) {
    if (window_ != nullptr) {
        focusShapeEditorWindow(*window_);
        if (auto* editor = dynamic_cast<ShapeListEditor*>(window_->getContentComponent()))
            editor->setSelected(shapeIndex);
        return window_.getComponent();
    }
    adapter_->setModuleName(host_.title());
    adapter_->setApplicationProperties(host_.appProperties ? host_.appProperties() : nullptr);
    window_ = openShapeEditorWindow(adapter_, shapeIndex, host_.card);
    return window_.getComponent();
}

int MorphShapesCard::getNumShapes() const { return (int)adapter_->getShapes().size(); }

int MorphShapesCard::nearestShapeToCursor() const {
    const auto cursor = adapter_->getCursor();
    return synth::morph::nearestShapeIndex(adapter_->getShapes(), cursor.x, cursor.y);
}

// ---------------------------------------------------------------------------------------------------------------
// Cables
// ---------------------------------------------------------------------------------------------------------------

bool MorphShapesCard::isMapShown() const { return mapFade_.isShown() && map_.isVisible(); }

std::optional<int> MorphShapesCard::channelAt(juce::Point<int> cardPoint) const {
    if (!isMapShown())
        return std::nullopt;
    const auto axis = map_.stripAt((cardPoint - map_.getPosition()).toFloat());
    if (axis == MorphMap::Axis::X)
        return morphXChannel_;
    if (axis == MorphMap::Axis::Y)
        return morphYChannel_;
    return std::nullopt;
}

MorphMap::Axis MorphShapesCard::axisForChannel(int channel) const {
    return channel == morphXChannel_   ? MorphMap::Axis::X
           : channel == morphYChannel_ ? MorphMap::Axis::Y
                                       : MorphMap::Axis::None;
}

int MorphShapesCard::axisChannel(MorphMap::Axis axis) const noexcept {
    return axis == MorphMap::Axis::X ? morphXChannel_ : axis == MorphMap::Axis::Y ? morphYChannel_ : -1;
}

std::optional<juce::Point<float>> MorphShapesCard::anchorFor(int channel) const {
    const auto axis = axisForChannel(channel);
    if (axis == MorphMap::Axis::None || !isMapShown())
        return std::nullopt;
    return map_.getPosition().toFloat() + map_.stripAnchor(axis);
}

juce::Rectangle<int> MorphShapesCard::stripBoundsFor(int channel) const {
    return map_.getStripBounds(axisForChannel(channel)).toNearestInt() + map_.getPosition();
}

void MorphShapesCard::setDropChannel(int channel) {
    map_.setDropHighlight(isMapShown() ? axisForChannel(channel) : MorphMap::Axis::None);
}

juce::Component* MorphShapesCard::stripControlFor(int channel) const {
    const auto axis = axisForChannel(channel);
    return axis == MorphMap::Axis::None || !isMapShown() ? nullptr : map_.getStripStop(axis);
}

CardControlGestures& MorphShapesCard::stripGestures(int channel) {
    return stripGestures_[channel == morphYChannel_ ? 1 : 0];
}

// ---------------------------------------------------------------------------------------------------------------
// Modulation
// ---------------------------------------------------------------------------------------------------------------

/** Offers a press to the strips' gestures in the order a fader's are asked (amount drag, dot, cable pickup); the one
    that claims it gets the drag and the release, and the map does nothing with that press. */
bool MorphShapesCard::routeMapPress(const juce::MouseEvent& e, int phase) {
    if (phase == 0) {
        pressedStrip_ = -1;
        for (int i = 0; i < 2 && pressedStrip_ < 0; ++i)
            if (stripGestures_[i].press(e))
                pressedStrip_ = i;
        return pressedStrip_ >= 0;
    }
    if (pressedStrip_ < 0)
        return false;
    stripGestures_[pressedStrip_].forward(e, phase);
    if (phase == 2)
        pressedStrip_ = -1;
    return true;
}

/** Hovering a slider or its bar hovers its cable, and back, as hovering a knob does. */
void MorphShapesCard::routeStripHover(MorphMap::Axis axis) {
    stripGestures_[0].hover(axis == MorphMap::Axis::X);
    stripGestures_[1].hover(axis == MorphMap::Axis::Y);
}

/** The reading is taken on the card's gated tick; the map repaints only when a bar actually changed. */
void MorphShapesCard::pushStripModulation() {
    const bool live = host_.stripModulation && isMapShown();
    map_.setStripModulation(MorphMap::Axis::X,
                            live ? host_.stripModulation(morphXChannel_) : std::vector<MorphMap::StripModulation>{});
    map_.setStripModulation(MorphMap::Axis::Y,
                            live ? host_.stripModulation(morphYChannel_) : std::vector<MorphMap::StripModulation>{});
}

/** The bar and the landing dot carry the mod dot's tooltip. */
juce::String MorphShapesCard::stripTooltipAt(juce::Point<float> point) const {
    if (!host_.modDotTooltip || !isMapShown())
        return {};
    for (const int channel : {morphXChannel_, morphYChannel_}) {
        const auto axis = axisForChannel(channel);
        const float reach = MorphMap::kModBarThickness * 0.5f + host_.landingDotDiameter;
        if (map_.modBarAt(point) == axis || map_.stripAnchor(axis).getDistanceFrom(point) <= reach)
            if (const auto text = host_.modDotTooltip(channel); text.isNotEmpty())
                return text;
    }
    return {};
}

bool MorphShapesCard::acceptsShapeDrop(synth::shapes::ShapeKind kind) const {
    return host_.customActive && host_.customActive() &&
           synth::shapes::kindFromWord(adapter_->getPreferenceKind()) == kind;
}

void MorphShapesCard::shapeDragOver(bool over, const juce::String& name) {
    dropOver_ = over;
    dropName_ = over ? name : juce::String();
    if (host_.card != nullptr)
        host_.card->repaint();
}

// The shape under the cursor is the one the card's curve editor shows, so that is the one the drop replaces.
void MorphShapesCard::shapeDropped(const synth::shapes::LibraryShape& shape) {
    auto shapes = adapter_->getShapes();
    if (shapes.empty() || !acceptsShapeDrop(shape.kind))
        return;
    const auto index = (size_t)juce::jlimit(0, (int)shapes.size() - 1, nearestShapeToCursor());
    adapter_->beginEdit();
    shapes[index].curve = shape.curve;
    adapter_->setShapes(shapes);
    adapter_->endEdit();
    if (host_.card != nullptr)
        host_.card->repaint();
}

void MorphShapesCard::paintShapeDropHint(juce::Graphics& g, juce::Rectangle<float> bounds) const {
    const auto& theme = synth::theme::themeOf(*host_.card);
    g.setColour(theme.colors.accent.withAlpha(0.12f));
    g.fillRoundedRectangle(bounds, theme.metrics.cornerRadius);
    g.setColour(theme.colors.accent);
    g.drawRoundedRectangle(bounds.reduced(1.0f), theme.metrics.cornerRadius, theme.metrics.borderWidth * 2.0f);
    const juce::Font font{juce::FontOptions(13.0f)};
    const auto label = "Stamp " + dropName_ + " here";
    const float width = juce::jmin(bounds.getWidth() - 16.0f, font.getStringWidthFloat(label) + 20.0f);
    const auto pill = juce::Rectangle<float>(width, 24.0f).withCentre(bounds.getCentre());
    g.setColour(theme.colors.surfaceHi.withAlpha(0.95f));
    g.fillRoundedRectangle(pill, theme.metrics.cornerRadiusSmall);
    g.setColour(theme.colors.textPrimary);
    g.setFont(font);
    g.drawText(label, pill.reduced(8.0f, 0.0f), juce::Justification::centred, true);
}

} // namespace synth::ui
