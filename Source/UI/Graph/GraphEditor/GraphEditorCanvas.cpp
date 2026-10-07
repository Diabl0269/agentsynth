// GraphEditorCanvas.cpp
//
// Canvas-level component lifecycle (detachAllModuleComponents/updateComponents), GraphEditor's
// own paint/paintOverChildren/resized, zoom/pan/minimap, and canvas mouse handling
// (move/exit/down/drag/up). GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp
// files in this directory hold the rest of the class.

#include "AudioEngine/AudioEngine.h"
#include "GraphEditor.h"
#include "GraphEditorInternal.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Layout/ContextMenuPlacement.h"

#include "CanvasAccessibilityClip.h"
#include "Mixer/MasterSplice.h"
#include "Modules/AttenuverterModule.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

namespace {

// A card's body is built once; when the layout it was built from has changed since (a layout edit,
// its undo or redo, or its type's stored default), the card has to be rebuilt.
bool cardLayoutIsStale(const ModuleComponent& comp, const juce::AudioProcessorGraph::Node& node) {
    const auto* body = comp.getCardBody();
    return body != nullptr && body->isStaleFor(node);
}

// Swaps `stale` for a fresh card at the same index (so canvas order is kept) and deletes it. The old
// card lets go of the processor before the new one binds to it. Making room is the caller's: a
// restore puts every position back from its snapshot, and the quick path makes room inside its own
// undo step (CardLayoutQuickEdit.cpp).
ModuleComponent* rebuildCard(juce::OwnedArray<ModuleComponent>& modules, juce::Component& content,
                             ModuleComponent& stale, const std::function<ModuleComponent*()>& build) {
    stale.detachFromProcessor();
    content.removeChildComponent(&stale);
    auto* card = build();
    modules.set(modules.indexOf(&stale), card, /*deleteOldElement*/ true);
    content.addAndMakeVisible(card);
    return card;
}

} // namespace

// Ends the mod dot's "pick on canvas" mode (it holds no node pointer, but it highlights cards that are going away)
// and then runs the owner's unbind hook.
void GraphEditor::fireBeforeDetachAllModuleComponents() {
    modDot_->endCanvasPick();
    if (onBeforeDetachAllModuleComponents)
        onBeforeDetachAllModuleComponents();
}

void GraphEditor::detachAllModuleComponents() {
    // An undo or redo that frees a node tears every card down here: picture them first, so the ones it removes
    // shrink away (CardGlideAnimator::noteExitsBeforeTeardown). A no-op outside a restore.
    cardGlide_.noteExitsBeforeTeardown();
    // THE seam other UI hooks to unbind from live processors/parameters before they're freed by
    // whatever mutation the caller is about to run (see the member's own doc comment).
    fireBeforeDetachAllModuleComponents();
    // A teardown can't be allowed to leave the settle animator holding a SafePointer to a card
    // set that no longer applies. Harmless either way (SafePointer guards it), but keeps the
    // zoomGestureActive state machine honest.
    endZoomGesture();
    // Every ModuleComponent below is about to be destroyed — if one of them owned a live body drag
    // (dragPreviewActive/selectionDragActive armed by its own mouseDown), no mouseUp is ever coming
    // to reset it (e.g. an AI patch apply landing mid-gesture). Cancel
    // unconditionally: harmless when nothing was active, correct when something was.
    cancelLiveDragGestures();
    // The reload's updateComponents() lands the canvas frame at its new size with no glide.
    canvasFrame_.requestSnapOnNextUpdate();
    // Every graph-replacing path (project load, new patch, preset, AI apply, undo fallback) comes through here: the
    // arrangement the make-room records describe is gone.
    macroController_.clearModuleDisplacements();
    for (auto* comp : content.getModules())
        comp->detachFromProcessor();
    content.getModules().clear(); // Remove after detach so ~ModuleComponent doesn't double-detach freed params
    modMatrix.detachAllRows();
    modMatrix.clearRows();
}

