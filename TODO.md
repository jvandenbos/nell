# TODO

Where Nell is going is [docs/roadmap.md](docs/roadmap.md). This is what's open now.

- [ ] Translations are behind: 114 interface strings added since round one
  (catalogs, the reading record, highlight colors, go-to, and more) have no
  entry in `src/i18n/{da,de,es-MX,fr,nl}.json` and show in English. Found
  2026-11-18; the strings added that day are translated.
- [ ] At the release after 0.4.1, add `zlib` to `depends` in the published
  recipe (`pkgbuilds/nell/PKGBUILD` in omarchy-pkgs); the bot doesn't.
- [ ] omarchy-pkgs PR #1674 still waits for a maintainer's `build-approved`.
- [ ] aarch64 was last built at 0.3.0.
- [ ] Auto page turn and the paragraph style have only been run under Xvfb
  with the software renderer, never on a real Omarchy session.
- [ ] Irregular English forms ("seen", "went") aren't traced to their
  headword by the offline dictionary; they fall through to Wiktionary.
