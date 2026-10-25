# Design review — GPT-6 Astra via Codex, 2026-10-24

Read-only review of v0.3.0 (`7bfc8d0`), prompted by Puck; prompt in the commit body. Unedited model output.

---

Nell’s main design risk is losing trust in saved reading state, not lacking features.  
The startup migration makes harmless commands mutate user data and needs immediate correction.  
The storage format has version labels but no compatibility enforcement or concurrent-writer protection.  
The reading engine couples layout, rendering, navigation, and persistence tightly enough that feature growth will amplify existing bugs.  
Stabilize those contracts before expanding the Apple Books/Kindle feature set.

This is a static, read-only review; I changed no files and did not run the write-producing test suite. One scope correction: [prompt.md:1](/home/janv/Work/nell/prompt.md:1) is a release handoff, not the original product brief. Product judgments below use the README’s stated promises and the available implementation.

**1. High — Startup migration is an uncontrolled, partially recoverable transaction**

[main.cpp:30](/home/janv/Work/nell/src/main.cpp:30) calls `adoptQuire()` before parsing arguments. Consequently, `--version`, help, invalid arguments, and read-only CLI commands can move user data. The accident recorded in the handoff is the expected consequence of that ordering. Requiring developers to redirect XDG directories merely works around the defect.

[ library.cpp:40](/home/janv/Work/nell/src/library.cpp:40) independently renames config, cache, and data directories, ignores the settings-file rename result, and rewrites cover paths only when both cache and data moved during that invocation. An interruption between operations leaves a state that rerunning does not reliably repair. Directory existence is treated as proof of completed migration; an empty destination can block adoption.

**Recommendation:** classify and validate commands before touching storage, with help/version handled before GUI initialization. Run migration only during an explicit migration command or deliberate library initialization. Copy into staging, validate JSON and settings, retain the source, and record resumable completion per component. Treat cache as disposable and regenerate it. Report failures instead of silently proceeding with a partly migrated installation.

**2. High — Schema versions currently provide no protection against destructive reads**

[ library.cpp:265](/home/janv/Work/nell/src/library.cpp:265) and [library.cpp:291](/home/janv/Work/nell/src/library.cpp:291) ignore JSON parse errors and format versions. Invalid JSON becomes an empty object. [reader.cpp:1731](/home/janv/Work/nell/src/reader.cpp:1731) then reconstructs annotations from recognized fields and saves them with the current version.

Opening and closing a book can therefore replace unreadable annotations with empty annotations. Running an older binary against a future schema can discard unknown top-level fields and relabel the result version 1. Optional-field additions are not safely backward-compatible if old writers erase them.

Save failure is also poorly contained: `Reader::save()` ignores `saveAnnotations()`’s result, while [reader.cpp:433](/home/janv/Work/nell/src/reader.cpp:433) clears the session after attempting to save.

**Recommendation:** distinguish missing, corrupt, unsupported, and valid storage. Refuse writes after failed validation or a newer schema; preserve unknown fields; implement explicit version migrations with backups. Keep dirty state after failed saves and surface a persistent error. QSettings needs a documented key-migration policy and checked synchronization errors too.

**3. High — Atomic file replacement does not prevent lost updates**

[ library.cpp:299](/home/janv/Work/nell/src/library.cpp:299) rewrites the entire in-memory library. [reader.cpp:1742](/home/janv/Work/nell/src/reader.cpp:1742) rewrites annotations and then separately updates library progress.

Two GUI instances, or a GUI plus `nell add`, can load the same snapshot and overwrite each other’s changes. For example, a GUI page save can erase a book added by the CLI after that GUI started. `QSaveFile` protects individual replacements, not read-modify-write transactions.

**Recommendation:** establish one writer through IPC, or use locking plus reload-and-merge for every mutation. SQLite is reasonable if multi-process access remains a product requirement. Make annotation position authoritative and derive library progress, or update them transactionally. Do not add synchronization on top of whole-file last-writer-wins semantics.

**4. High — Book identity and annotation anchors are less stable than advertised**

[ library.cpp:76](/home/janv/Work/nell/src/library.cpp:76) identifies file bytes with a truncated SHA-256. That supports moving an unchanged file, but recompressing an EPUB or correcting metadata creates a different book. More immediately, [reader.cpp:185](/home/janv/Work/nell/src/reader.cpp:185) checks existence without verifying content: replacing a PDF in place attaches old annotations to new content. EPUB extraction can instead reuse stale cached content because [epub.cpp:30](/home/janv/Work/nell/src/epub.cpp:30) trusts the presence of `container.xml`.