void GraphEditor::updateComponents() {
    // Not re-entrant: a card constructed below is not in `modules` until this pass adds it, so a nested pass
    // would not see it and would build a second card for the same node, and so on without end. Nothing may
    // call back into here while cards are being built.
    if (updatingComponents) {
        jassertfalse;
        return;
    }
    // A cable edit can change which macro ports a routing is looked through without changing the routing itself,
    // so the next tick recounts every knob's sources (ModDotController::recountIfRoutingsChanged).
    modDot_->forgetRoutings();
    const juce::ScopedValueSetter<bool> reentrancyGuard(updatingComponents, true);

    auto& graph = audioEngine.getGraph();
    auto& modules = content.getModules();

    // Nodes appear/disappear here, so the cable memo can go stale from this call alone (a repaint
    // is not guaranteed to follow immediately).
    cablesCacheValid = false;

    // Any reconcile can follow a node removal (delete, undo/redo, preset load). Drop selected ids
    // whose nodes are gone BEFORE anything reads the selection again.
    pruneSelection();

    // 1. Remove components for nodes that no longer exist
    for (int i = modules.size(); --i >= 0;) {
        auto* comp = modules.getUnchecked(i);
        bool stillExists = false;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor() == comp->getModule()) {
                stillExists = true;
                break;
            }
        }
        if (!stillExists) {
            // The node this component tracked just vanished (undo, a doc removal) — if it was the
            // one live-dragging (dragPreviewSelfId), no mouseUp is ever coming to reset the flags it
            // armed; cancel now, before the component itself is destroyed below. A
            // non-initiating group member vanishing on its own is harmless: the initiator survives,
            // its real mouseUp is still coming, and finalizeSelectionDrag's lookup simply skips a
            // stale id it can't find (see cancelLiveDragGestures' own comment).
            if (dragDropController_.isDragPreviewActive() &&
                comp->getNodeId() == dragDropController_.getDragPreviewSelfId())
                cancelLiveDragGestures();
            macroController_.forgetModuleDisplacements(comp->getNodeId());
            content.removeChildComponent(comp);
            modules.remove(i);
        }
    }

    // 2. Add components for new nodes
    int moduleIndex = 0;
    for (auto* node : graph.getNodes()) {
        auto* processor = node->getProcessor();
        if (!processor)
            continue;

        if (dynamic_cast<AttenuverterModule*>(processor) != nullptr)
            continue;

        // Check if we already have a component for this module
        ModuleComponent* existingComp = nullptr;
        for (auto* comp : modules) {
            if (comp->getModule() == processor) {
                existingComp = comp;
                break;
            }
        }

        if (existingComp == nullptr) {
            auto* newComp = modules.add(new ModuleComponent(processor, node->nodeID, *this, undoManager));
            content.addAndMakeVisible(newComp);
            existingComp = newComp;
        } else if (cardLayoutIsStale(*existingComp, *node)) {
            if (dragDropController_.isDragPreviewActive() &&
                existingComp->getNodeId() == dragDropController_.getDragPreviewSelfId())
                cancelLiveDragGestures();
            existingComp = rebuildCard(modules, content, *existingComp, [&] {
                return new ModuleComponent(processor, node->nodeID, *this, undoManager);
            });
        }

        // Always sync position from properties OR deterministic fallback
        auto x = node->properties.getWithDefault("x", -1);
        auto y = node->properties.getWithDefault("y", -1);

        if (static_cast<int>(x) != -1 && static_cast<int>(y) != -1) {
            existingComp->setTopLeftPosition(static_cast<int>(x), static_cast<int>(y));
        } else {
            // Fallback: no stored position — resolve a non-overlapping slot.
            // Desired origin strides by 300px to reduce clustering; resolvePlacement
            // then snaps and spirals clear of any previously placed components.
            auto desired = juce::Point<int>(synth::LayoutUtil::kArrangeOriginX + moduleIndex * 300, 600);
            auto clear = resolvePlacement(desired, existingComp->getWidth(), existingComp->getHeight(), node->nodeID);
            existingComp->setTopLeftPosition(clear);
            // Persist the resolved position so subsequent loads don't need to re-resolve
            node->properties.set("x", clear.x);
            node->properties.set("y", clear.y);
        }

        moduleIndex++;
    }

    // Reconcile macro membership against whatever nodes actually survived — the MacroSet
    // analogue of pruneSelection() above, and the ONE seam that keeps `macros` from ever
    // naming a dead node, regardless of which delete/undo/preset-load path got here.
    {
        std::vector<juce::String> aliveUuids;
        for (auto* node : graph.getNodes()) {
            const juce::String uuid = node->properties["uuid"].toString();
            if (uuid.isNotEmpty())
                aliveUuids.push_back(uuid);
        }
        macros.retainOnly(aliveUuids);
    }
    evictOutputDockFromMacros(); // a dock card is never a macro member (a saved project may hold one)
    syncMacroCards();
    macroController_.dockMacroPortWidgets();
    reflowOutputDock(); // load, module add/remove, undo/redo: the dock always sits right of everything

    // Refresh mod matrix to pick up any new/removed attenuverter routings
    // Use callAsync to avoid re-entrancy during graph modification
    // SafePointer guards against the GraphEditor being destroyed before the callback fires
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->modMatrix.updateRowsIfOpen();
    });

    // Let owners refresh anything that depends on which modules the patch now contains. Event-driven
    // on purpose: no timer and no per-tick repaint.
    if (onGraphStructureChanged)
        onGraphStructureChanged();

    // A card created mid-gesture (e.g. paste/duplicate while zooming) must join the freeze, or it
    // rasterizes once at the pre-gesture scale and then again at thaw instead of just once.
    if (zoomGestureActive)
        setModuleRasterFrozen(true);

    // Cards can appear/disappear/reposition here (new node, deleted node, position synced
    // from properties) without a pan/zoom in between, so updateTransform()'s own call wouldn't
    // see it until the next frame.
    applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());

    refreshCanvasFrame(CanvasFrame::Mode::Animate);
    repaint();
}

// The colour outside the canvas frame; GraphContentComponent::paint draws the frame itself.
void GraphEditor::paint(juce::Graphics& g) {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        lf->fillThemedBackground(g, getLocalBounds().toFloat(), /*isCanvas*/ false);
    else
        g.fillAll(juce::Colours::darkgrey);
}

