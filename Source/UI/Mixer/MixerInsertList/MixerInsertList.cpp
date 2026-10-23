// Concern: MixerInsertList's rows (folding splits included), bypass, accessibility and paint. The mouse, menus and
// mutations are in MixerInsertListEdits.cpp.
#include "MixerInsertList.h"

#include "AppUndoManager.h"
#include "Modules/ModuleBase.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Mixer/MixerRowsMotion.h"
#include "UI/Mixer/MixerSections/MixerSectionViewport.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <set>

namespace synth::ui {
namespace {

using Kind = synth::MixerInsertEntry::Kind;

constexpr double kFoldMs = 180.0;

// The splits a person opened, by the uuid of the node that fans out. View state, so it lives outside every list:
// the column that shows a split is rebuilt after any recorded edit (bypassing one of its paths included), and the
// docked and detached mixers show the same columns.
std::set<juce::String>& openSplits() {
    static std::set<juce::String> open;
    return open;
}

// Path rails take the accent, then the rose hue, alternating, so two neighbouring paths always differ.
juce::Colour pathColour(const synth::theme::Theme& theme, int path) {
    return path % 2 == 0 ? theme.colors.accent : theme.colors.hueRose;
}

} // namespace

void MixerInsertList::resetSplitOpenStateForTest() { openSplits().clear(); }

MixerInsertList::MixerInsertList() { setInterceptsMouseClicks(true, false); }

MixerInsertList::~MixerInsertList() { foldAnimation_.stop(vblank_); }

void MixerInsertList::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                                GraphEditor& graphEditor) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    macros_ = &macros;
    graphEditor_ = &graphEditor;
}

void MixerInsertList::setEntries(const std::vector<synth::MixerInsertEntry>& entries, bool linear,
                                 juce::AudioProcessorGraph::NodeID sourceNodeId,
                                 juce::AudioProcessorGraph::NodeID stripNodeId) {
    foldAnimation_.stop(vblank_);
    rowDrag_.discard(); // a lifted row cannot outlive the rows it was lifted from
    animatingSplit_ = -1;
    entries_ = entries;
    linear_ = linear;
    hasSplits_ = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e.kind == Kind::Split; });
    sourceNodeId_ = sourceNodeId;
    stripNodeId_ = stripNodeId;
    refreshRows();
}

// ---- Rows ------------------------------------------------------------------------------------------------------

int MixerInsertList::subtreeEnd(int entryIndex) const noexcept {
    const int depth = entries_[(size_t)entryIndex].depth;
    int end = entryIndex + 1;
    while (end < (int)entries_.size() && entries_[(size_t)end].depth > depth)
        ++end;
    return end;
}

bool MixerInsertList::isEntryOpen(int entryIndex) const {
    if (entryIndex == animatingSplit_)
        return true; // its rows stay on screen until a fold lands
    const auto& entry = entries_[(size_t)entryIndex];
    return entry.kind == Kind::Split && openSplits().count(entry.uuid) != 0;
}

void MixerInsertList::rebuildVisibleRows() {
    visible_.clear();
    for (int i = 0; i < (int)entries_.size();) {
        visible_.push_back(i);
        i = entries_[(size_t)i].kind == Kind::Split && !isEntryOpen(i) ? subtreeEnd(i) : i + 1;
    }
}

const synth::MixerInsertEntry* MixerInsertList::entryForRow(int rowIndex) const noexcept {
    return rowIndex >= 0 && rowIndex < (int)visible_.size() ? &entries_[(size_t)visible_[(size_t)rowIndex]] : nullptr;
}

// A row under the split that is folding or opening is as tall as that split's paths show right now.
int MixerInsertList::rowHeight(int rowIndex) const {
    if (animatingSplit_ >= 0 && rowIndex >= 0 && rowIndex < (int)visible_.size()) {
        const int entry = visible_[(size_t)rowIndex];
        if (entry > animatingSplit_ && entry < subtreeEnd(animatingSplit_))
            return juce::roundToInt((float)kRowHeight * animatingAmount_);
    }
    return kRowHeight;
}

