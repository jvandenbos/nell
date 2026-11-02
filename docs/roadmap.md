# Roadmap

What the field has that Nell doesn't, from a look at Readest, Foliate,
KOReader, Kindle, Apple Books, Readwise Reader and Sioyek on 2026-11-01, and
what Nell does with it. Nell already beats the Linux readers on reading feel:
fit text, two-column pages, time left learned from the reader's own pace, a
dictionary with a Words list, and a JSON command line. What's below is the
rest.

## Done (round 1, 2026-11-02)

All seven shipped on 2026-11-02: footnotes, lightbox, hyphenation, OPDS
catalogs, the reading ruler, page peek (`` ` `` tap to pin and return, hold to
peek), and the reading record with its badges and `nell stats`.

Table stakes, in every one of Readest, Foliate and KOReader (the first two in
Apple Books as well):

- **Footnote popups.** A footnote reference opens its note in place.
- **Image lightbox.** A figure opens full-window.
- **Hyphenation.** Justified text without it looks broken.
- **OPDS catalogs**, the Calibre Content Server among them.

Small delights:

- **Reading ruler.** A band that follows the line being read (Readest and
  Kindle both added one in 2026).
- **Page peek.** Hold a key to look at another page or the contents, let go
  to snap back (Kindle calls its version Page Flip).
- **A reading record.** Nell already learns the reader's pace, so a reading
  log, streaks and per-book time cost little, and are what Apple Books'
  reading goals and Kindle's insights are loved for.

## Next round

- **Read aloud** with a local voice (Piper or Kokoro), the sentence
  highlighted as it's spoken, pages turning on their own. Foliate has only
  espeak through Speech Dispatcher; Readest has it inside a webview. Omarchy
  already ships local speech models (Monologue) and `omarchy-local-ai`
  landed 2026-11-01, so the precedent is set.
- **X-Ray, offline.** Kindle's best feature; nobody open-source has it. The
  first version needs no model: capitalized terms by frequency, each with
  its first appearance and the chapters it's in. A local model can add
  one-line descriptions later.

## Later, maybe

- A watched Markdown folder that mirrors highlights into an Obsidian vault
  as they're made (Readwise's pitch, without the subscription).
- Flashcard review of the Words list (Kindle's Vocabulary Builder).
- Progress sync with KOReader (the kosync protocol is a few HTTP calls).
- MOBI and AZW3 through Calibre's `ebook-convert` as an optional dependency.
- Continuous scroll as an alternative to pages.
- An Omarchy screensaver showing a highlight from the library.

## Not doing

Parallel reading, web clipping, comics, cloud accounts, AI summaries. Readest
has them and they dilute the Apple Books feel Nell is built on.

## Sources

Readest README and its vendor-written Readest-vs-Foliate page, the Foliate
README, the KOReader wiki (feature list last edited 2023), Amazon's Kindle
accessibility page, omacom/omarchy-pkgs PRs #280 (omabook) and #305
(oma-preview).