// Draws the empty-canvas onboarding hint centred in the visible, untransformed viewport,
// after children paint -- so it draws over the canvas unaffected by the content transform.
void GraphEditor::paintOverChildren(juce::Graphics& g) {
    // The canvas's focus-region outline, drawn OVER children (unlike the other four focus
    // regions' paint()) so a module's own image-cached body can never occlude it. Ahead of the
    // empty-canvas early return below, since the outline must show regardless of whether the canvas
    // has any modules in it.
    //
    // Mod Matrix is a CHILD component of the canvas (its own focus region nests inside this one —
    // see FocusRegionRegistry::regionContaining), so `hasKeyboardFocus(true)` here is also true
    // while focus is inside the Mod Matrix. Skip the canvas's own outline in that case so the two
    // regions never paint two accent rectangles for one focus location; the Mod Matrix paints its
    // own outline in ModMatrixComponent::paint().
    if (!modMatrix.hasKeyboardFocus(true))
        synth::ui::paintFocusRegionOutline(*this, g);

    // ---- Empty-canvas first-run hint ----
    // Drawn here (OUTER, untransformed GraphEditor local coordinates) so it is ALWAYS
    // centred in the visible viewport regardless of pan/zoom on the inner canvas.
    // The inner GraphContentComponent runs in a transformed (pan+zoom) space over a
    // virtual canvas (the canvas frame plus slack) — any rect drawn there would land off-screen once the
    // user pans or zooms. Drawing here, in getLocalBounds(), guarantees centre alignment.
    //
    // Gate: only when canvas is empty. Show/hide is driven by the existing updateComponents()
    // repaint path — no extra timer or per-tick repaint is added.
    if (!GraphEditor::isCanvasEmpty(static_cast<int>(content.getModules().size())))
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // textMuted token at ~60% alpha — tasteful, non-distracting.
    const juce::Colour textMutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;
    g.setColour(textMutedColour.withAlpha(0.6f));

    // Use the theme h1 font (~18pt) for comfortable legibility; fall back to 16pt headless.
    juce::Font hintFont;
    if (lf != nullptr)
        hintFont = juce::Font(juce::FontOptions(lf->getTheme().type.h1)).withStyle(juce::Font::plain);
    else
        hintFont = juce::Font(16.0f);
    g.setFont(hintFont);

    // Centre in the GraphEditor's visible local bounds (untransformed viewport coordinates).
    g.drawFittedText("Drag modules here to build your patch", getLocalBounds(), juce::Justification::centred, 1);
    // ---- End empty-canvas hint ----
}

void GraphEditor::resized() {
    // Mitigates the case where a zoom gesture's settle animator never completes (no VBlank, e.g.
    // the window is hidden mid-gesture): a resize is a good proxy for "something else is about to
    // repaint everything anyway", so thaw now rather than leave every card soft indefinitely.
    endZoomGesture();

    // Only set the mod-matrix bounds when we are not in the middle of an animated show/hide.
    // If an animation is running, it owns the bounds until it completes; we update the target
    // but don't interrupt the tween.
    if (!modMatrixAnim.isRunning()) {
        if (isMatrixVisible) {
            modMatrix.setBounds(getWidth() - 600, 0, 600, getHeight());
        }
    } else {
        // Update the stored target so the onComplete callback uses the updated size.
        modMatrixTargetBounds = isMatrixVisible ? juce::Rectangle<int>(getWidth() - 600, 0, 600, getHeight())
                                                : juce::Rectangle<int>(getWidth(), 0, 600, getHeight());
    }

    layoutMinimap();

    updateTransform();
}

void GraphEditor::lookAndFeelChanged() {
    // Same rationale as resized(): a theme swap re-skins/re-rasters everything anyway, so a zoom
    // gesture straddling a theme change must not leave cards frozen at the old scale afterwards.
    endZoomGesture();
}

void GraphEditor::toggleModMatrixVisibility() {
    isMatrixVisible = !isMatrixVisible;

    // Always make it visible before the animation so it paints during the tween.
    // On hide we keep it visible until the animation completes, then hide it.
    modMatrix.setVisible(true);

    const int panelW = 600;
    const int editorW = getWidth();
    const int editorH = getHeight();

    // From-bounds: current position (either fully shown or fully hidden off-screen right).
    juce::Rectangle<int> fromBounds = modMatrix.getBounds();
    // If the component has never been laid out, give it a sensible off-screen start.
    if (fromBounds.isEmpty())
        fromBounds = {editorW, 0, panelW, editorH};

    // To-bounds: target position.
    juce::Rectangle<int> toBounds = isMatrixVisible ? juce::Rectangle<int>(editorW - panelW, 0, panelW, editorH)
                                                    : juce::Rectangle<int>(editorW, 0, panelW, editorH);

    modMatrixTargetBounds = toBounds;

    // Start a 220 ms easeOutCubic tween on modMatrix bounds.
    const bool hidingAfterAnim = !isMatrixVisible;
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    modMatrixAnim.start(
        vblankUpdater, 220.0, synth::ui::easeOutCubic,
        [safeThis, fromBounds, toBounds](float t) {
            if (auto* self = safeThis.getComponent())
                self->modMatrix.setBounds(synth::ui::AnimationDriver::lerpBounds(fromBounds, toBounds, t));
        },
        [safeThis, hidingAfterAnim, toBounds]() {
            if (auto* self = safeThis.getComponent()) {
                self->modMatrix.setBounds(toBounds);
                if (hidingAfterAnim)
                    self->modMatrix.setVisible(false);
            }
        });
}

