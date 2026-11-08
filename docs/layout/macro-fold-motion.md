# Macro fold motion

How a macro folds its modules into its closed card and unfolds them out of it, with the cables that cross its
border and the neighbours it pushes aside. Part of the canvas motion rules in [animation.md](animation.md).

Collapsing a macro folds its modules into the closed card; expanding unfolds them out of it. The model change is
still instant and one undo step (`MacroGroupController::applyMacroCollapsed`); the flight is a paint-only layer in
`MacroFoldAnimator` (`Source/UI/Graph/MacroFoldAnimator/`), owned by `CardGlideAnimator` (`fold()`), drawn by the same
overlay, and timed by the pure `MacroFoldTimeline.h` (like `ExitEnterTimeline.h`, unit-tested without components).

- **Collapse:** each member flies (position and size, `easeInCubic`, 200 ms) from its card to its own preview box on
  the closed card, which sits at the group's top-left. Members leave 35 ms apart; for a big macro the stagger shrinks
  so the last one still lands by 320 ms. The macro's dashed border shrinks in parallel. Each ghost cross-fades from the
  picture of its card to its category-coloured box in the last part of its flight, and the real card takes over from
  the ghosts in the last 80 ms.
- **Expand:** the exact reverse (the module that left last comes out first), with the 8% bounce
  (`easeOutBackGrow`) as each lands. The border grows ahead of the modules and is done by about 60% of the flights.
  A module's real card shows the moment its ghost lands.
- **The border always holds every module:** it is drawn as the card-to-border blend unioned with every module's
  current rect (`macro_fold::outlineRect`), so no frame shows a module outside it. An expanding macro's border is
  returned by `GraphEditor::paintedMacroHullBounds` (so the chip and buttons ride it); a collapsing macro's dashed
  border is drawn by the fold itself, since the macro is already collapsed.
- **Landing is exact:** the preview boxes come from one pure function, `macro_preview::boxes`
  (`MacroPreviewLayout.h`), used by `MacroCardComponent::paint` and by the fold, so a module lands on the box the card
  then draws.
- **Cables:** a cable with both ends on a collapsing module rides the two flying ghosts and fades as they reach their
  boxes. When collapsing, a cable from a flying module to one of the macro's ports (the inside half of a crossing
  cable) rides its module and fades the same way, since the closed card no longer draws it. When expanding, a cable that
  touches a module is left out of the canvas' cable memo (`MacroFoldAnimator::applyTo`) and is not drawn until that
  module lands (the later one, when both ends are modules), then draws out of its port over 160 ms. That draw-out runs
  past the 320 ms cap by up to 160 ms; the modules never do.
- **A cable crossing the border slides with it.** A cable drawn before and after the fold whose end on the macro moved
  (the closed card's jack on one side, the open border's port on the other) is drawn by the fold, its end following
  the card-to-border blend (`MacroFoldAnimator::Plan::slides`, built by `FoldTreeBuilder::addSlides`): it reaches the
  border's port as the port widget shows (about 60% of the flights) and the card's jack as the card takes over. Only
  an end within a jack's reach of the card or border slides; the far end stays where the canvas puts it.
