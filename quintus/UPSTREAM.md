# Vendored Mecrisp-Quintus sources

`common/` is an unmodified copy of `mecrisp-quintus-source/common/` from
Mecrisp-Quintus 1.1.1d by Matthias Koch, released under the GNU GPL v3
or later (see `COPYING`).

- Release: `mecrisp-quintus-1.1.1d.tar.gz` (2025-07-15)
- Source: https://sourceforge.net/projects/mecrisp/files/
- SHA-256 of the tarball: `0421e1de4e78ad792fe16a3980b84d0de38dcb19b9e6745c2e7991cf574593d2`
- `UPSTREAM-README` and `UPSTREAM-CHANGELOG` are the release's README and CHANGELOG.

Do not edit files in `common/`. Board-specific code lives in `pocket/`,
which started as a copy of the release's `ulx3s-pipeline` target
(RV32IM, no compressed instructions).

To upgrade Quintus: replace `common/` with the new release's copy, update
this file, rebuild, and run the tests.
