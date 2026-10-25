# UI review — GPT-6 Astra via Codex, 2026-10-24

Read-only review of v0.3.0 (`71021ae`) with both screenshots attached (`-i docs/reader.png -i docs/library.png`), prompted by Puck; prompt in the commit body. Companion to `2026-10-24-astra-design-review.md`, which it was told not to repeat. Unedited model output.

---

Nell’s reader captures the calm of Apple Books; the library captures its structure more than its polish.  
The strongest decisions are the restrained page, cover-led navigation, and compact annotation tools.  
The biggest daily-use weaknesses are keyboard focus, narrow-window layouts, and hidden interaction affordances.  
Theme integration is partial: reading pages follow preferences, while most supporting UI remains hard-coded dark.  
Prioritize predictable interaction and responsive layouts before adding more features.

This is a read-only review of both screenshots, all 17 QML files, and the C++ surface exposed to QML. Findings are based on static inspection, not a live interaction test. The existing architecture review’s storage, migration, threading, and fit-text analysis are excluded.

**1. High — Keyboard shortcuts lack a reliable interaction boundary**

[Main.qml:43](/home/janv/Work/nell/src/Main.qml:43) registers application-wide `Q`, `F`, and `?` without checking text-entry focus. [ReaderView.qml:137](/home/janv/Work/nell/src/ReaderView.qml:137) suppresses navigation only for NoteEditor and SearchPanel: Contents, Appearance, Definition, and Help leave page-turn shortcuts enabled.

Consequently, arrows and Space remain eligible to turn pages while users operate controls. The unconditional character shortcuts also create a text-entry conflict risk; exact dispatch needs runtime verification. Help is a mouse-catching Rectangle, not a keyboard-modal surface ([HelpOverlay.qml:7](/home/janv/Work/nell/src/HelpOverlay.qml:7)).

**Recommendation:** enable reader navigation only when the reading surface owns focus and no overlay is active. Disable unmodified application shortcuts during text entry; retain `F11` and use `Ctrl+Q` universally. Make Help a focus-owning Popup, and restore focus to the invoking control after dismissal. This matters more for Omarchy users than additional shortcut coverage.

**2. High — Half-width tiles expose structural layout problems**

The library header keeps a 240-pixel search field, 150-pixel sort control, title, and Add Books button in one row ([LibraryView.qml:74](/home/janv/Work/nell/src/LibraryView.qml:74)). Continue Reading always divides available width by three, even when displaying fewer books ([LibraryView.qml:131](/home/janv/Work/nell/src/LibraryView.qml:131)); each card reserves 96 pixels for its cover plus 48 pixels of margins and spacing ([ContinueCard.qml:19](/home/janv/Work/nell/src/ContinueCard.qml:19)). At roughly 680 pixels wide, these choices leave little usable title space.

Appearance lists every available font without scrolling or a viewport-height limit ([AppearancePanel.qml:109](/home/janv/Work/nell/src/AppearancePanel.qml:109)). Its height therefore depends on installed fonts and can exceed a 768-pixel laptop viewport. The reader footer reserves 400 pixels for labels before spacing and the slider ([ReaderView.qml:355](/home/janv/Work/nell/src/ReaderView.qml:355)).

**Recommendation:** below 800 logical pixels, put search on a second row, use one full-width Continue card, and collapse the footer’s right label to content width. Cap Appearance at `window.height - 88` with scrolling. Support 480-pixel-wide tiles instead of the current 640 minimum, once these layouts adapt ([Main.qml:10](/home/janv/Work/nell/src/Main.qml:10)).

**3. High — Keyboard-first stops before selection and several basic actions**

PageView supports pointer selection and double-click word selection but exposes no keyboard selection operation; the QML focuses it without adding caret navigation ([ReaderView.qml:194](/home/janv/Work/nell/src/ReaderView.qml:194), [pageview.h:47](/home/janv/Work/nell/src/pageview.h:47)). Thus `H`, `N`, and `D` depend on first using a mouse.

ContinueCard is a Rectangle with a TapHandler, progress cycling is a Label with a TapHandler, and the scrubber explicitly refuses focus ([ContinueCard.qml:73](/home/janv/Work/nell/src/ContinueCard.qml:73), [ReaderView.qml:269](/home/janv/Work/nell/src/ReaderView.qml:269), [ReaderView.qml:373](/home/janv/Work/nell/src/ReaderView.qml:373)). Not every action is keyboard reachable.

**Recommendation:** provide an `F7` caret mode with Shift-selection and word-wise movement. Make Continue cards and progress controls real buttons; enable slider keyboard focus and add `Ctrl+G` for direct page/location navigation. Preserve the existing library arrow navigation and visible cover selection ring—they are good foundations.