EPUB offsets refer to Qt’s interpreted document; PDF offsets refer to Poppler’s extracted word ordering ([reader.cpp:257](/home/janv/Work/nell/src/reader.cpp:257), [reader.cpp:1137](/home/janv/Work/nell/src/reader.cpp:1137)). They survive resizing, but parser changes can invalidate them without changing the book’s bytes.

**Recommendation:** retain the full digest and detect changed source files on open. Separate a file revision from a logical book. Store locator versions and recovery evidence: EPUB resource identity plus structural/text anchors; PDF page, quote, and geometry. Verify the stored quote before presenting an old highlight at a reconstructed location.

**5. High — Fit-text cannot uphold “no line ever cut in half”**

[reader.cpp:712](/home/janv/Work/nell/src/reader.cpp:712) adjusts cuts using individual word boxes, but only when a word’s upper edge lies beyond the first quarter of the screen. Tall or overlapping text that fails that condition is cut anyway. Scanned text has no boxes. Staggered columns can have no common safe horizontal cut.

Header detection is riskier than cosmetic cropping: [reader.cpp:390](/home/janv/Work/nell/src/reader.cpp:390) learns recurring individual words at absolute vertical positions, then [reader.cpp:679](/home/janv/Work/nell/src/reader.cpp:679) excludes every word outside the resulting global bounds. A later chapter heading or legitimate edge content can disappear even though it is not a running header.

**Recommendation:** recognize line/block regions and page-specific header candidates, then validate cut intervals. When no safe cut exists, provide overlap or ordinary zoom/pan and explain the fallback. Make automatic header removal reversible independently of fit-text. The present unconditional guarantee should be narrowed immediately.

**6. Medium — Small-print detection measures a different layout from the one displayed**

[reader.cpp:656](/home/janv/Work/nell/src/reader.cpp:656) uses the first page and the entire available width. Normal spread rendering uses each column’s width at [reader.cpp:1062](/home/janv/Work/nell/src/reader.cpp:1062). A width-limited spread can therefore display smaller text than detection predicts.

The first ten pages also stand in for the entire document. Covers, introductory pages, mixed page sizes, and later dense appendices defeat that assumption. [reader.cpp:665](/home/janv/Work/nell/src/reader.cpp:665) caps zoom to content width, so increasing font size eventually does nothing on wide pages.

**Recommendation:** measure effective display scale for representative page classes, including spread geometry. Expose Auto/On/Off explicitly rather than erasing an explicit choice whenever it happens to match detection ([reader.cpp:646](/home/janv/Work/nell/src/reader.cpp:646)). Add manual zoom/pan for scans and width-limited documents.

**7. Medium — Positions are not consistently modeled across reading operations**

Several concrete bugs expose the overloaded `(chapter, offset)` contract:

- [reader.cpp:1693](/home/janv/Work/nell/src/reader.cpp:1693) ignores a PDF search result’s occurrence offset and navigates to `hits.first()`. Selecting a later result on the same page returns to the first occurrence.
- [reader.cpp:1420](/home/janv/Work/nell/src/reader.cpp:1420) saves PDF bookmarks with offset zero. Every slice shares one bookmark state, and returning loses the bookmarked slice.
- [pageview.cpp:89](/home/janv/Work/nell/src/pageview.cpp:89) permits EPUB dragging into the other column but passes its offset with the starting column’s chapter. A spread crossing a spine boundary mixes two coordinate systems.
- [reader.cpp:917](/home/janv/Work/nell/src/reader.cpp:917) excludes the current PDF page from remaining time even when several unread slices remain.

**Recommendation:** introduce typed positions, ranges, search occurrences, and visible spans. Either support cross-resource selection properly or stop it at the boundary. Make bookmarks, progress, search, and timing consume those shared types instead of independently interpreting integers.

**8. Medium — The architectural split is nominal; expensive work and domain policy remain entangled**

`Reader` owns document parsing, pagination, rendering, caches, selection, navigation, annotations, undo, search, timing, and persistence ([reader.h:247](/home/janv/Work/nell/src/reader.h:247)). QML also owns domain behavior: [DefinitionCard.qml:32](/home/janv/Work/nell/src/DefinitionCard.qml:32) decides when a lookup becomes saved vocabulary, using the current selection when the response arrives.

