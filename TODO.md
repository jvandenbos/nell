# TODO

Where Nell is going is [docs/roadmap.md](docs/roadmap.md). This is what's open now.

## Next session (parked 2026-11-19)

- [ ] **Fonts are sometimes hard to read** (Jan). Not diagnosed. Three
  answers settle where to look: is it EPUBs, PDFs or both; is it the letters
  (fuzzy, thin, grey) or the typeface (cramped, old-fashioned); and which
  typeface does Appearance show as selected? Best done looking at gargoyle,
  which didn't answer on the LAN that night.
  Guesses so far, none checked on his machine: Nell bundles no typeface, so
  the default is whichever of the list in `src/preferences.cpp` is installed;
  hinting is off (`src/reader.cpp`), which can look soft at 1× scale; PDF
  pages are inverted images in dark themes, which may thin the letters.
  Likely fixes: bundle Literata as the default, then a weight control.
- [ ] **A native Apple Nell** (iPad, iPhone, Mac), waiting on Jan's go before
  a spec is written or a repo made. Agreed so far: native per platform, not
  a Qt port; built for Jan first; the goal is amazing reading, not a Preview
  competitor; it never changes the file. Sketch: SwiftUI designed on the
  iPad, WebKit for EPUB, PDFKit for PDF, the system dictionary and speech,
  its own private repo. What the two apps share is the JSON formats in
  [docs/file-formats.md](docs/file-formats.md) and test fixtures, not code.
  Known risk: a (chapter, character offset) position may not land on the
  same word in Qt's text engine and WebKit, and needs a tested mapping.
- [ ] **Where Nell falls short of that spec today.** Missing: your place,
  everywhere (no sync). Half: notes that leave (export is by hand; nothing
  reaches Readwise or Notion). Never measured: start-up and page-turn speed.
- [ ] **Readwise push**, waiting on Jan's say-so (it sends highlights to a
  third party) and a token in `secrets.env`. Check Readwise's API docs
  first: that it takes highlights and exports to Notion itself is from
  memory, not verified.

## Open

- [ ] Translations are behind: 114 interface strings added since round one
  (catalogs, the reading record, highlight colors, go-to, and more) have no
  entry in `src/i18n/{da,de,es-MX,fr,nl}.json` and show in English. Found
  2026-11-18; the strings added that day are translated.
- [ ] omarchy-pkgs PR #1674 still waits for a maintainer's `build-approved`
  (at 0.5.0 with `zlib` since 2026-11-18; builds and passes in their builder).
- [ ] aarch64 was last built at 0.3.0.
- [ ] Auto page turn and the paragraph style have only been run under Xvfb
  with the software renderer, never on a real Omarchy session.
- [ ] Irregular English forms ("seen", "went") aren't traced to their
  headword by the offline dictionary; they fall through to Wiktionary.
