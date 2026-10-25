# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/jvandenbos/quire/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/jvandenbos/quire/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/jvandenbos/quire/releases/tag/v0.1.0
