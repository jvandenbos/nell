# Architecture

Nell is one C++17 / Qt 6 Quick binary. C++ owns the state (the library, the
open book, preferences); QML draws it. `src/main.cpp` builds one of each object
and hands them to QML as context properties, so there is no model layer between
them.

```
main.cpp ──► Translator   ~/.config/omarchy/language → i18n/<lang>.json
         ├─► Theme        ~/.local/state/omarchy/current → accent, rounding, colors
         ├─► Preferences  ~/.config/nell/nell.conf
         ├─► Library      ~/.local/share/nell/{library.json, books/<id>.json}
         ├─► Reader       the open book: pagination, place, marks
         ├─► Dictionary   Wiktionary look-ups
         ├─► PortalFilePicker   xdg-desktop-portal file chooser over D-Bus
         └─► Main.qml     LibraryView ⇄ ReaderView (PageView paints the pages)
```

If the first argument is a CLI command (`list`, `add`, `info`, `notes`,
`skill`), `main.cpp` sets `QT_QPA_PLATFORM=offscreen`, never loads QML, and
hands over to `cli.cpp`. That is why the commands work over ssh and from agents.
`--help` and `--version` are handled the same way, before any storage is
touched.

Quire's storage (Nell's name before 0.3.0) is moved across by
`Library::adoptQuire` only when Nell is about to use its storage for real: the
window opening, or `nell add`. Commands that only read leave it alone and say
so on stderr. Each part (data, settings, cache, the cover paths in
`library.json`) is checked and redone on its own, so a run that stopped half
way is finished by the next one; problems go to stderr.

## The pieces

| File | What it does |
| --- | --- |
| `library.{h,cpp}` | The list of books and each book's saved marks. A book is identified by the first 16 hex digits of the SHA-256 of its file, so a moved or re-added file keeps its notes. Books are never written to. |
| `epub.{h,cpp}` | Unpacks an EPUB with `bsdtar` into `~/.cache/nell/<id>/` and reads the OPF: metadata, spine (reading order), and the table of contents from `nav.xhtml` or the NCX. |
| `reader.{h,cpp}` | The open book. EPUB chapters load into a `QTextDocument` subclass (`BookDocument`) and are laid out at the window's page size; PDFs are rendered by Poppler. Owns bookmarks, highlights, words, undo/redo, search, and the reading-speed estimate. |
| `pageview.{h,cpp}` | The `QQuickPaintedItem` that paints the current page or spread and turns clicks and drags into page turns, selections, links, and highlight picks. It keeps no state. |
| `preferences.{h,cpp}` | Reading settings and the learned reading speed. A change that moves text (font, size, spacing, margins) emits `layoutChanged` so the Reader paginates again; a color change only repaints. |
| `theme.{h,cpp}` | Watches the Omarchy theme directory and exposes its colors, accent, and window rounding live. |
| `translator.{h,cpp}` | A `QTranslator` that reads `src/i18n/<lang>.json`, keyed by the English source string, with CLDR plural forms. Follows the Omarchy language live. |
| `dictionary.{h,cpp}` | Fetches a definition from Wiktionary. Only the selected word goes out, and only on request. |
| `portalfilepicker.{h,cpp}`, `filepicker.h` | The native file chooser through the desktop portal. |
| `cli.{h,cpp}` | The headless commands, each with a `--json` form. |
| `skill.md` | The agent skill `nell skill install` writes out. |
| `*.qml` | The interface: `Main.qml` switches between `LibraryView` and `ReaderView`; the panels (contents, search, appearance, notes) are separate files. |

## Places in a book

Every position is a `(chapter, offset)` pair. In an EPUB, `chapter` is the index
of a spine document and `offset` is a character position in it. That survives
any change of font or window size, which page numbers don't. In a PDF, `chapter`
is the page and `offset` is a word on it. Page numbers are worked out from
these pairs on every pagination and are never stored.

## Fit text

A PDF counts as small print when its body text, which is the height most of
its characters are set at, would be drawn under `Reader::SmallPrintPixels`
(12.5 px) with the page fitted to the window. That is smaller than the
smallest size an EPUB can be set to. For such a book the Reader:

- zooms each page until the body text is the reader's font size, capped so the
  page's content still fits the window's width, and uses one column;
- crops the page to its content: the ink found by a 24 dpi render plus the word
  boxes, minus running heads and feet (words near the top or bottom edge that
  recur in the same place on most of the first ten pages, ignoring digits);
- cuts the page into slices one view high, moving each cut up above any line
  it would go through, so no line is shown in halves and none is skipped.

`m_page` stays the PDF page and `m_slice` picks the slice, so the rest of the
reader still counts pages as it always has. The saved place is the first word
of the slice, which finds the right slice again after the window or text size
changes. A choice made with the Appearance switch is saved with the book as
`fitText` and only when it differs from what detection would pick.

## Borrowed code

The theme watcher, the translator, and the portal file chooser follow the
versions in Omacut, Monologue, and Hype (the Omarchy apps by David Heinemeier
Hansson), so Nell behaves like its neighbours on the desktop.

## Tests

`tests/nell_tests.cpp` is one Qt Test suite covering the Omarchy theme
and language watchers, the shipped translation catalogs, EPUB parsing, the
library, the reader (pagination, marks and undo, search, PDFs), the reading-speed
estimate, and Wiktionary parsing. It builds the
same sources as the app minus QML and runs offscreen through `bin/test`, which
first runs `bin/check-version`.
