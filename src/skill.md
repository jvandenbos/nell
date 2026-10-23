---
name: quire
description: |
  Read and manage the person's e-book library with Quire, the Omarchy EPUB and
  PDF reader. Use when asked about their books, what they're reading, their
  highlights, notes, bookmarks, or looked-up words, or to add books to Quire.
---

# Quire

Quire is a desktop e-book reader for EPUB and PDF. Its library lives in
`~/.local/share/quire/`; books stay where they are on disk and are never
modified. The `quire` command reads and adds to the library without a display.

## Commands

Every command takes `--json`, exits 0 on success and 1 on failure, and writes
errors to stderr.

- `quire list` — the library, most recently read first: id, progress, title,
  author. `--json` adds path, format, cover, added and opened times, and
  whether the file is missing.
- `quire add <file>...` — add EPUB or PDF files. Adding a book that's already
  there (even moved) finds it again by content, so it is safe to repeat.
- `quire info <book>` — title, author, format, and table of contents of a file
  or a library id. Works on files not in the library.
- `quire notes <book>` — highlights with their notes, grouped by chapter, plus
  bookmarks and looked-up words. Prints Markdown by default; `--json` gives
  each highlight's `text`, `note`, `color`, `page`, and `section`.
- `quire open [book]` — open the reader for the person. It needs their
  desktop, so do not wait on it.

## Working with the person

- Their highlights and notes are their own reading; quote them faithfully and
  don't rewrite them.
- To answer "what was I reading", use `quire list --json` and look at
  `opened` and `progress`.
- Quire never edits book files, and neither should you on its behalf.
