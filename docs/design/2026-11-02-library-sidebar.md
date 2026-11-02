# Library sidebar — design brief and Codex review (2026-11-02)

The brief sent to Codex (read-only, with src/ attached), then its answer verbatim.

## Brief

### Nell: a sidebar for the library — design review brief

Nell is a C++17 / Qt 6 Quick (Material) EPUB+PDF reader for Omarchy (Hyprland, keyboard-first
desktop). Read `src/Main.qml`, `src/LibraryView.qml`, `src/CatalogView.qml`, `src/StatsPanel.qml`,
`src/HelpOverlay.qml`, `src/Tokens.qml`, `src/Segmented.qml`, `src/IconButton.qml` before answering.

## The complaint (from the owner, today)

"The nav/interaction grammar on the library could be much easier to use — maybe a sidebar with
these options?" Today the library view is: a header row (title, Find field, Sort combo, a cloud
icon button for Catalogs, a stats icon button for the reading record, an "Add Books" button),
a Segmented control All / Reading / Finished, a "Continue Reading" strip, and the cover grid.
Catalogs is a separate full-window view (`CatalogView`) replacing the library (Main.qml
`catalogOpen`), with its own back button; inside it, a list of saved catalogs (five built in:
Project Gutenberg, ManyBooks, Unglue.it, textos.info, Ebooks libres et gratuits), then feeds.
The reading record is a modal popup (`StatsPanel`). Letter shortcuts: S stats, C catalogs,
/ find, Ctrl+O add, ? help. Discoverability is poor: three icon buttons and hidden letters.

## Proposed design

