// GraphEditorCanvas.cpp
//
// Canvas-level component lifecycle (detachAllModuleComponents/updateComponents), GraphEditor's
// own paint/paintOverChildren/resized, zoom/pan/minimap, and canvas mouse handling
// (move/exit/down/drag/up). GraphEditor is declared in GraphEditor.h; sibling GraphEditor*.cpp
// files in this directory hold the rest of the class.

#include "GraphEditor.h"
#include "GraphEditorInternal.h"

#include "Mixer/MasterSplice.h"
#include "Modules/AttenuverterModule.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Macros/MacroCardComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

void GraphEditor::detachAllModuleComponents() {
    // A teardown can't be allowed to leave the settle animator holding a SafePointer to a card
    // set that no longer applies. Harmless either way (SafePointer guards it), but keeps the
    // zoomGestureActive state machine honest.
    endZoomGesture();
    // Every ModuleComponent below is about to be destroyed — if one of them owned a live body drag
    // (dragPreviewActive/selectionDragActive armed by its own mouseDown), no mouseUp is ever coming
    // to reset it (FRO19: an AI patch apply landing mid-gesture is the reproducing case). Cancel
    // unconditionally: harmless when nothing was active, correct when something was.
    cancelLiveDragGestures();
    for (auto* comp : content.getModules())
        comp->detachFromProcessor();
    content.getModules().clear(); // Remove after detach so ~ModuleComponent doesn't double-detach freed params
    modMatrix.detachAllRows();
    modMatrix.clearRows();
}

void GraphEditor::updateComponents() {
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
            // armed (FRO19); cancel now, before the component itself is destroyed below. A
            // non-initiating group member vanishing on its own is harmless: the initiator survives,
            // its real mouseUp is still coming, and finalizeSelectionDrag's lookup simply skips a
            // stale id it can't find (see cancelLiveDragGestures' own comment).
            if (isDragPreviewActive() && comp->getNodeId() == getDragPreviewSelfId())
                cancelLiveDragGestures();
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
    syncMacroCards();
    dockMacroPortWidgets();

    // Refresh mod matrix to pick up any new/removed attenuverter routings
    // Use callAsync to avoid re-entrancy during graph modification
    // SafePointer guards against the GraphEditor being destroyed before the callback fires
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->modMatrix.updateRowsFromGraph();
    });

    // Let owners refresh anything that depends on which modules the patch now contains. Event-driven
    // on purpose: no timer and no per-tick repaint.
    if (onGraphStructureChanged)
        onGraphStructureChanged();

    // A card created mid-gesture (e.g. paste/duplicate while zooming) must join the freeze, or it
    // rasterizes once at the pre-gesture scale and then again at thaw instead of just once.
    if (zoomGestureActive)
        setModuleRasterFrozen(true);

    repaint();
}

void GraphEditor::paint(juce::Graphics& g) {
    // GraphEditor itself can draw a background or overlay if needed
    // But content handles it now.
}

void GraphEditor::paintOverChildren(juce::Graphics& g) {
    // T159: the canvas's focus-region outline, drawn OVER children (unlike the other four focus
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
    // ~10000x10000 virtual canvas — any rect drawn there would land off-screen once the
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

    // ---- Minimap (issue #159) ----
    // Bottom-LEFT with a 12px margin — the mod-matrix panel occupies a 600px panel on the right.
    // Auto-hide when the editor is too small to show it without swallowing the view, but never
    // clobber the user's preference: `minimapVisible` still reflects what they asked for, and
    // resized() just recomputes whether that preference currently fits.
    {
        constexpr int kMargin = 12;
        // Absolute floors, not a fraction of the editor: a fraction-of-self test is always
        // satisfied (w/4 * 2 <= w for any w), so it would never actually hide anything.
        constexpr int kMinEditorW = 480, kMinEditorH = 360;
        const int mmW = juce::jmin(220, getWidth() / 4);
        const int mmH = juce::jmin(150, getHeight() / 4);
        const bool fits = getWidth() >= kMinEditorW && getHeight() >= kMinEditorH;
        minimap.setBounds(kMargin, getHeight() - mmH - kMargin, mmW, mmH);
        minimap.setVisible(minimapVisible && fits);
    }

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

    content.setBounds(0, 0, 10000, 10000);
    content.setTransform(t);
    repaint();

    // Keep the minimap tracking pan/zoom immediately rather than waiting up to 33ms for the next
    // timer tick. Only the viewport rect is pushed here: pan/zoom move what you're LOOKING at, not
    // where the modules and cables are, so rebuilding the full model on every drag frame would
    // re-walk every graph edge for nothing. The 30 Hz tick owns node/cable changes.
    if (minimap.isVisible())
        minimap.setViewport(getVisibleCanvasRect());
}