void GraphEditor::updateTransform() {
    juce::AffineTransform t;
    t = t.scaled(zoomLevel, zoomLevel);
    t = t.translated(panOffset);

    applyContentBounds();
    content.setTransform(t);
    // A zoomed-out macro port's interior jack slides onto its boundary jack (getPortCenter reads the zoom), so the
    // memoized cable endpoints go stale with the zoom itself; the strips/widgets repaint live.
    if (!getMacros().empty())
        cablesCacheValid = false;
    repaint();

    // Keep the minimap tracking pan/zoom immediately rather than waiting up to 33ms for the next
    // timer tick. Only the viewport rect is pushed here: pan/zoom move what you're LOOKING at, not
    // where the modules and cables are, so rebuilding the full model on every drag frame would
    // re-walk every graph edge for nothing. The 30 Hz tick owns node/cable changes.
    if (minimap.isVisible())
        minimap.setViewport(getVisibleCanvasRect());

    // updateTransform() runs every wheel/pan/zoom frame, so a card that just left or
    // re-entered the visible rect gets its accessibility flipped immediately -- the per-card guard
    // inside makes every other frame here a no-op.
    applyCanvasAccessibilityClip(content.getModules(), content.getMacroCards(), getVisibleCanvasRect());
}

// Shared zoom math for mouseWheelMove and zoomAroundCentre — keeps the formula (and the
// [0.1, 2.0] clamp) in exactly one place. `screenAnchor` is the point (in GraphEditor local/
// screen coordinates) whose underlying canvas point must stay put under the cursor/centre.
void GraphEditor::applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor) {
    wheelPanTween_.stop();
    float oldZoom = zoomLevel;
    zoomLevel += wheelDelta * 0.1f * zoomLevel;
    zoomLevel = juce::jlimit(synth::ViewDoc::kMinZoom, synth::ViewDoc::kMaxZoom, zoomLevel);

    if (oldZoom != zoomLevel) {
        // Transform the anchor position to get the graph point before scaling
        auto invT =
            juce::AffineTransform::translation(-panOffset.x, -panOffset.y).scaled(1.0f / oldZoom, 1.0f / oldZoom);
        float gx = screenAnchor.x;
        float gy = screenAnchor.y;
        invT.transformPoint(gx, gy);

        // We want to keep the graph point under the anchor constant:
        // anchor = (graphPointBefore * zoomLevel) + newPanOffset
        // newPanOffset = anchor - (graphPointBefore * zoomLevel)
        panOffset.x = screenAnchor.x - (gx * zoomLevel);
        panOffset.y = screenAnchor.y - (gy * zoomLevel);
    }

    updateTransform();

    // Only a real scale change costs a re-raster; a clamped wheel tick at the 0.1/2.0 limits
    // must not keep the cards soft forever.
    if (oldZoom != zoomLevel)
        beginOrRefreshZoomGesture();
}

// The canvas view a project saves. Applying one is a view change only: it is not an edit, so it records no undo
// step and does not mark the document unsaved (the same as any pan or zoom).
synth::ViewDoc GraphEditor::getViewDoc() const {
    synth::ViewDoc view;
    view.zoom = zoomLevel;
    view.panX = panOffset.x;
    view.panY = panOffset.y;
    return view;
}

void GraphEditor::applyViewDoc(const synth::ViewDoc& view) {
    wheelPanTween_.stop();
    const float oldZoom = zoomLevel;
    zoomLevel = juce::jlimit(synth::ViewDoc::kMinZoom, synth::ViewDoc::kMaxZoom, view.zoom);
    panOffset = {view.panX, view.panY};
    updateTransform();
    // A changed scale re-rasters every card, exactly as a wheel tick does.
    if (oldZoom != zoomLevel)
        beginOrRefreshZoomGesture();
}

void GraphEditor::setModuleRasterFrozen(bool frozen) {
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            comp->setRasterFrozen(frozen);
}

// While a zoom gesture is in flight every card's raster scale is pinned, so a wheel tick resamples
// the cached images instead of re-rendering every panel + slider at a new scale. The gesture ends
// kZoomSettleMs after the last zoom event and thaws with exactly one crisp re-render. Time-bounded
// (docs/layout/animation.md#the-time-bounded-animation-rule).
void GraphEditor::beginOrRefreshZoomGesture() {
    if (!zoomGestureActive) {
        zoomGestureActive = true;
        setModuleRasterFrozen(true);
    }
    // start() stops+replaces any running animator, so every zoom event restarts the settle
    // window: this IS the debounce. onUpdate is a no-op — the driver adds no repaint source.
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    zoomSettleAnim.start(
        vblankUpdater, kZoomSettleMs, [](float t) { return t; }, [](float) {},
        [safeThis] {
            if (auto* self = safeThis.getComponent())
                self->endZoomGesture();
        });
}

void GraphEditor::endZoomGesture() {
    if (!zoomGestureActive)
        return;
    zoomGestureActive = false;
    // Thawing each card drops its image and repaints it, so the crisp pass costs exactly one
    // rasterization per visible card for the whole gesture.
    setModuleRasterFrozen(false);
}

// The canvas rect currently visible in the editor — the inverse of the content transform
// applied to getLocalBounds().
juce::Rectangle<float> GraphEditor::getVisibleCanvasRect() const {
    // Rebuilt from zoomLevel/panOffset rather than reading content.getTransform(), so this stays
    // independent of child state and can be const.
    const juce::AffineTransform t = juce::AffineTransform().scaled(zoomLevel, zoomLevel).translated(panOffset);
    return getLocalBounds().toFloat().transformedBy(t.inverted());
}

// Pans so `canvasPoint` sits at the centre of the visible area. Zoom is unchanged.
void GraphEditor::centreViewOn(juce::Point<float> canvasPoint) {
    wheelPanTween_.stop();
    panOffset = getLocalBounds().getCentre().toFloat() - canvasPoint * zoomLevel;
    updateTransform();
}