- **A macro pushed aside glides whole.** The macros the fold pushes away (`makeRoomFor`) or lets come back glide in the
  same `CardGlideAnimator::Scope`: a folded one as its card, an open one as its member cards plus its border, chip and
  port strips (`MacroGroupController::setBorderGlideHook`, the canvas' `glideHullsFrom`, 220 ms), so no frame shows a
  border at its new place around cards still on their way. The folding macro, its ancestors and its nested macros
  are left to the fold. Reduce Motion and Off move them at once.
- **Held cards are invisible**, not just transparent: the closed card while collapsing, each member's card until it
  lands while expanding. An invisible card can be neither clicked, focused nor read by a screen reader, and
  `syncMacroCards` decides visibility again when the fold ends.
- **Port widgets** of an unfolding macro dock to the open border at once, so they are held invisible with the module
  cards and shown when the border has grown to them (about 60% of the flights).
- **Fold and Pack Macros** (Cmd+Ctrl+G, [details](../macros/menu-and-membership.md#fold-and-pack-macros-cmdctrlg))
  adds no motion of its own: the model is final at once, the folds fly to the packed card positions, and a selected
  card that was already folded glides there (160 ms) in the same glide scope. With "Packing also tidies the rest of the
  canvas" the Auto Arrange glide joins the same scope.
- **Grouping folds too.** Grouping a selection into a macro (Cmd+G, the menu, the canvas context menu: all end in
  `MacroGroupController::groupSelectionIntoMacro`) creates the macro already closed, so it folds exactly like a later
  collapse of the same group: the modules fly from where they stood into the new card at the footprint's top-left, and
  a neighbour pushed aside to make room glides (one `CardGlideAnimator::Scope`). The new macro has no entry in the
  `snapshotFoldState()` taken before, so `foldChangedMacros()` treats a collapsed macro that is not in the snapshot as
  "was open, now closed", built from the snapshot's per-module rects and pictures (`FoldSnapshot::nodes`). Redo of a
  grouping goes through the same path. Undo of a grouping, and the Ungroup command on a closed macro, unfold:
  a closed macro in the snapshot that no longer exists, whose modules stand on the canvas again, gets an expand plan
  from the boxes it had on its card to the modules' cards (`unfoldRemovedMacro`); the removed macro's card is drawn by the fold, so it gets no shrinking exit ghost and the neighbours' (and Master's) glide runs with the unfold, not after it; no border is drawn for the macro that
  is gone. Track creation (`addMacroForMembers`) stays instant: its modules are new.
- **Several macros at once** (a multi-macro toggle, an undo step that flips more than one) fold together: the toggle
  takes one `snapshotFoldState()` and starts every plan in one `foldChangedMacros()`.
- **Pictures come from the cards' rasters** (`ZoomFrozenCachedImage::lastRaster`); a card that was never painted (a
  macro expanded straight after a project load) is rendered once, up to 16 modules, and past that flies as a plain box.
- **Undo and redo animate too.** `AppUndoManager::applyHistoryStep` takes `snapshotFoldState()` before the restore and
  calls `foldChangedMacros()` after it, which folds every macro whose collapsed state flipped, all in one animation.
  Those modules keep out of the delete and undo ghosts (`CardGlideAnimator::dropGhostsFor`).
- **Interruption:** any pass that re-syncs the cards (`syncMacroCards`: a second toggle, a rename) lands the running
  fold at once first.
- **Nested macros** fold as one box of their parent (the parent's card previews a nested macro as one box, see
  [the collapsed card](macro-cards.md#the-collapsed-card)), and each keeps its own collapsed flag, so it comes back
  as it was left. Each level is its own plan with its own clock (`Plan::delayMs`), built parent-first by
  `FoldTreeBuilder` in `MacroGroupControllerFold.cpp`; only the outermost macro whose state changed starts one.
  - *Collapsing a parent with an open child:* the child folds first, exactly like a macro of its own (its modules fly
    into the card it would fold into, `foldedCardBounds`, and its dashed border shrinks to it). As it hands over, its
    border and boxes fade while a picture of that card fades in, and then the parent's flight starts: the card flies
    into its box on the parent's card together with the parent's own modules. A folded child just flies in as its card.
  - *Expanding the parent:* the reverse. The child's box flies out to its card; an open child then unfolds out of it
    (its border is not drawn until then), and its picture fades as its modules start. A folded child's real card shows
    the moment its box lands, and it stays folded.
  - *Timing:* the levels share the one 320 ms cap (each runs on a clock 1/N as long, `Timeline::scale`), however deep; an
    expand adds the 160 ms cable draw-out after the last landing. A cable from a child's module to one of its
    parent's belongs to the child's plan: folding, it fades with the child's own cables; unfolding, it waits for the
    child's module.
  - Fold and Pack over a whole parent folds the child inside it the same way, and undo and redo play the same
    motion. Reduce Motion is one 80 ms fade for every level; Animations Off and an off-screen canvas are instant.
- **Reduce Motion:** no flight; the card that appears (the closed card, or the members) fades in over 80 ms. Animations
  Off, and an off-screen canvas, land at once (`Hooks::canAnimate`; tests force it with `setForceAnimateForTest` and
  step `MacroFoldAnimator::applyAtMs`).
- **A frame repaints only the fold's area** (its border, ghosts and the cables it draws), never the whole canvas.
- **One owner per drawn thing per frame.** In every frame each card on screen is drawn once: by its real card, by the
  glide's picture of it (the real card at alpha 0), or by the fold (the real card held invisible); and each cable is
  drawn once, by the canvas or by the fold. The fold and the glide paint in the same overlay pass, the fold first, so
  every drawer sets its own opacity before drawing a picture: JUCE draws an image at the alpha of the last colour set,
  and a glide picture drawn after the fold's fading boxes and cables used to inherit their alpha, so the neighbours
  an opening macro pushed aside faded to a shadow and snapped back when the motion ended. The fold also paints inside
  its own saved state. Guards: `CardGlideMacroPushOpenNeighbour.*` and `MacroFoldCableSlideOpenNeighbour.*`
  (`Tests/UI/Graph/MacroFold/MacroFoldOpenNeighbourTests.cpp`) open the fro830 test project
  (`Tools/ui-verify/fixtures/fro830-test.agsproj`: a macro opening into an open neighbour, cables crossing both
  borders), then open Macro, open Macro2 and undo the open, rendering every 20 ms frame under Full motion, Reduce
  Motion and Off. They fail on a card or cable that is missing or drawn twice, and on a gliding card drawn dimmer than
  it is at rest.