A persistent left sidebar in the non-reading state (library and catalogs), owned by
`Main.qml`, to the left of whichever of `LibraryView` / `CatalogView` is showing. Width ~220 px.
Hidden below 900 px window width (the header's icon buttons and Segmented come back then).

Sections, top to bottom:
1. **Library** — rows: All Books, Reading, Finished. Selecting sets the library's status
   filter and shows the library. (Replaces the Segmented control at wide widths.)
2. **Catalogs** — the section header is itself a row ("Catalogs", opens the catalogs management
   list: add/remove, download folder). Beneath it, one row per saved catalog; clicking opens that
   catalog's feed directly (`catalog.open(i)`), skipping the list. Current catalog highlighted.
3. **Reading** — one row showing the streak ("12-day streak" or "Reading goal"); opens the
   StatsPanel popup as now.
4. Bottom, pinned: **Add Books…** (accent button) and **Shortcuts (?)**.

Keyboard: Tab reaches the sidebar; Up/Down move within it, Enter/Space activate. Letter keys
keep working. The selected row shows the focus ring from Tokens.

Narrow mode (<900 px): sidebar not shown; existing header controls stay.

## Questions — answer each by number, briefly, with a recommendation

1. Is a persistent sidebar the right grammar for a keyboard-first reader on a tiling WM where the
   window is often a half-tile (640–960 px wide)? Or should the sidebar be a collapsible rail
   (icons only) rather than hidden at narrow widths?
2. Should the five built-in catalogs really be listed in the sidebar as rows, or is "Catalogs"
   as one row enough? Consider a user with 10+ Calibre libraries.
3. Should All/Reading/Finished stay as a Segmented control in the content header too, or move
   entirely to the sidebar at wide widths? (Duplication vs. a layout that changes with width.)
4. Is the StatsPanel better as a sidebar-navigated page (like Catalogs) than a modal popup now
   that there's a sidebar?
5. Where should "Add Books" live — sidebar bottom, content header, or both?
6. Focus model: when the user presses C or clicks a catalog row, where does keyboard focus go,
   and how does Esc behave (back to the library grid? to the sidebar?)? Propose rules that don't
   conflict with the existing Shortcut `enabled:` guards (`win.textEntry`, `win.surfaceFocused`).
7. Anything in the existing code that makes this harder than it looks (e.g. `objectName: "shelf"`
   lookups, `Main.qml`'s `catalogOpen` state, `CatalogView.goBack()`), and the simplest way through.
8. What would Apple Books / Calibre / Thorium / Foliate do here, in one line each, and which
   of them is the right reference for Nell.

Keep the answer under 900 words. Recommendations, not surveys.

## Codex's review

1. **Use a labeled sidebar when space permits; a drawer when it doesn’t.** A 220 px sidebar at 900 px leaves 680 px for content—three existing 184 px shelf columns. That’s workable, but make it collapsible even at wide widths. Below 900 px, provide a visible “Navigation” button opening the same labeled sidebar as an overlay. An icon rail repeats the discoverability problem and cannot meaningfully identify catalogs. Retain narrow header controls as quick access.

2. **Start with one “Catalogs” row.** Five built-ins shouldn’t occupy more navigation space than the user’s books. Keep the complete, searchable catalog list in `CatalogView`; optionally add a small, explicitly pinned subset later. Ten Calibre libraries should expand the management list, not the application’s permanent navigation. If children are introduced, give “Catalogs” navigation and its expand/collapse affordance separate targets.

3. **Move the status choices entirely into the sidebar at wide widths.** Restore `Segmented` when the sidebar collapses, with both bound to the same `status`. Keep the current filter visible in the content title: “All Books,” “Reading,” or “Finished.” Responsive relocation is preferable to two simultaneously visible controls for one choice. Preserve the filter and grid position across resizing.

4. **Keep StatsPanel as a popup.** It is a brief check on progress plus one goal setting; it doesn’t yet warrant another destination. Label its sidebar action consistently—“Reading record,” with “12-day streak” as secondary text—rather than changing its identity with the data. Opening it should leave the current navigation selection intact. Consider a page only if history and analysis become substantial tasks.

5. **Sidebar bottom when expanded; header when collapsed.** One persistent Add Books action per layout is enough. Keep the empty-library Add Books button because it explains the immediate next step. Preserve Ctrl+O and drop-to-import everywhere. In catalogs, distinguish “Add Books…” from the content-specific “Add Catalog.”

6. **Separate navigation focus from content focus.**

   - Tab enters the sidebar at its remembered row; Up/Down move focus without navigating; Enter/Space activate. Activation transfers focus to the destination grid/list. Shift+Tab provides a predictable route back.
   - `C` opens the catalog management list and focuses it. A catalog child, if added, opens its root feed and focuses entries. Loading must not repeatedly steal focus.
   - Esc closes the topmost popup/dialog first, restoring its invoker. In catalog content, retain the existing hierarchy: previous feed → catalog list → library grid. In the sidebar, Esc returns focus to current content without navigating. In an overlay drawer, it closes the drawer and restores its opener.
   - Keep selection styling separate from the `Tokens.focus` ring, which indicates actual keyboard focus.
   - Move `C`/`S` to `Main.qml`, enabled only outside reading, text entry, and modal interaction. Preserve the narrower `surfaceFocused` guard for `Q`/`F`; giving sidebar rows `objectName: "shelf"` would accidentally enable quitting there. `!textEntry` alone does not protect dialogs focused on buttons.

7. **This is a modest layout change with a real focus refactor.** Keep `catalogOpen` for now; two content destinations don’t need a router. Add explicit Main functions for library selection, catalog management, and direct catalog opening. Management must call `catalog.leave()`; otherwise `openCatalogs()` can display an existing feed.

   Move `StatsPanel` from `LibraryView` into Main and pass the actual invoker to `show()`, replacing today’s unconditional return to `grid`. Centralize destination focus instead of relying on `CatalogView.onBrowsingChanged` alone: switching catalogs while already browsing doesn’t change that boolean.

   Preserve `goBack()` for content navigation, but prevent its application-wide Esc shortcut from intercepting sidebar or modal interaction. Both views currently fill their parent; place them in a shared content container beside the sidebar. Their existing **800 px content-width** breakpoints should remain separate from the **900 px window-width** sidebar breakpoint. If implementing catalog children, remember `catalog.open(i)` clears feed history and indices shift on removal.

8. **Use these references selectively:**

   - **Apple Books:** borrow labeled destinations and visible goals; its goals live within Home. [Apple guide](https://support.apple.com/en-lamr/guide/books/ibks7c41beea/mac)
   - **Calibre:** borrow scalable filtering from its tag browser, but avoid its management density. [Manual](https://manual.calibre-ebook.com/gui.html)
   - **Thorium:** borrow the explicit distinction between local books and OPDS acquisition. [EDRLab](https://www.edrlab.org/software/thorium-reader/)
   - **Foliate:** use its reading-focused Linux scope as the primary reference, with Nell’s explicit keyboard focus rules. [Project](https://github.com/johnfactotum/foliate)

   **Recommendation: Foliate’s restraint, Apple Books’ navigation clarity, and Thorium’s catalog separation.**