**4. High — Panels can coexist without a coherent stacking contract**

Contents and Search are independently opened Drawers. Appearance is another independent Popup ([ReaderView.qml:397](/home/janv/Work/nell/src/ReaderView.qml:397)). Opening one does not close another. `closeTopmost()` uses a fixed component order, rather than actual opening order ([ReaderView.qml:104](/home/janv/Work/nell/src/ReaderView.qml:104)). There is no explicit policy governing focus or which panel owns navigation.

Search keeps its drawer open while jumping to matches ([SearchPanel.qml:72](/home/janv/Work/nell/src/SearchPanel.qml:72)); Contents closes after navigation. Both behaviors can make sense, but the distinction needs deliberate presentation.

**Recommendation:** allow one primary panel—Contents, Search, or Appearance—at a time. Give each an explicit close button, initial focus, and focus-return target. Keep Search open for arrow-key previews, but make Enter accept the result and close it. On narrow tiles, use a modal sheet; on wide windows, a nonmodal search sidebar would preserve more useful reading context.

**5. High — Accessibility and focus treatment are incomplete**

IconButton supplies a tooltip but no explicit accessible name ([IconButton.qml:18](/home/janv/Work/nell/src/IconButton.qml:18)). Swatches, theme choices, and segmented options use custom content without explicit names or selected-state semantics. AccentButton and Segmented replace their backgrounds without drawing keyboard focus ([AccentButton.qml:10](/home/janv/Work/nell/src/AccentButton.qml:10), [Segmented.qml:27](/home/janv/Work/nell/src/Segmented.qml:27)). No QML file supplies `Accessible` metadata.

Beyond the painted-text limitation already identified by the architecture review, the surrounding controls themselves need attention. Changing reading font size does not scale the numerous 10–13-pixel UI labels.

**Recommendation:** add accessible names, roles, checked states, and a consistent 2-pixel focus outline with 2-pixel separation. Name swatches “Yellow highlight,” etc.; label the size buttons “Decrease text size” and “Increase text size.” Offer 100–200% UI scaling independently of book typography. Provide reduced motion: page changes currently translate 26 pixels and fade from 25% opacity over 200 ms ([ReaderView.qml:223](/home/janv/Work/nell/src/ReaderView.qml:223)).

**6. Medium-high — Chrome is quiet only until the pointer moves**

Any pointer movement shows chrome, then a 2.6-second timer hides it ([ReaderView.qml:130](/home/janv/Work/nell/src/ReaderView.qml:130), [ReaderView.qml:281](/home/janv/Work/nell/src/ReaderView.qml:281)). A timer firing while a panel or toolbar is occupied does nothing and does not restart itself. Closing a panel can therefore leave chrome visible until another movement restarts the cycle.

Page turns occupy invisible outer 28% zones; center clicks toggle chrome ([ReaderView.qml:206](/home/janv/Work/nell/src/ReaderView.qml:206)). The cursor remains an I-beam over ordinary text, giving no navigation clue.

**Recommendation:** reveal chrome on center click, keyboard focus, or movement within 48 pixels of the top/bottom edge. Restart hiding after panel closure or toolbar exit; never hide a focused control. Show subtle previous/next chevrons on margin hover, with 40-pixel targets. Keep the broad click zones as shortcuts, not the sole visible navigation affordance.

**7. Medium — Progress is useful but unnecessarily secret**

The requested “time / page / percent” cycle is not the implemented cycle. [ReaderView.qml:57](/home/janv/Work/nell/src/ReaderView.qml:57) cycles chapter time, book time, chapter pages remaining, and EPUB locations/PDF pages. Percent appears separately only in the quiet footer; the visible-chrome footer instead shows page counts.

The bottom-left control looks like inert status text, and only its quiet version gets a pointer cursor. Readers cannot discover its alternatives without experimenting.

**Recommendation:** use a focusable status button with tooltip “Reading progress—click to change.” Add a menu containing five named modes, including percentage, plus `P` to cycle them. Keep the chosen mode stable across chrome states. Give it a minimum 32-pixel hit height and keep page/location terminology explicit.

**8. Medium — Theme colors are sound; supporting surfaces break the system**

The five fixed theme pairs and desktop-derived Omarchy pair are clearly defined ([AppearancePanel.qml:16](/home/janv/Work/nell/src/AppearancePanel.qml:16)). Quiet and Night deliberately avoid pure-white body text, which suits sustained reading. However, quiet footer labels reduce that foreground to 45% opacity at 12 pixels ([ReaderView.qml:263](/home/janv/Work/nell/src/ReaderView.qml:263)), substantially weakening contrast.