int MixerInsertList::rowTop(int rowIndex) const {
    int top = 0;
    for (int i = 0; i < rowIndex && i < (int)visible_.size(); ++i)
        top += rowHeight(i);
    return top;
}

// The listed rows, or the "(no inserts)" placeholder when there are none and nothing to add either.
int MixerInsertList::listedRowsHeight() const {
    if (visible_.empty())
        return hasAddRow() ? 0 : kRowHeight;
    return rowTop((int)visible_.size());
}

int MixerInsertList::contentHeight() const { return listedRowsHeight() + (hasAddRow() ? kRowHeight : 0); }

bool MixerInsertList::isSplitRow(int rowIndex) const noexcept {
    const auto* entry = entryForRow(rowIndex);
    return entry != nullptr && entry->kind == Kind::Split;
}

bool MixerInsertList::isSplitOpen(int rowIndex) const noexcept {
    const auto* entry = entryForRow(rowIndex);
    return entry != nullptr && entry->kind == Kind::Split && openSplits().count(entry->uuid) != 0;
}

int MixerInsertList::getParentSplitRow(int rowIndex) const noexcept {
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr || entry->depth == 0)
        return -1;
    for (int row = rowIndex - 1; row >= 0; --row) {
        const auto* candidate = entryForRow(row);
        if (candidate->kind == Kind::Split && candidate->depth == entry->depth - 1)
            return row;
    }
    return -1;
}

// Opening adds the paths' rows at zero height and grows them; folding shrinks them, then drops them. Either way the
// column re-reads the list's height each frame, so the rows below slide rather than jump.
void MixerInsertList::setSplitOpen(int rowIndex, bool open) {
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr || entry->kind != Kind::Split || isSplitOpen(rowIndex) == open)
        return;
    const int entryIndex = visible_[(size_t)rowIndex];
    const auto uuid = entry->uuid;
    foldAnimation_.stop(vblank_);
    if (open)
        openSplits().insert(uuid);
    else
        openSplits().erase(uuid);

    const bool animate = isShowing() && !animationsOff();
    if (!animate) {
        animatingSplit_ = -1;
        refreshRows();
        notifyHeightChanged();
        return;
    }
    animatingSplit_ = entryIndex;
    animatingOpen_ = open;
    animatingAmount_ = open ? 0.0f : 1.0f;
    refreshRows();
    foldAnimation_.start(
        vblank_, kFoldMs, easeOutCubic,
        [this](float t) {
            animatingAmount_ = animatingOpen_ ? t : 1.0f - t;
            resized();
            repaint();
            notifyHeightChanged();
        },
        [this] {
            animatingSplit_ = -1;
            refreshRows();
            notifyHeightChanged();
        });
}

void MixerInsertList::notifyHeightChanged() {
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

void MixerInsertList::refreshRows() {
    rebuildVisibleRows();
    rebuildRowAccessibilityProxies();
    rebuildBypassButtons();
    resized();
    repaint();
}

bool MixerInsertList::isSplitBypassed(int entryIndex) const {
    bool any = false;
    for (int i = entryIndex + 1; i < subtreeEnd(entryIndex); ++i) {
        const auto& entry = entries_[(size_t)i];
        if (entry.kind != Kind::Module)
            continue;
        if (!entry.bypassed)
            return false;
        any = true;
    }
    return any;
}

// "Split into 2 paths", then one line per path naming its modules in order (a nested split's modules included).
juce::String MixerInsertList::splitTooltip(int entryIndex) const {
    const auto& split = entries_[(size_t)entryIndex];
    std::vector<juce::StringArray> paths((size_t)juce::jmax(0, split.pathCount));
    int path = -1;
    for (int i = entryIndex + 1; i < subtreeEnd(entryIndex); ++i) {
        const auto& entry = entries_[(size_t)i];
        if (entry.depth == split.depth + 1)
            path = entry.path;
        if (entry.kind == Kind::Module && path >= 0 && path < (int)paths.size())
            paths[(size_t)path].add(entry.name);
    }
    juce::String text = "Split into " + juce::String(split.pathCount) + " paths";
    for (size_t p = 0; p < paths.size(); ++p)
        text << "\nPath " << juce::String((int)p + 1) << ": "
             << (paths[p].isEmpty() ? juce::String("dry, no effects") : paths[p].joinIntoString(", "));
    return text;
}

juce::Rectangle<int> MixerInsertList::splitIconBounds(int rowIndex) const {
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr)
        return {};
    const int x = 2 + entry->depth * kIndent + kArrowWidth;
    return {x, rowTop(rowIndex), kSplitIconWidth, rowHeight(rowIndex)};
}

