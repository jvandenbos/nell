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