void GraphEditor::applyZoomAt(float wheelDelta, juce::Point<float> screenAnchor) {
    float oldZoom = zoomLevel;
    zoomLevel += wheelDelta * 0.1f * zoomLevel;
    zoomLevel = juce::jlimit(0.1f, 2.0f, zoomLevel);

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

void GraphEditor::setModuleRasterFrozen(bool frozen) {
    for (auto* comp : content.getModules())
        if (comp != nullptr)
            comp->setRasterFrozen(frozen);
}

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

void GraphEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    applyZoomAt(wheel.deltaY, e.position);
}

juce::Rectangle<float> GraphEditor::getVisibleCanvasRect() const {
    // Rebuilt from zoomLevel/panOffset rather than reading content.getTransform(), so this stays
    // independent of child state and can be const.
    const juce::AffineTransform t = juce::AffineTransform().scaled(zoomLevel, zoomLevel).translated(panOffset);
    return getLocalBounds().toFloat().transformedBy(t.inverted());
}

void GraphEditor::centreViewOn(juce::Point<float> canvasPoint) {
    panOffset = getLocalBounds().getCentre().toFloat() - canvasPoint * zoomLevel;
    updateTransform();
}

void GraphEditor::zoomAroundCentre(float wheelDelta) {
    applyZoomAt(wheelDelta, getLocalBounds().getCentre().toFloat());
}

void GraphEditor::setMinimapVisible(bool shouldBeVisible) {
    minimapVisible = shouldBeVisible;
    // resized() recomputes the effective (preference && fits) visibility.
    resized();
    // Seed the full model on the way in: updateTransform() only pushes the viewport, so without
    // this the map would show an empty canvas until the next 30 Hz tick.
    if (minimap.isVisible())
        minimap.setModel(buildMinimapModel());
}

void GraphEditor::toggleMinimapVisibility() { setMinimapVisible(!minimapVisible); }

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

void GraphEditor::fitViewToModules() {
    // P8-31: after loading a patch, bring every module on-screen. A loaded patch keeps its saved
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
    const float kMinZoom = 0.1f; // matches the wheel-zoom clamp in contentWheelPositionChanged
    const float kMaxZoom = 2.0f;
    scale = juce::jlimit(kMinZoom, kMaxZoom, scale);
    zoomLevel = scale;
    // Centre the (expanded) box within the viewport. screen = content * scale + panOffset.
    float targetCenterX = view.getX() + view.getWidth() * 0.5f;
    float targetCenterY = view.getY() + view.getHeight() * 0.5f;
    float boxCenterX = box.getX() + box.getWidth() * 0.5f;
    float boxCenterY = box.getY() + box.getHeight() * 0.5f;
    panOffset = {(targetCenterX - boxCenterX * scale), (targetCenterY - boxCenterY * scale)};
    updateTransform();
}