// ---- Bypass ----------------------------------------------------------------------------------------------------

// Rebuilt wholesale with the proxies: the row count and every row's identity can both change on one edit or fold, and
// a button's title and tooltip carry the row's name and bypass state. The tooltip is computed when shown, so a rebound
// key shows up without a rebuild.
void MixerInsertList::rebuildBypassButtons() {
    bypassButtons_.clear();
    showButtons_.clear();
    for (int i = 0; i < (int)visible_.size(); ++i) {
        const auto& entry = entries_[(size_t)visible_[(size_t)i]];
        if (entry.kind == Kind::Module) {
            auto show = std::make_unique<TrackShowModuleButton>();
            show->setTexts("Show " + entry.name + " on canvas",
                           "Show " + entry.name + " on the canvas: selects its card there and brings it into view");
            show->onClick = [this, uuid = entry.uuid] {
                if (onFocusModule)
                    onFocusModule(uuid);
            };
            addAndMakeVisible(*show);
            showButtons_.push_back(std::move(show));
        } else {
            showButtons_.push_back(nullptr);
        }
        if (entry.kind == Kind::DryPath) {
            bypassButtons_.push_back(nullptr); // nothing to bypass: the slot keeps the buttons indexed by row
            continue;
        }
        const bool bypassed = entry.kind == Kind::Split ? isSplitBypassed(visible_[(size_t)i]) : entry.bypassed;
        auto button = std::make_unique<MixerIconButton>("mixerInsertBypass");
        button->setIcon(synth::theme::Icon::ModuleBypass);
        button->setToggleState(bypassed, juce::dontSendNotification);
        button->setTitle(entry.name + " bypass, " + (bypassed ? "on" : "off"));
        button->tooltipProvider = [this, i] { return bypassTooltip(i); };
        button->onClick = [this, i] { toggleBypassForRow(i); };
        addAndMakeVisible(*button);
        bypassButtons_.push_back(std::move(button));
    }
}

juce::String MixerInsertList::bypassTooltip(int rowIndex) const {
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr)
        return {};
    juce::String base;
    if (entry->kind == Kind::Split)
        base = isSplitBypassed(visible_[(size_t)rowIndex]) ? "Turn every path back on" : "Bypass every path";
    else
        base = entry->bypassed ? "Turn " + entry->name + " back on" : "Bypass " + entry->name;
    return formatShortcutHint(base, bypassShortcutText ? bypassShortcutText() : juce::String());
}

// The module's own bypass parameter is part of the graph state, so one captured before/after pair is a complete
// undo step -- for a split row too, which flips every module under it inside that one pair. The change reaches the
// canvas card through the parameter itself; onMutated refreshes every mixer view. The rebuild it triggers destroys
// this list, so nothing may touch it afterwards.
void MixerInsertList::toggleBypassForRow(int rowIndex) {
    const auto* row = entryForRow(rowIndex);
    if (row == nullptr || graph_ == nullptr || undoManager_ == nullptr)
        return;
    const int entryIndex = visible_[(size_t)rowIndex];
    const bool isSplit = row->kind == Kind::Split;
    const int first = isSplit ? entryIndex + 1 : entryIndex;
    const int last = isSplit ? subtreeEnd(entryIndex) : entryIndex + 1;
    const bool target = isSplit ? !isSplitBypassed(entryIndex) : !row->bypassed;

    std::vector<std::pair<int, ModuleBase*>> modules;
    for (int i = first; i < last; ++i) {
        const auto& entry = entries_[(size_t)i];
        if (entry.kind != Kind::Module)
            continue;
        auto* node = graph_->getNodeForId(entry.nodeId);
        if (auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr)
            modules.emplace_back(i, module);
    }
    if (modules.empty())
        return;
    undoManager_->captureBeforeState(*graph_);
    for (auto& [index, module] : modules)
        module->setBypassed(target);
    undoManager_->pushSnapshotFromCapture(*graph_);
    // The rows read right even where nothing rebuilds the list afterwards (a bare list in a test).
    for (auto& [index, module] : modules)
        entries_[(size_t)index].bypassed = module->isBypassed();
    refreshRows();
    if (onMutated)
        onMutated();
}

