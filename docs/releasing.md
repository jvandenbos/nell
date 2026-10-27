# Releasing

Nell uses [Semantic Versioning](https://semver.org/). While it's 0.x, a minor
bump may break things, and a change to a [file format](file-formats.md)
also bumps that file's own `version` field.

The version is written in three places, and `bin/check-version` (run by
`bin/test`) fails if they don't match:

- `VERSION` in `nell.pro`, compiled in as `NELL_VERSION` and printed by `nell --version`
- `pkgver` in `pkgbuild/PKGBUILD`
- the newest `## [x.y.z]` heading in `CHANGELOG.md`

## Steps

1. Under `## [Unreleased]` in `CHANGELOG.md`, sort the entries into Added,
   Changed, Fixed, and Removed, then rename the heading to
   `## [x.y.z] - YYYY-MM-DD` and add an empty `## [Unreleased]` above it.
   Update the compare links at the bottom.
2. Set `VERSION` in `nell.pro` and `pkgver` in `pkgbuild/PKGBUILD`; reset `pkgrel=1`.
3. `bin/test`: the version check and the suite must both pass.
4. `bin/install`, then `nell --version` to confirm the installed binary.
5. Commit as `Release x.y.z`, run `git tag -a vx.y.z -m "Nell x.y.z"`, and
   `git push --follow-tags`.
6. Publish the GitHub Release from the tag, with the changelog section as
   its notes, so the releases page is not a list of bare tags:

   ```sh
   awk '/^## \[x\.y\.z\]/{f=1;next} /^## \[/{f=0} f' CHANGELOG.md > /tmp/nell-notes.md
   gh release create vx.y.z --verify-tag --latest --title "Nell x.y.z" --notes-file /tmp/nell-notes.md
   ```

   `--verify-tag` refuses if the tag has not been pushed; `--latest` keeps
   the newest version pinned at the top. 0.1.0 to 0.3.0 were created this
   way after the fact on 2026-10-26.

## The PKGBUILD is a local recipe

`pkgbuild/PKGBUILD` builds and packages the checkout it sits in: `source=()`
and `sha256sums=()` are empty and `build()` runs `bin/build` in `$startdir/..`.
That suits `bin/install` on this machine, but the package it makes is only
whatever the working tree held, and the version check can't prove otherwise.
A package published for others (the AUR, a repository) would need a
`source` pinned to the release, such as the `vX.Y.Z` tag's tarball from
GitHub, with its `sha256sums`; a `build()` in `$srcdir` from that source in a
clean directory, not the checkout; and a `check()` running the suite, which
covers `nell --version`, the commands, and the window starting.

## The Omarchy package

The published recipe is `pkgbuilds/nell/` in
[omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs) (submitted as
PR #1674 on 2026-10-26). It builds from the `vX.Y.Z` tag's tarball and runs
`bin/test` as `check()`, and its upstream watch follows this repository's
tags, so a pushed release tag is picked up by their bot without another PR.
It deliberately omits the local recipe's `conflicts`/`replaces` on `quire`:
that name belongs to an unrelated AUR package.