bool GraphEditor::hasLocatableMasterOrOutput() const {
    auto& graph = audioEngine.getGraph();
    if (synth::findMasterNode(graph) != nullptr)
        return true;
    for (auto* node : graph.getNodes())
        if (node != nullptr && isTerminalAudioSink(node->getProcessor()))
            return true;
    return false;
}

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

    for (auto* comp : content.getModules()) {
        if (comp != nullptr && comp->getNodeId() == node->nodeID) {
            centreViewOn(comp->getBounds().toFloat().getCentre());
            break;
        }
    }

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
    const bool overChip = !macroChipAt(localPos.roundToInt()).isEmpty();
    if (overChip != hoveringMacroChip) {
        hoveringMacroChip = overChip;
        setMouseCursor(overChip ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    if (overChip)
        return;

    std::optional<CableId> newId;
    if (auto cable = getCableAt(localPos.toFloat()))
        newId = cable->id;

    // Repaint only when the hovered cable actually CHANGES, not on every mouse move. (The canvas
    // already repaints at 30Hz for the wire animation, so this just marks the next frame dirty
    // rather than introducing a new repaint source — see the no-continuous-repaint invariant.)
    const bool changed =
        newId.has_value() != hoveredCableId.has_value() || (newId.has_value() && *newId != *hoveredCableId);
    if (!changed)
        return;

    hoveredCableId = newId;
    setMouseCursor(newId.has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaintCanvas();
}

void GraphEditor::mouseExit(const juce::MouseEvent&) {
    const bool wasHoveringChip = hoveringMacroChip;
    hoveringMacroChip = false;
    if (!hoveredCableId.has_value() && !wasHoveringChip)
        return;
    hoveredCableId.reset();
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
            m.showMenuAsync(juce::PopupMenu::Options());
            return;
        }

        // Right-click inside an expanded macro's hull: the same macro actions the collapsed
        // card's own menu offers (Fix 4/P8-12 follow-up), reachable without collapsing first.
        //
        // The explicit selectMacro() call used to be load-bearing, not cosmetic: buildMacroMenu's
        // "Ungroup" and "Save as Snippet..." items act on the CURRENT SELECTION
        // (ungroupSelection()/onSaveSnippetRequested()), and mouseUp deliberately preserves
        // whatever was selected on a right-click (so the canvas menu's Paste keeps working) — so
        // without selecting the macro here FIRST, those items would have silently acted on
        // whatever was selected before this click instead of the macro the user just right-clicked.
        // Now redundant — buildMacroMenu selects the macro itself before either item runs
        // (founder-review item 4, docs/macros_ports.md §5.8, so the same fix also covers a macro
        // member's own right-click menu) — left in place to keep this fix's diff scoped.
        if (const auto hullMacroId = macroHullAt(canvasPos.roundToInt()); hullMacroId.isNotEmpty()) {
            // T138: captured BEFORE the reselect above, which otherwise destroys any external
            // batch (or a partial subset of this macro's own members, picked for a targeted
            // "Remove Selection from Macro") the user chose before right-clicking this hull —
            // see buildMacroMenu's own comment on addCandidateSelection, and
            // MacroCardComponent::mouseDown's matching right-click branch. Only reselect when
            // there was NO prior selection at all.
            const auto priorSelection = getSelectedNodes();
            if (priorSelection.empty())
                selectMacro(hullMacroId, false);
            buildMacroMenu(hullMacroId, nullptr, &priorSelection).showMenuAsync(juce::PopupMenu::Options());
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

        // Collapse button (founder-review fix G5) - checked BEFORE the chip below, carving its
        // hit zone out of that row explicitly, even though the two rectangles never actually
        // overlap (macroCollapseButtonBounds sits at the row's right end, macroChipBounds at its
        // left). A single click collapses through the SAME setMacroCollapsed the menu's
        // "Collapse" item uses (one undo step - see applyMacroCollapsed), so there is only ever
        // one code path that can collapse a macro. Not gated on Shift the way the chip is: the
        // button is a small fixed target near the hull's top-right corner, not the drag-prone
        // strip the marquee-vs-chip carve-out below exists for.
        if (auto macroId = macroCollapseButtonAt(localPos.roundToInt()); macroId.isNotEmpty()) {
            setMacroCollapsed(macroId, true);
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
        if (auto macroId = macroChipAt(localPos.roundToInt()); macroId.isNotEmpty() && !e.mods.isShiftDown()) {
            selectMacro(macroId, false);
            if (undoManager)
                undoManager->captureBeforeState(audioEngine.getGraph());
            beginSelectionDrag();
            macroChipDragId = macroId;
            macroChipDragStartCanvasPos = localPos.roundToInt();
            pendingEmptyCanvasClick = false;
            return;
        }

        auto attenId = getAttenuverterNodeAt(localPos.toFloat());
        if (attenId.uid != 0) {
            draggingAttenuverterNodeId = attenId;
            if (undoManager)
                undoManager->captureBeforeState(audioEngine.getGraph());
            return;
        }

        draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

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
        dragSelectionBy(canvasPos - macroChipDragStartCanvasPos, nullptr);
        repaintCanvas();
        return;
    }

    if (e.mods.isLeftButtonDown() && !isDraggingConnection) {
        pendingEmptyCanvasClick = false;
        if (draggingAttenuverterNodeId.uid != 0) {
            auto& graph = audioEngine.getGraph();
            auto* node = graph.getNodeForId(draggingAttenuverterNodeId);
            if (node) {
                if (auto* p =
                        dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))) {
                    float delta = (e.getPosition().y - lastMousePos.y) * -0.01f;
                    float currentVal = p->get(); // -1 to 1
                    currentVal = juce::jlimit(-1.0f, 1.0f, currentVal + delta);
                    p->setValueNotifyingHost(p->convertTo0to1(currentVal));
                    repaintCanvas();
                }
            }
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
            finalizeSelectionDrag(); // snaps + de-overlaps the group as one rigid body
            if (undoManager)
                undoManager->pushSnapshotFromCapture(audioEngine.getGraph());
        } else {
            cancelSelectionDrag();
        }
        macroChipDragId.clear();
        repaintCanvas();
        return;
    }

    if (draggingAttenuverterNodeId.uid != 0 && undoManager) {
        undoManager->pushSnapshotFromCapture(audioEngine.getGraph());
    }
    draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();

    // A press on empty canvas that never turned into a pan is a plain click: deselect — UNLESS it
    // landed inside an expanded macro's hull (Fix 2/P8-12 follow-up), in which case it selects
    // that macro instead. Only reachable here at all because a click that landed ON a member
    // module is consumed by that ModuleComponent's own mouseDown and never reaches the canvas —
    // this is deliberately just the empty space inside the hull (between/around member cards),
    // never a drag-to-move-the-macro gesture, so it can't steal the pan gesture.
    if (pendingEmptyCanvasClick) {
        pendingEmptyCanvasClick = false;
        if (e.mods.isPopupMenu())
            return; // right-click keeps the selection so the context menu can act on it

        const auto canvasPos = content.getLocalPoint(this, e.getPosition());
        if (const auto hullMacroId = macroHullAt(canvasPos); hullMacroId.isNotEmpty())
            selectMacro(hullMacroId, false);
        else
            clearSelection();
    }
}