void MixerInsertList::rebuildRowAccessibilityProxies() {
    // Rebuilt wholesale on every setEntries() and fold (same idiom as MixerSendList::rebuildKnobs()) rather than diffed
    // in place -- the row count and every row's identity can both change on a single insert/remove/reorder/fold, and
    // this list is never large enough for the rebuild itself to matter.
    rowProxies_.clear();
    for (int i = 0; i < (int)visible_.size(); ++i) {
        const int entryIndex = visible_[(size_t)i];
        const auto& entry = entries_[(size_t)entryIndex];
        auto proxy = std::make_unique<RowAccessibilityProxy>();
        // An overlay purely for accessibility -- mouseDown() above still owns every real
        // click via rowIndexAt()/hit-testing on `this`, unchanged.
        proxy->setInterceptsMouseClicks(false, false);
        if (entry.kind == Kind::DryPath)
            proxy->setTitle("Path " + juce::String(entry.path + 1) + ": dry, no effects");
        else if (entry.kind == Kind::Split)
            proxy->setTitle("Split into " + juce::String(entry.pathCount) + " paths, " +
                            (isEntryOpen(entryIndex) ? "open" : "folded") +
                            (isSplitBypassed(entryIndex) ? ", bypassed" : ""));
        else
            proxy->setTitle(entry.bypassed ? entry.name + ", bypassed" : entry.name);
        addAndMakeVisible(*proxy);
        rowProxies_.push_back(std::move(proxy));
    }
    if (hasAddRow()) {
        auto proxy = std::make_unique<RowAccessibilityProxy>();
        proxy->setInterceptsMouseClicks(false, false);
        proxy->setTitle("Add an insert");
        addAndMakeVisible(*proxy);
        rowProxies_.push_back(std::move(proxy));
    }
}

std::vector<juce::String> MixerInsertList::entryKeys() const {
    std::vector<juce::String> keys;
    for (const auto& entry : entries_)
        keys.push_back(MixerRowsMotion::insertKey(entry));
    return keys;
}

std::vector<ExitEnterListRow> MixerInsertList::motionRows() const {
    std::vector<ExitEnterListRow> rows;
    for (size_t i = 0; i < visible_.size(); ++i) {
        const auto bounds = getRowBounds((int)i);
        rows.push_back({MixerRowsMotion::insertKey(entries_[(size_t)visible_[i]]), (float)bounds.getY(),
                        (float)bounds.getHeight()});
    }
    return rows;
}

juce::Rectangle<int> MixerInsertList::getRowBounds(int rowIndex) const {
    if (isAddRow(rowIndex))
        return getLocalBounds().withY(listedRowsHeight()).withHeight(kRowHeight);
    if (rowIndex < 0 || rowIndex >= (int)visible_.size())
        return {};
    return getLocalBounds().withY(rowTop(rowIndex)).withHeight(rowHeight(rowIndex));
}

void MixerInsertList::setFocusedRow(int rowIndex) {
    focusedRow_ = rowIndex >= 0 && rowIndex < getRowCount() ? rowIndex : -1;
    if (focusedRow_ >= 0)
        if (auto* viewport = findParentComponentOfClass<MixerSectionViewport>()) {
            const auto row = getRowBounds(focusedRow_);
            viewport->revealRange(row.getY(), row.getBottom());
        }
}