// Multiplies zoom around the centre of the visible area, clamped to the same [0.1, 2.0]
// range as wheel zoom, so the point under the centre stays put.
void GraphEditor::zoomAroundCentre(float wheelDelta) {
    applyZoomAt(wheelDelta, getLocalBounds().getCentre().toFloat());
}

synth::ui::MinimapModel GraphEditor::buildMinimapModel() {
    synth::ui::MinimapModel model;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& colors = lf != nullptr ? lf->getTheme().colors : fallbackColors;

    for (auto* comp : content.getModules()) {
        if (comp == nullptr || comp->getModule() == nullptr)
            continue;

        // Per-category theme colour, the same source buildVisibleCables()/colourForCable() use for
        // "By source module" cable colouring — there is no cheap per-instance colour on
        // ModuleComponent itself, but category IS available cheaply via ModuleBase::getModuleType().
        auto category = synth::ui::ModuleCategory::Utility;
        if (auto* mb = dynamic_cast<ModuleBase*>(comp->getModule()))
            category = synth::ui::categoryFor(mb->getModuleType());

        synth::ui::MinimapModel::Node node;
        node.bounds = comp->getBounds().toFloat();
        node.colour = synth::ui::themeColourForCategory(colors, category);
        node.selected = isNodeSelected(comp->getNodeId());
        model.nodes.push_back(node);
    }

    for (const auto& cable : buildVisibleCables()) {
        synth::ui::MinimapModel::Cable mc;
        mc.p1 = cable.p1;
        mc.p2 = cable.p2;
        mc.colour = colourForCable(cable);
        model.cables.push_back(mc);
    }

    model.viewport = getVisibleCanvasRect();
    return model;
}

// Scroll + zoom the viewport so every module component is on-screen, clamped to the
// same [0.1, 2.0] range as wheel zoom. Called after a patch is loaded so the just-loaded
// modules are not left off-screen at their saved coordinates; a no-op when there are no
// modules or the editor has no area yet.
void GraphEditor::fitViewToModules() {
    // After loading a patch, bring every module on-screen. A loaded patch keeps its saved
    // coordinates, which often fall outside the current viewport; fitting the view shows the result
    // of the load instead of leaving the user staring at an empty region of the canvas.
    auto model = buildMinimapModel(); // one pass over modules + cables, already non-null-filtered

    const float margin = 40.0f;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    bool haveBox = false;
    for (const auto& n : model.nodes) {
        const auto& b = n.bounds;
        if (b.getWidth() < 1.0f || b.getHeight() < 1.0f)
            continue;
        if (!haveBox) {
            x1 = b.getX();
            y1 = b.getY();
            x2 = b.getRight();
            y2 = b.getBottom();
        } else {
            x1 = juce::jmin(x1, b.getX());
            y1 = juce::jmin(y1, b.getY());
            x2 = juce::jmax(x2, b.getRight());
            y2 = juce::jmax(y2, b.getBottom());
        }
        haveBox = true;
    }
    if (!haveBox)
        return; // no placed modules to fit

    x1 -= margin;
    y1 -= margin;
    x2 += margin;
    y2 += margin;
    juce::Rectangle<float> box{x1, y1, x2 - x1, y2 - y1};

    juce::Rectangle<float> view = getLocalBounds().toFloat(); // == viewport when the editor fills its parent
    if (view.getWidth() < 1.0f || view.getHeight() < 1.0f)
        return; // not laid out yet; a later paint/resize will settle the view

    float scale = juce::jmin(view.getWidth() / box.getWidth(), view.getHeight() / box.getHeight());
    scale = juce::jlimit(synth::ViewDoc::kMinZoom, synth::ViewDoc::kMaxZoom, scale);
    zoomLevel = scale;
    // Centre the (expanded) box within the viewport. screen = content * scale + panOffset.
    float targetCenterX = view.getX() + view.getWidth() * 0.5f;
    float targetCenterY = view.getY() + view.getHeight() * 0.5f;
    float boxCenterX = box.getX() + box.getWidth() * 0.5f;
    float boxCenterY = box.getY() + box.getHeight() * 0.5f;
    panOffset = {(targetCenterX - boxCenterX * scale), (targetCenterY - boxCenterY * scale)};
    updateTransform();
}

// True when locateMasterOrOutput() has a node to find — drives the canvas context menu item's
// (and the equivalent command's) enabled state, so the two surfaces can never disagree.
bool GraphEditor::hasLocatableMasterOrOutput() const {
    auto& graph = audioEngine.getGraph();
    if (synth::findMasterNode(graph) != nullptr)
        return true;
    for (auto* node : graph.getNodes())
        if (node != nullptr && isTerminalAudioSink(node->getProcessor()))
            return true;
    return false;
}

