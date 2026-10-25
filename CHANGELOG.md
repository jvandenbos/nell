# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Leaving out running heads and feet in fit text is its own setting
  (`Preferences.hideRunningHeads`, on by default), so it can be undone without
  turning fit text off.
- Fit text per book is Auto, On, or Off (`Reader.fitTextMode`); choosing On or
  Off is kept even when detection would choose the same.
- `nell import` and `Library.importNotes` read back a JSON notes export.
- `Reader.exportNotesFor(id, url)` exports any book's notes from what's
  stored, so notes can be exported when the book file is gone; `nell notes`
  does the same.
- `Ctrl+G` goes to a page or a percentage. `P` cycles the progress corner,
  which is now a button with a menu of its five modes, percent read among them.
- Library filters: All, Reading, Finished.
- "Locate File…" for a book whose file has moved, from its menu or the message
  shown when it won't open; the file must be the same book.
- Undo in the message after removing a highlight, note, bookmark, or word.
- Edit Note and Show Definition buttons on the Notes and Words lists; Retry
  when a dictionary lookup fails.
- Reduce motion, under Appearance: no sliding page turns or fading controls.
- Previous and next chevrons in the page margins on hover.
- Screen reader names, roles, and checked states on every control, and one
  keyboard focus ring (2 px in the accent, 2 px clear) on Nell's own controls.

### Changed

- Exported notes are a reading record: the Markdown has each highlight's
  color, the bookmarks, and the Words list with definitions, placed by PDF
  page or EPUB location instead of page numbers that depend on the window.
  The JSON export (`nell notes --json`, or Export Notes to a `.json` file) has
  a `version` field and `location` in place of an EPUB `page`.
- Text is ragged-right by default; justification stays an option.
- Letter shortcuts act only while the page or shelf has the keyboard: they no
  longer turn pages behind an open panel or fire while typing. `Ctrl+Q` quits
  from anywhere. Help is a proper dialog: Esc closes it and focus goes back.
- One panel at a time: Contents, Search, Appearance, and Go To close each
  other, Esc closes the newest, each has a close button, and focus returns to
  what opened it. In Search, Enter on a match goes there and closes the panel.
- The controls come up when the pointer nears the top or bottom edge or Tab
  moves into them, stay while one has focus, and hide 2.6 s after a panel
  closes or the pointer leaves them.
- Panels and popups take their colors from the reading theme, the library
  from the desktop theme; secondary text is opaque and at least 4.5:1.
- Covers show the whole artwork instead of being cropped to one shape.
- The window goes down to 480 px wide: the library's find field takes its own
  row, Continue Reading shows as many cards as fit, Appearance scrolls within
  the window, and the reader's footer labels take only their own width.
- Messages about failures stay until dismissed; adding several books reports
  "Added N books; M failed" with the reasons.
- `H` highlights in the last color picked; every swatch is named.
- The page scrubber takes the keyboard (Tab to it; arrows, Home, End).

### Fixed

- `nell --version`, `--help`, a mistyped option, and the commands that only
  read (`list`, `info`, `notes`, `skill`) no longer move Quire's library. It
  moves when the window opens or `nell add` writes, each part (data,
  settings, cache) is finished by the next run if one stopped half way, an
  empty `nell` directory no longer blocks it, and anything that can't be moved
  is reported on stderr.
- A damaged `library.json`, book notes file, or `nell.conf`, or one written by a
  newer Nell, is no longer overwritten: Nell reads what it can, leaves the file
  as it is, and stops saving to it (`Library.storageProblems` lists them).
  Fields Nell doesn't know are kept when it saves. Notes that fail to save are
  kept and saved on the next try instead of being dropped when the book closes.
- Two Nells writing at once (the window and `nell add`, or two windows) no
  longer lose each other's changes: each save locks the file, rereads it, and
  merges books by id and notes mark by mark.
- A book file replaced in place with different contents no longer gets the old
  notes laid over new text. Nell keeps each book's whole SHA-256, size, and
  time, notices the change on open, and opens the new contents as a new entry
  (`Reader.fileChanged`, `Reader.previousRevision`), leaving the old notes
  with the old one. An unpacked EPUB is reused only if it came from the same file.
- Fit text checked a cut only against words below the first quarter of the
  screen, so tall or staggered lines could be cut through. Every word on the
  page is checked now, and where no clean cut exists the cut line is shown
  again at the top of the next screen. Running-head removal only drops the
  words that recur, so a heading as high as the running head on its own page
  is no longer hidden. The README no longer promises no line is ever cut.
- Small print is measured at the width a page is actually drawn, so a PDF that
  is readable on a full window but tiny in a two-page spread gets fit text.
- PDF search went to the first match on a page whichever result was picked; it
  goes to the one picked now.
- PDF bookmarks in fit text mark the screenful, not the whole page, so each
  slice has its own and returning finds the slice.
- Dragging a selection across a two-page spread into the next chapter stops at
  the chapter's edge instead of selecting from the wrong place.
- Time left in a PDF in fit text counts the rest of the page in view.
- Saving a looked-up word can be undone, like every other mark.
- The Original theme keeps an EPUB's own text and background colors instead
  of replacing them; the other themes still use their own.
- Cancelling a new note no longer leaves the highlight it was made on.
- The selected cover's ring is no longer cut off at the shelf's left edge.

## [0.3.0] - 2026-10-24

### Changed

- Renamed from Quire to Nell, after the girl who reads the Primer in *The
  Diamond Age*. "Quire" already names Getty's digital book publishing
  framework, a task manager, and an Arch (AUR) package. The binary, package, desktop entry, data
  directories, and agent skill are all `nell` now.

### Added

- On first run Nell moves Quire's library, notes, settings, and cache from
  `~/.local/share/quire`, `~/.config/quire`, and `~/.cache/quire`, unless
  Nell already has its own. Run `nell skill install` again if you had the
  Quire skill installed.

## [0.2.0] - 2026-10-24

### Added

- Fit text for small-print PDFs. A PDF whose text would be too small to read
  is zoomed to the reader's text size and read a screen at a time, with
  running heads and feet left out and no line split between screens. It turns
  on by itself and can be turned off per book in Appearance.
- `End` goes to the foot of the last page in fit text.
- Architecture, file-format, and release docs under `docs/`.
- `bin/check-version`, run by `bin/test`: fails when `quire.pro`, the PKGBUILD,
  and this changelog disagree on the version.

### Changed

- The version is set once in `quire.pro` and compiled in, not written into `main.cpp`.

## [0.1.0] - 2026-10-23

### Added

- A library of EPUB and PDF books with covers, Continue Reading, sorting, and
  find; add by file chooser, drag and drop, or opening a file.
- A paginated reader with one or two pages, six reading themes (one following
  the Omarchy theme), typefaces, size, line spacing, margins, and justification.
- Highlights in five colors and underline, notes, bookmarks, undo and redo, and
  Markdown export of notes.
- Wiktionary look-ups that keep each word and its sentence in a Words list.
- Full-text search, a table of contents, and internal links.
- Time left in the chapter and book, learned from the reader's own pace, plus
  pages left and Kindle-style locations.
- `quire list`, `add`, `info`, `notes`, and `skill` commands for scripts and agents.
- Danish, Dutch, French, German, and Spanish translations.

[Unreleased]: https://github.com/jvandenbos/nell/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/jvandenbos/nell/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/jvandenbos/nell/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/jvandenbos/nell/releases/tag/v0.1.0