juce::String MixerInsertList::describeRow(int rowIndex) const {
    if (isAddRow(rowIndex))
        return "Add an insert";
    const auto* entry = entryForRow(rowIndex);
    if (entry == nullptr)
        return {};
    const auto prefix = "Insert " + juce::String(rowIndex + 1) + ", ";
    if (entry->kind == Kind::DryPath)
        return prefix + "path " + juce::String(entry->path + 1) + ": dry, no effects";
    if (entry->kind == Kind::Split) {
        const int entryIndex = visible_[(size_t)rowIndex];
        return prefix + "split into " + juce::String(entry->pathCount) + " paths, " +
               (isEntryOpen(entryIndex) ? "open" : "folded") + (isSplitBypassed(entryIndex) ? ", bypassed" : "");
    }
    return prefix + entry->name + (entry->depth > 0 ? ", path " + juce::String(entry->path + 1) : juce::String()) +
           (entry->bypassed ? ", bypassed" : "");
}

juce::String MixerInsertList::getRowUuid(int rowIndex) const {
    const auto* entry = entryForRow(rowIndex);
    return entry != nullptr && entry->kind == Kind::Module ? entry->uuid : juce::String();
}

juce::String MixerInsertList::tooltipAt(juce::Point<int> mouse) const {
    if (gapTargetAt(mouse))
        return "Add an effect here";
    const int row = rowIndexAt(mouse);
    if (isAddRow(row))
        return "Add an effect at the end of this chain";
    if (const auto* entry = entryForRow(row)) {
        if (entry->kind == Kind::DryPath)
            return "Path " + juce::String(entry->path + 1) + " is wired straight through: click to add an effect to it";
        if (entry->kind == Kind::Split)
            return splitIconBounds(row).contains(mouse) ? juce::String("Split") : splitTooltip(visible_[(size_t)row]);
        return entry->name + (isEditable()
                                  ? ": drag to reorder, double-click to show it on the canvas, right-click for more"
                                  : ": a branching chain, edit it on the canvas");
    }
    if (!isEditable())
        return "A branching chain: edit it on the canvas";
    return "Insert chain";
}

int MixerInsertList::getPreferredHeight() const noexcept { return contentHeight(); }

int MixerInsertList::rowButtonsWidth(const synth::MixerInsertEntry& entry) const noexcept {
    return entry.kind == Kind::Module ? kBypassWidth + kShowWidth : kBypassWidth;
}

int MixerInsertList::rowIndexAt(juce::Point<int> position) const {
    if (position.y < 0)
        return -1;
    int top = 0;
    for (int i = 0; i < (int)visible_.size(); ++i) {
        top += rowHeight(i);
        if (position.y < top)
            return i;
    }
    if (hasAddRow() && position.y >= listedRowsHeight() && position.y < listedRowsHeight() + kRowHeight)
        return getAddRowIndex();
    return -1;
}

// ---- Paint -----------------------------------------------------------------------------------------------------

// One row, drawn at its place in the drag (its static slot when nothing is being reordered).
void MixerInsertList::paintRow(juce::Graphics& g, int i, float lift) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto text = theme.colors.textPrimary;
    const auto muted = theme.colors.textMuted;
    const auto disabled = theme.colors.textDisabled;
    const int entryIndex = visible_[(size_t)i];
    const auto& entry = entries_[(size_t)entryIndex];
    const auto row = drawBounds(i);
    if (row.getHeight() <= 0)
        return;
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(row);
    // A growing row reveals from the top: lay it out at full height and let the clip show what fits.
    auto full = row.withHeight(kRowHeight);
    paintReorderLift(g, full.toFloat(), lift, theme.colors.surfaceHi, theme.colors.accent);
    for (int level = 1; level <= entry.depth; ++level) {
        // One rail per level, in the colour of the path this row sits on at that level.
        int path = entry.path;
        if (level < entry.depth)
            for (int j = entryIndex - 1; j >= 0; --j)
                if (entries_[(size_t)j].depth == level) {
                    path = entries_[(size_t)j].path;
                    break;
                }
        g.setColour(pathColour(theme, path).withAlpha(0.6f));
        g.fillRect(2 + (level - 1) * kIndent + 4, full.getY(), 2, kRowHeight);
    }
    auto content = full.withTrimmedLeft(2 + entry.depth * kIndent).withTrimmedRight(rowButtonsWidth(entry));
    if (entry.kind == Kind::Split) {
        const float openness = entryIndex == animatingSplit_ ? animatingAmount_ : (isEntryOpen(entryIndex) ? 1.f : 0.f);
        auto arrow = content.removeFromLeft(kArrowWidth).toFloat().withSizeKeepingCentre(7.0f, 7.0f);
        synth::theme::paintDisclosureChevron(g, arrow, openness, theme, false);
        auto iconArea = content.removeFromLeft(kSplitIconWidth).toFloat().reduced(1.0f, 2.0f);
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        if (const auto* icon = lf != nullptr ? lf->peekIcon(synth::theme::Icon::MixerSplit) : nullptr)
            icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred, 1.0f);
        g.setColour(isSplitBypassed(entryIndex) ? disabled : text);
        g.drawText(entry.name, content.reduced(2, 0), juce::Justification::centredLeft, true);
    } else if (entry.kind == Kind::DryPath) {
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::italic)));
        g.drawText(entry.name, content.withTrimmedLeft(kArrowWidth).reduced(2, 0), juce::Justification::centredLeft,
                   true);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
    } else {
        g.setColour(entry.bypassed ? disabled : text);
        g.drawText(entry.name, content.reduced(2, 0), juce::Justification::centredLeft, true);
    }
}

