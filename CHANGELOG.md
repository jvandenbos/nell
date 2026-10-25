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