// Selects Master, falling back to Audio Output when there is no Master yet, and pans it into
// the centre of the view. Graceful no-op (LocateMasterResult::NoTarget) when the patch has
// neither node. See the note below for the select/pan/minimap-highlight reuse.
//
// Reuses the exact select-by-NodeID path MainComponent::selectNodeInGraph already uses for the
// timeline binding chip (GraphEditor::selectModule) rather than duplicating it, plus the same pan
// primitive the minimap's own click-to-navigate uses (centreViewOn). The minimap highlight comes
// for free: buildMinimapModel() derives Node::selected from the current selection, so selecting
// Master IS the minimap highlight — refreshed immediately here rather than waiting for the next
// 30 Hz tick, the same as centreViewOn's own immediate viewport push in updateTransform().
GraphEditor::LocateMasterResult GraphEditor::locateMasterOrOutput() {
    auto& graph = audioEngine.getGraph();

    juce::AudioProcessorGraph::Node* node = synth::findMasterNode(graph);
    LocateMasterResult result = LocateMasterResult::Master;
    if (node == nullptr) {
        // Fall back to Audio Output — identified by TYPE via isTerminalAudioSink (defined above),
        // never by the "Audio Output" name comparison MasterSplice.cpp uses, so a ModuleBase that
        // happened to share that name could not impersonate the sink.
        for (auto* n : graph.getNodes()) {
            if (n != nullptr && isTerminalAudioSink(n->getProcessor())) {
                node = n;
                break;
            }
        }
        result = LocateMasterResult::AudioOutput;
    }
    if (node == nullptr)
        return LocateMasterResult::NoTarget; // neither node exists yet — graceful no-op

    // Reuses the exact select-by-NodeID path MainComponent::selectNodeInGraph already uses for the
    // timeline binding chip, rather than duplicating it.
    selectModule(node->nodeID, /*additive=*/false);

    frameOutputDock(); // "Go to Output": the whole dock, not just the one selected card

    // The minimap highlight is free (buildMinimapModel() derives Node::selected from the selection
    // set above) — pushed immediately rather than waiting for the next 30 Hz tick, the same as
    // centreViewOn's own immediate viewport push in updateTransform().
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());

    return result;
}