void MixerInsertList::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);

    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    if (visible_.empty() && !hasAddRow()) {
        g.setColour(theme.colors.textMuted);
        g.drawText("(no inserts)", getLocalBounds().removeFromTop(kRowHeight), juce::Justification::centredLeft, true);
    }
    // The lifted row is drawn last, so it sits over the rows gliding past it.
    const int lifted = liftedRow();
    for (int i = 0; i < (int)visible_.size(); ++i)
        if (i != lifted)
            paintRow(g, i, 0.0f);
    if (lifted >= 0)
        paintRow(g, lifted, rowDrag_.animator().getLift());
    const auto accent = theme.colors.accent;
    if (hasAddRow()) {
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("+ Insert", getRowBounds(getAddRowIndex()).reduced(4, 0), juce::Justification::centredLeft, true);
    }
    if (hoverGap_ >= 0) {
        // The line between the two rows, with a "+" in the middle: an add lands right there.
        const int y = hoverGap_ < (int)visible_.size() ? rowTop(hoverGap_) : listedRowsHeight();
        const auto line = juce::Rectangle<int>(4, y - 1, getWidth() - kBypassWidth - 8, 2);
        g.setColour(accent);
        g.fillRect(line);
        const auto dot = juce::Rectangle<float>(14.0f, 14.0f).withCentre(line.getCentre().toFloat());
        g.fillEllipse(dot);
        g.setColour(theme.colors.bg0);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText("+", dot.translated(0.0f, -0.5f), juce::Justification::centred, false);
    }
}

void MixerInsertList::resized() { placeRows(); }

// Every proxy and bypass button follows its row's drawn place, so a lifted row carries its controls along. Runs on
// each frame of a drag: a setBounds to the same bounds is free.
void MixerInsertList::placeRows() {
    for (int i = 0; i < (int)rowProxies_.size(); ++i)
        rowProxies_[(size_t)i]->setBounds(drawBounds(i)); // the last one is "+ Insert" when there is one
    const int lifted = liftedRow();
    for (int i = 0; i < (int)bypassButtons_.size(); ++i) {
        if (bypassButtons_[(size_t)i] == nullptr)
            continue;
        const auto row = drawBounds(i);
        bypassButtons_[(size_t)i]->setBounds(row.withHeight(kRowHeight).removeFromRight(kBypassWidth).reduced(1));
        bypassButtons_[(size_t)i]->setVisible(row.getHeight() >= kRowHeight / 2);
        if (i == lifted)
            bypassButtons_[(size_t)i]->toFront(false);
        if (auto* show = showButtons_[(size_t)i].get()) {
            show->setBounds(
                row.withHeight(kRowHeight).withTrimmedRight(kBypassWidth).removeFromRight(kShowWidth).reduced(0, 1));
            show->setVisible(row.getHeight() >= kRowHeight / 2);
            if (i == lifted)
                show->toFront(false);
        }
    }
    repaint();
}

} // namespace synth::ui
