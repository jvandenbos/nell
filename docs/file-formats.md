# File formats

Nell keeps everything as JSON or INI under the XDG directories. Each JSON file
has a top-level integer `version`, the *format* version, which is separate from
the app's semver. It goes up only when an existing reader would misread the new
shape. Adding an optional field doesn't change it. The current format version
is **1** (`Library::FormatVersion`).

Nell reads each file as missing, valid, damaged (it doesn't parse), or newer
(its `version` is above what this Nell knows). Only missing and valid files
are ever written. A damaged or newer file is read as far as it can be, left
byte for byte as it is, and named in `Library::storageProblems` so the window
can say so; the app works on without saving to that file. Fields Nell doesn't
know, at the top level or inside an entry, are kept when it writes a file
back, so an older Nell doesn't strip what a newer one added.

More than one Nell can write at once (a window and `nell add`, or two
windows). Every write takes a `QLockFile` beside the file (`library.json.lock`,
`books/<id>.json.lock`, removed when done), reads the file again, and applies
its change to what's there now: books are kept from both sides by `id`, and the
newest `opened` wins a book's progress. A book's notes are merged three ways
against what that Nell last read: bookmarks by `chapter` and `offset`,
highlights by `id`, words by `word`; a mark only one side changed takes that
change, and where both changed the same one, the Nell saving now wins. The
write itself is still a `QSaveFile` replace.

Timestamps are ISO 8601 strings in UTC (`2026-10-23T13:55:02Z`).

## `~/.local/share/nell/library.json`

```json
{
  "version": 1,
  "books": [
    {
      "id": "3f9c0a1b2d4e5f60",
      "sha256": "3f9c0a1b2d4e5f60…(64 hex digits)",
      "size": 186412,
      "modified": "2026-10-20T08:12:44.120Z",
      "path": "/home/me/Books/alice.epub",
      "format": "epub",
      "title": "Alice's Adventures in Wonderland",
      "author": "Lewis Carroll",
      "language": "en",
      "cover": "/home/me/.cache/nell/covers/3f9c0a1b2d4e5f60.jpg",
      "progress": 0.42,
      "added": "2026-10-23T13:55:02Z",
      "opened": "2026-10-23T14:20:11Z",
      "finished": false
    }
  ]
}
```

- `id`: the first 16 hex digits of the SHA-256 of the book file. It names the
  annotations file and the cache directory.
- `sha256`, `size`, `modified`: the whole digest, byte size, and modification
  time of the file when it was added or last checked. Opening a book compares
  size and time and hashes the file again only if they changed. If the digest
  changed, the file was edited in place: Nell adds what's there now as a new
  entry with fresh notes, and leaves the old entry and its notes untouched,
  with `replacedBy` on the old entry and `revisionOf` on the new one naming
  each other. Books added before these fields existed get them the first time
  they're opened.
- `format`: `"epub"` or `"pdf"`.
- `progress`: 0 to 1. `finished` is set once a book reaches the end and stays set.
- `opened` is empty until the book is first read.

## `~/.local/share/nell/books/<id>.json`

```json
{
  "version": 1,
  "position": { "chapter": 3, "offset": 1820 },
  "bookmarks": [
    { "chapter": 3, "offset": 1820, "title": "A Mad Tea-Party",
      "snippet": "Have some wine,", "created": "…" }
  ],
  "highlights": [
    { "id": "…", "chapter": 3, "start": 1900, "end": 1964,
      "text": "Then you should say what you mean,", "color": "yellow",
      "note": "", "created": "…" }
  ],
  "fitText": false,
  "vocabulary": [
    { "word": "treacle", "definition": "…", "context": "…the sentence…",
      "chapter": 6, "offset": 812, "created": "…" }
  ]
}
```

`fitText`, when present, is the reader's choice in Appearance for this PDF
(on or off) and overrides small-print detection, even when detection would
choose the same. It's left out while the choice is Auto.

A PDF bookmark made in fit text has the first word of its slice as `offset`,
so it marks that screenful; otherwise a PDF bookmark's `offset` is 0, the page.
`chapter` and `offset` mean what they mean in the reader (see
[architecture.md](architecture.md#places-in-a-book)). `color` is `yellow`, `green`, `blue`, `pink`,
`purple`, or `underline`. `vocabulary` is newest first.

Removing a book from the library leaves this file in place, so adding the book
again brings its marks back.

## Notes export (`nell notes --json`, Export Notes to a `.json` file)

```json
{
  "version": 1,
  "id": "3f9c0a1b2d4e5f60", "sha256": "…", "format": "epub",
  "title": "Alice's Adventures in Wonderland", "author": "Lewis Carroll",
  "position": { "chapter": 3, "offset": 1820 },
  "bookmarks": [ { "chapter": 3, "offset": 1820, "…": "…", "location": 412, "section": "A Mad Tea-Party" } ],
  "highlights": [ { "id": "…", "chapter": 3, "start": 1900, "…": "…", "location": 413, "section": "…" } ],
  "vocabulary": [ { "word": "treacle", "definition": "…", "…": "…", "location": 520 } ]
}
```

Its own `version` (**1**, `Library::NotesFormatVersion`) is separate from the
stores'. Each mark is as stored in `books/<id>.json`, plus places worked out
for the reader: `location` (an EPUB's Kindle-style location, 150 characters
each, or a PDF's page), `page` (PDFs only), and `section` (the contents entry
it falls under). None of them depend on the window. When the book file is
missing they're left out for an EPUB, and the rest still exports.
`nell import <file>` (or `Library::importNotes`) reads an export back into
`books/<id>.json`, dropping the worked-out places and merging with any notes
already there; an export with a higher `version` is refused. An export without
`version` (from 0.3.0 and before) reads as 1.

The Markdown export places each highlight, bookmark, and word by page (PDF)
or location (EPUB), never by an EPUB page number, and includes each
highlight's color and note, the bookmarks, and the Words list with
definitions and context.

## `~/.config/nell/nell.conf`

Qt `QSettings` INI. `version` (under `[General]`) is the settings format
version, **1**, written with the first setting saved; a file without it is
version 1. A file `QSettings` can't parse, or one with a higher `version`, is
read but never written. The keys are `reading/theme`, `reading/font`,
`reading/fontSize` (12 to 44), `reading/lineSpacing` (1.0 to 2.0),
`reading/margins` (0 to 2), `reading/justify` (off unless set),
`reading/spread`, `reading/hideRunningHeads` (fit text leaves out running
heads; default true), `reading/infoMode` (0 to 4: chapter time, book time,
pages left in chapter, location or page, percent), `reading/speed` (the
learned reading speed), `reading/highlightColor` (what H highlights in: the
last color picked), `ui/reduceMotion`, and `library/sort`.

## `~/.cache/nell/`

Unpacked EPUBs (`<id>/`, each with `.nell-sha256` naming the file it came
from; one that doesn't match is unpacked again) and covers. Nothing here is the only copy of anything.
Delete it whenever you like.