Opening loads all EPUB chapters; layout changes revisit all documents. Search scans synchronously ([reader.cpp:1642](/home/janv/Work/nell/src/reader.cpp:1642)); extraction blocks for up to 60 seconds ([epub.cpp:44](/home/janv/Work/nell/src/epub.cpp:44)); PDF rendering occurs inside painting ([reader.cpp:1075](/home/janv/Work/nell/src/reader.cpp:1075)). There is no independent cancellable worker pipeline. Timers defer work without making it asynchronous.

**Recommendation:** extract document backends, annotation storage, and a reading-session controller. Give lookup requests immutable book/selection context. Move extraction, indexing, and PDF rendering to workers with clear document ownership and generation IDs; paint completed images. Replace wholesale QVariant-list refreshes with models where incremental updates matter. Keep QML responsible for presentation and transient interaction state.

**9. Medium — Exports preserve prose, not a recoverable reading record**

[reader.cpp:1564](/home/janv/Work/nell/src/reader.cpp:1564) omits bookmarks, highlight identity/color/anchors, and vocabulary definitions from Markdown. EPUB page numbers depend on layout; CLI export invents a 640×900 layout ([cli.cpp:159](/home/janv/Work/nell/src/cli.cpp:159)).

JSON export retains more information but lacks a format version and restore/import path. Both forms require opening the original book, so a missing source file prevents exporting notes that remain safely stored.

**Recommendation:** keep Markdown as a readable report, with stable location references. Add a versioned, lossless archive independent of the renderer, including book identity, annotations, definitions, and settings where relevant. Allow export when the source book is unavailable.

**10. Medium — Tests avoid the boundaries carrying the greatest risk**

The suite covers ordinary parsing, deduplication, persistence, pagination, annotation undo, search, theme/language behavior, and synthetic PDFs. But [tests/nell_tests.pro:11](/home/janv/Work/nell/tests/nell_tests.pro:11) excludes startup, CLI, PageView, and QML.

The migration test covers successful moves and an existing destination ([tests/nell_tests.cpp:449](/home/janv/Work/nell/tests/nell_tests.cpp:449)). Fit-text rendering checks ink quantity, not line integrity ([tests/nell_tests.cpp:376](/home/janv/Work/nell/tests/nell_tests.cpp:376)). Neither would catch the central failures above.

**Recommendation:** prioritize subprocess tests proving help/version do not mutate storage; migration interruption cases; corrupt/newer schemas; competing writers; same-page search occurrences; cross-chapter dragging; and line-coverage invariants across mixed PDF layouts. Preserve a QML integration harness in the repository. Build/run it under isolated XDG roots.

**11. Medium — Product fidelity requires explicit limits and a replaceable backend**

“Original” still strips publisher colors ([reader.cpp:259](/home/janv/Work/nell/src/reader.cpp:259)); dark PDFs invert every pixel, including photographs and diagrams ([reader.cpp:1087](/home/janv/Work/nell/src/reader.cpp:1087)). “Everything undoes” is already false for vocabulary insertion, which lacks a checkpoint ([reader.cpp:1508](/home/janv/Work/nell/src/reader.cpp:1508)).

For Apple Books/Kindle-style ambitions, distinguish extensions from replacements. Collections and password prompts are incremental. Cross-device annotation reconciliation needs a different persistence contract. Broad EPUB fidelity needs substantially more than `QTextDocument`, regex CSS removal, and SVG-to-image substitution ([reader.cpp:31](/home/janv/Work/nell/src/reader.cpp:31), [reader.cpp:132](/home/janv/Work/nell/src/reader.cpp:132)). Accessible reading needs semantic text exposure beyond painted pages.

**Recommendation:** define the supported EPUB subset and prioritize reliable reading over additional polish. The elaborate automatic crop behavior is overcommitted relative to its evidence. Isolate the rendering backend now so future fidelity work does not require rewriting annotations and navigation too.

**12. Low — Release packaging does not identify its inputs**

[pkgbuild/PKGBUILD:11](/home/janv/Work/nell/pkgbuild/PKGBUILD:11) has no source archive or checksums and builds the adjacent checkout. Version agreement cannot prove that package contents correspond to the release.

**Recommendation:** retain this as a local development recipe; publish packages from a pinned release source in a clean build directory, with smoke tests covering CLI and QML startup.

**Do first**

1. Make help/version side-effect-free and migration resumable with retained originals.
2. Reject corrupt/newer storage and prevent concurrent lost updates.
3. Fix PDF occurrence navigation, slice bookmarks, and cross-chapter selection.
4. Replace the fit-text guarantee with tested invariants and explicit fallbacks.
5. Separate document backends and persistence before adding more features.