void GraphEditor::mouseMove(const juce::MouseEvent& e) {
    auto localPos = content.getLocalPoint(this, e.getPosition());

    // Hovering an expanded macro's name chip shows a grab cursor, since the chip doubles as a drag
    // handle. Tracked with its own bool (rather than early-returning) so leaving the chip falls
    // through to the ordinary cable/canvas cursor logic below instead of getting stuck on the hand
    // cursor - cheap either way (one hull-list walk), no repaint needed for a cursor-only change.
    const bool overChip = !macroController_.macroChipAt(localPos.roundToInt()).isEmpty();
    if (overChip != hoveringMacroChip) {
        hoveringMacroChip = overChip;
        setMouseCursor(overChip ? synth::ui::dragGrabCursor() : juce::MouseCursor(juce::MouseCursor::NormalCursor));
    }
    if (overChip)
        return;

    std::optional<CableId> newId;
    std::optional<VisibleCable> newCable = getCableAt(localPos.toFloat());
    if (newCable)
        newId = newCable->id;

    // Repaint only when the hovered cable actually CHANGES, not on every mouse move. (The canvas
    // already repaints at 30Hz for the wire animation, so this just marks the next frame dirty
    // rather than introducing a new repaint source — see the no-continuous-repaint invariant.)
    const bool changed =
        newId.has_value() != hoveredCableId.has_value() || (newId.has_value() && *newId != *hoveredCableId);
    if (!changed)
        return;

    hoveredCableId = newId;
    setMouseCursor(newId.has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

    // A hovered cable that lands on a knob highlights that knob's ring too, via the same
    // shared hover-correlation state a knob hover writes the other direction (setHoveredModTarget
    // repaints only the affected card). docs/modules/modulation.md#modulation-rings-on-knobs.
    if (newCable.has_value() && newCable->landsOnKnob)
        setHoveredModTarget(
            HoveredModTarget{juce::AudioProcessorGraph::NodeID{newCable->destNodeId}, newCable->destChannel});
    else
        setHoveredModTarget(std::nullopt);

    repaintCanvas();
}

void GraphEditor::mouseExit(const juce::MouseEvent&) {
    const bool wasHoveringChip = hoveringMacroChip;
    hoveringMacroChip = false;
    if (!hoveredCableId.has_value() && !wasHoveringChip)
        return;
    hoveredCableId.reset();
    setHoveredModTarget(std::nullopt);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaintCanvas();
}

void GraphEditor::mouseDown(const juce::MouseEvent& e) {
    // Clicking away from an inline title editor commits it. Done explicitly and FIRST rather than
    // leaning on the grabKeyboardFocus() below to fire onFocusLost: that ordering happens to work
    // for a canvas press but is invisible coupling, and it does nothing for a press on a card
    // (ModuleComponent never grabs focus). Idempotent — whichever path runs second finds no editor.
    commitAnyOpenTitleRename();

    // Any press on the canvas takes focus, so the canvas-scoped Delete/Escape keys land here
    // rather than on whichever panel happened to be focused last. Must come BEFORE the cable
    // menu below, which returns early — otherwise a right-click on a wire would skip it.
    grabKeyboardFocus();

    // Right-click on a cable: act on the wire itself. Until this existed the only way to remove
    // a connection was to right-click one of its PORTS, which is not where users aim.
    if (e.mods.isPopupMenu()) {
        auto canvasPos = content.getLocalPoint(this, e.getPosition()).toFloat();
        if (auto cable = getCableAt(canvasPos)) {
            const auto captured = *cable;
            juce::Component::SafePointer<GraphEditor> safeThis(this);

            juce::PopupMenu m;
            m.addSectionHeader(juce::String(synth::ui::cableSignalLabel(captured.signal)) + " cable");
            m.addItem("Disconnect Cable", [safeThis, captured] {
                if (safeThis != nullptr)
                    safeThis->disconnectCable(captured);
            });
            m.showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
            return;
        }

        // Right-click inside an expanded macro's hull: the same macro actions the collapsed
        // card's own menu offers, reachable without collapsing first.
        //
        // buildMacroMenu's "Ungroup" and "Save as Snippet..." items act on the CURRENT SELECTION
        // (ungroupSelection()/onSaveSnippetRequested()), and mouseUp deliberately preserves
        // whatever was selected on a right-click (so the canvas menu's Paste keeps working).
        // buildMacroMenu selects the macro itself before either item runs
        // (docs/macros/menu-and-membership.md#the-macro-menus-entry-points), so the selectMacro()
        // below is a redundant safeguard, not what makes those items target this macro.
        if (const auto hullMacroId = macroController_.macroHullAt(canvasPos.roundToInt()); hullMacroId.isNotEmpty()) {
            // Captured BEFORE the reselect above, which otherwise destroys any external
            // batch (or a partial subset of this macro's own members, picked for a targeted
            // "Remove Selection from Macro") the user chose before right-clicking this hull —
            // see buildMacroMenu's own comment on addCandidateSelection, and
            // MacroCardComponent::mouseDown's matching right-click branch. Only reselect when
            // there was NO prior selection at all.
            const auto priorSelection = getSelectedNodes();
            if (priorSelection.empty())
                macroController_.selectMacro(hullMacroId, false);
            buildMacroMenu(hullMacroId, nullptr, &priorSelection)
                .showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
            return;
        }

        // Nothing under the cursor: the canvas menu, which is how paste is reachable without the
        // keyboard. Right-clicking empty canvas leaves the selection alone (see mouseUp), so a
        // paste from here still knows what was selected.
        showCanvasContextMenu(canvasPos.roundToInt());
        return;
    }

    if (e.mods.isLeftButtonDown()) {
        lastMousePos = e.getPosition();
        pendingEmptyCanvasClick = false;

        auto localPos = content.getLocalPoint(this, e.getPosition());

        // The '+' / '-' at the foot of an open macro's port strips, checked first: '+' opens the same kind/shape
        // menu the collapsed card's '+' does (through the canvas menu hook a test installs; null shows the real
        // async menu), '-' deletes the bottom port on that side.
        if (auto hit = macroController_.macroHullPortButtonAt(localPos.roundToInt(),
                                                              content.getTransform().getScaleFactor())) {
            if (hit->isAdd) {
                auto menu = macroController_.buildAddPortMenu(hit->macroId, hit->isInput);
                if (showCanvasContextMenuHook_)
                    showCanvasContextMenuHook_(menu);
                else
                    menu.showMenuAsync(synth::ui::contextMenuOptionsAtPointer());
            } else {
                macroController_.deleteBottomMacroPort(hit->macroId, hit->isInput);
            }
            return;
        }

        // Collapse button - checked BEFORE the chip below, carving its
        // hit zone out of that row explicitly, even though the two rectangles never actually
        // overlap (macroCollapseButtonBounds sits at the row's right end, macroChipBounds at its
        // left). A single click collapses through the SAME setMacroCollapsed the menu's
        // "Collapse" item uses (one undo step - see applyMacroCollapsed), so there is only ever
        // one code path that can collapse a macro. Not gated on Shift the way the chip is: the
        // button is a small fixed target near the hull's top-right corner, not the drag-prone
        // strip the marquee-vs-chip carve-out below exists for.
        if (auto macroId = macroController_.macroCollapseButtonAt(localPos.roundToInt()); macroId.isNotEmpty()) {
            macroController_.setMacroCollapsed(macroId, true);
            return;
        }

        // Pressing an expanded macro's name chip drags the whole macro as a rigid body - checked
        // before the attenuverter/empty-canvas-click logic below so the chip wins over whatever
        // would otherwise be under it (in practice, empty canvas above the hull).
        //
        // Shift is excluded deliberately: it is the explicit marquee modifier, and a marquee that
        // happens to start on a chip should still be a marquee. The chip is a small target, so
        // letting it swallow Shift+drag would make marquees fail unpredictably near a hull's top
        // edge. Unmodified drag is the chip's gesture; Shift keeps belonging to the marquee.
        // The chip's rigid-body drag start, shared with the hull-drag preference below so both
        // gestures run the ONE beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag path.
        const auto beginMacroBodyDrag = [this, &localPos](const juce::String& macroId) {
            macroController_.selectMacro(macroId, false);
            if (undoManager)
                undoManager->captureBeforeState(audioEngine.getGraph());
            beginSelectionDrag();
            macroChipDragId = macroId;
            macroChipDragStartCanvasPos = localPos.roundToInt();
            pendingEmptyCanvasClick = false;
        };

        if (auto macroId = macroController_.macroChipAt(localPos.roundToInt());
            macroId.isNotEmpty() && !e.mods.isShiftDown()) {
            beginMacroBodyDrag(macroId);
            return;
        }

        auto attenId = getAttenuverterNodeAt(localPos.toFloat());
        if (attenId.uid != 0) {
            draggingAttenuverterNodeId = attenId;
            beginModAmountGesture(); // shared with the card-knob ring-drag gesture
            return;
        }

        draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

        // Preference "moveMacroOnHullDrag" (off by default): an unmodified press on the empty space
        // inside an expanded hull moves the macro like its name chip does. Checked after the chip and
        // attenuverter so those keep winning; Shift stays the marquee. A macro with fewer than two
        // members cannot arm a group drag, so it keeps panning rather than doing nothing.
        if (moveMacroOnHullDragEnabled && !e.mods.isShiftDown()) {
            const auto hullId = macroController_.macroHullAt(localPos.roundToInt());
            const auto* hullMacro = hullId.isNotEmpty() ? getMacros().find(hullId) : nullptr;
            // Counted transitively: a parent with one direct member and a nested child still moves.
            if (hullMacro != nullptr && getMacros().descendantMembers(hullId).size() > 1) {
                beginMacroBodyDrag(hullId);
                return;
            }
        }

        // Shift starts a marquee; anything else keeps the historical drag-to-pan behaviour.
        // Cmd/Ctrl alongside Shift makes the marquee additive.
        if (e.mods.isShiftDown()) {
            beginMarquee(localPos.roundToInt(), e.mods.isCommandDown() || e.mods.isCtrlDown());
        } else {
            // Deferred: only a press that never becomes a drag counts as "click empty canvas to
            // deselect", so panning does not wipe the selection.
            pendingEmptyCanvasClick = true;
        }
    }
}

void GraphEditor::mouseDrag(const juce::MouseEvent& e) {
    if (marqueeActive) {
        updateMarquee(content.getLocalPoint(this, e.getPosition()).roundToInt());
        return;
    }

    if (macroChipDragId.isNotEmpty()) {
        // The delta MUST be computed in CANVAS space (via content.getLocalPoint), not in
        // GraphEditor-local space the way the pan code below does: `content` carries the zoom
        // transform, so a raw e.getPosition() delta would make the macro drift at any zoom other
        // than 1.0 (a delta of N screen pixels is N/zoom canvas pixels).
        auto canvasPos = content.getLocalPoint(this, e.getPosition()).roundToInt();
        dragSelectionBy(clampDragDeltaToCanvas(canvasPos - macroChipDragStartCanvasPos), nullptr);
        refreshCanvasFrame(CanvasFrame::Mode::GrowOnly); // the frame steps out ahead of the hull
        repaintCanvas();
        return;
    }

    if (e.mods.isLeftButtonDown() && !isDraggingConnection) {
        pendingEmptyCanvasClick = false;
        if (draggingAttenuverterNodeId.uid != 0) {
            const float delta = (e.getPosition().y - lastMousePos.y) * -0.01f;
            adjustModAmount(draggingAttenuverterNodeId, delta); // shared gesture helper
            lastMousePos = e.getPosition();
            return;
        }

        auto delta = e.getPosition() - lastMousePos;
        panOffset += delta.toFloat();
        lastMousePos = e.getPosition();
        updateTransform();
    }
}

void GraphEditor::mouseUp(const juce::MouseEvent& e) {
    if (marqueeActive) {
        endMarquee();
        return;
    }

    if (macroChipDragId.isNotEmpty()) {
        const auto canvasPos = content.getLocalPoint(this, e.getPosition()).roundToInt();
        // isSelectionDragActive() guards a single-member macro (reachable - see
        // MacroDelete.DeletingDownToOneMemberDoesNotDissolveButDeletingTheLastDoes): beginSelectionDrag
        // requires >1 recorded member to arm, so a lone member never actually moves even if the
        // mouse travelled, and finalizing would push a no-delta undo entry that visibly does nothing.
        const bool moved = isSelectionDragActive() && canvasPos != macroChipDragStartCanvasPos;
        if (moved) {
            // Graph AND macros in one step: the drop can shift a carried nested collapsed macro's
            // `bounds` (finalizeSelectionDrag). The graph "before" is the mouseDown capture, since
            // every drag tick already wrote live positions into the graph.
            auto doFinalize = [this] { finalizeSelectionDrag(); }; // snaps + de-overlaps as one rigid body
            if (undoManager)
                undoManager->recordGraphAndMacroChange(audioEngine.getGraph(), macros, doFinalize,
                                                       undoManager->takeCapturedGraphBeforeState());
            else
                doFinalize();
        } else {
            cancelSelectionDrag();
        }
        macroChipDragId.clear();
        repaintCanvas();
        return;
    }

    if (draggingAttenuverterNodeId.uid != 0)
        commitModAmountGesture(); // shared gesture helper
    draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

    // A press on empty canvas that never turned into a pan is a plain click: deselect — UNLESS it
    // landed inside an expanded macro's hull, in which case it selects
    // that macro instead. Only reachable here at all because a click that landed ON a member
    // module is consumed by that ModuleComponent's own mouseDown and never reaches the canvas —
    // this is deliberately just the empty space inside the hull (between/around member cards),
    // never a drag-to-move-the-macro gesture, so it can't steal the pan gesture.
    if (pendingEmptyCanvasClick) {
        pendingEmptyCanvasClick = false;
        if (e.mods.isPopupMenu())
            return; // right-click keeps the selection so the context menu can act on it

        const auto canvasPos = content.getLocalPoint(this, e.getPosition());
        const auto hullMacroId = macroController_.macroHullAt(canvasPos);
        // The hull is big and reads as empty canvas: a single click on an already-selected macro's
        // hull clears. The second click of a double-click still selects (numClicks >= 2).
        if (hullMacroId.isNotEmpty() && !(macroController_.isMacroSelected(hullMacroId) && e.getNumberOfClicks() < 2))
            macroController_.selectMacro(hullMacroId, false);
        else
            clearSelection();
    }
}
