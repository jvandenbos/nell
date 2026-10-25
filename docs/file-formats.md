# File formats

Quire keeps everything as JSON or INI under the XDG directories. Each JSON file
has a top-level integer `version`, the *format* version, which is separate from
the app's semver. It goes up only when an existing reader would misread the new
shape. Adding an optional field doesn't change it. The current format version
is **1** (`Library::FormatVersion`).

Timestamps are ISO 8601 strings in UTC (`2026-10-23T13:55:02Z`).

## `~/.local/share/quire/library.json`

```json
{
  "version": 1,
  "books": [
    {
      "id": "3f9c0a1b2d4e5f60",
      "path": "/home/me/Books/alice.epub",
      "format": "epub",
      "title": "Alice's Adventures in Wonderland",
      "author": "Lewis Carroll",
      "language": "en",
      "cover": "/home/me/.cache/quire/covers/3f9c0a1b2d4e5f60.jpg",
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
- `format`: `"epub"` or `"pdf"`.
- `progress`: 0 to 1. `finished` is set once a book reaches the end and stays set.
- `opened` is empty until the book is first read.

## `~/.local/share/quire/books/<id>.json`

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
and overrides small-print detection. It's left out while detection decides.

`chapter` and `offset` mean what they mean in the reader (see
[architecture.md](architecture.md#places-in-a-book)). `color` is `yellow`, `green`, `blue`, `pink`,
`purple`, or `underline`. `vocabulary` is newest first.

Removing a book from the library leaves this file in place, so adding the book
again brings its marks back.

## `~/.config/quire/quire.conf`

Qt `QSettings` INI. The keys are `reading/theme`, `reading/font`,
`reading/fontSize` (12 to 44), `reading/lineSpacing` (1.0 to 2.0),
`reading/margins` (0 to 2), `reading/justify`, `reading/spread`,
`reading/infoMode`, `reading/speed` (the learned reading speed), and
`library/sort`.

## `~/.cache/quire/`

Unpacked EPUBs (`<id>/`) and covers. Nothing here is the only copy of anything.
Delete it whenever you like.