The library is always `#0e0e10`; drawers are `#18181b`; popups are `#1f1f23`; Material is globally dark ([Main.qml:16](/home/janv/Work/nell/src/Main.qml:16)). Contents/Search even accept page colors without using them. Omarchy integration therefore follows accent and rounding more consistently than surfaces.

**Recommendation:** define shared surface, elevated-surface, primary-text, secondary-text, border, and focus tokens. Derive reader panels from the chosen reading theme and library surfaces from the desktop. Use opaque secondary colors targeting at least 4.5:1 contrast. Consolidate duplicated theme/highlight palettes; retain semantic annotation colors rather than replacing everything with the accent.

**9. Medium — The reader looks book-like; the shelf still looks like a utility**

In the reader screenshot, the warm paper, serif body, modest header, and restrained fold convincingly support the Apple Books ambition. Justified spacing creates noticeable rivers, however, particularly in the right column. [AppearancePanel.qml:162](/home/janv/Work/nell/src/AppearancePanel.qml:162) already offers the necessary alignment choice.

The library screenshot has clear hierarchy but heavy rectangular Continue cards, tiny shelf metadata, and a strong yellow action competing with covers. The selected cover outline is clipped at the shelf’s left edge. Fixed 156×234 cover boxes crop portrait artwork ([LibraryView.qml:159](/home/janv/Work/nell/src/LibraryView.qml:159), [BookCover.qml:86](/home/janv/Work/nell/src/BookCover.qml:86)). It is recognizable as a bookshelf, but lacks the optical care of the reference intent.

**Recommendation:** default to ragged-right text; preserve justification as an option. Use 14-pixel shelf titles and 12-pixel metadata, preserve complete cover artwork, and reserve 6 pixels around focus outlines. Keep the coherent 1.8-unit icon stroke ([Icon.qml:40](/home/janv/Work/nell/src/Icon.qml:40)). Standardize spacing around an 8-pixel rhythm instead of the current assortment of 14, 18, 20, and 24.

**10. Medium — Annotation tools are compact, but recovery and editing are hidden**

The screenshot’s selection bar is efficient, though visually dense. Every color has the same “Highlight (H)” tooltip, while `H` always chooses yellow ([SelectionMenu.qml:55](/home/janv/Work/nell/src/SelectionMenu.qml:55)). Note creation immediately creates a yellow highlight before opening the editor; Cancel closes without removing that provisional highlight ([ReaderView.qml:87](/home/janv/Work/nell/src/ReaderView.qml:87)).

Notes rows navigate rather than offering direct editing. Words rows similarly navigate to context rather than expanding their truncated definitions ([ContentsPanel.qml:179](/home/janv/Work/nell/src/ContentsPanel.qml:179), [ContentsPanel.qml:250](/home/janv/Work/nell/src/ContentsPanel.qml:250)).

**Recommendation:** remember the last highlight color, name every swatch, and use 36-pixel targets. Remove a newly created provisional highlight when note creation is cancelled. Add explicit Edit Note and Show Definition actions. Retain DefinitionCard’s loading indicator and “Saved to Words” feedback; add Retry alongside dictionary errors ([DefinitionCard.qml:55](/home/janv/Work/nell/src/DefinitionCard.qml:55)).

**11. Medium — Library scale and operational feedback need a second pass**

The empty state clearly explains EPUB/PDF and drag-and-drop. Search and Recent/Title/Author sorting are sensible starting points ([LibraryView.qml:85](/home/janv/Work/nell/src/LibraryView.qml:85)). For hundreds of books, however, controls scroll away with the grid header, and there are no reading-status filters or compact list alternative. On 4K, fixed small covers simply multiply across the width; the reader’s capped text columns are better controlled ([ReaderView.qml:30](/home/janv/Work/nell/src/ReaderView.qml:30)).

Missing files receive a label but no Locate action. Failed opens use the same 4.5-second dismissible toast as routine success ([Main.qml:23](/home/janv/Work/nell/src/Main.qml:23), [Toast.qml:31](/home/janv/Work/nell/src/Toast.qml:31)). Import has no progress presentation; undo state is exposed by C++ but has no visible action.

**Recommendation:** pin library search/sort, add All/Reading/Finished filters and a compact list view. Show batch results as “Added 18 books; 2 failed,” with expandable errors. Give missing books Locate File, keep actionable failures until dismissed, and show a six-second Undo action after annotation deletion. Retain the uncluttered window without adding a custom titlebar; actual decoration behavior should be verified under Hyprland.

**Do first**

1. Establish keyboard focus ownership and suppress conflicting shortcuts.
2. Fix 640–800-pixel layouts and constrain Appearance to the viewport.
3. Enforce one primary panel with predictable dismissal and focus restoration.
4. Make selection, progress, and navigation keyboard accessible.
5. Add shared theme/focus tokens and actionable error/undo feedback.