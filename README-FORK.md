# Drawpile Mobile (fork)

This is an **unofficial fork** of [Drawpile](https://drawpile.net), the
collaborative drawing program. It is not affiliated with or endorsed by the
Drawpile project. All credit for Drawpile itself goes to its authors, listed in
[AUTHORS](AUTHORS) and the [Drawpile repository](https://github.com/drawpile/Drawpile).

The fork adds a touch-first interface for phones and tablets that replaces
Drawpile's small-screen ("Mobile") interface mode. Everything else is
upstream Drawpile:

* The painting engine, brushes, file formats and network protocol are
  unchanged. Files are interchangeable with official Drawpile 2.3.x and you can
  host or join sessions with official clients.
* The Desktop and Dynamic interface modes are unchanged. In Dynamic mode, the
  new interface is used whenever Drawpile would use its small-screen layout.
* On Android, the fork installs alongside the official app (application id
  `net.drawpile.mobileui`, name "Drawpile Mobile (fork)").

The mobile interface can be switched back to the classic small-screen layout
via **View ▸ Drawpile Mobile interface** (takes effect after a restart).

## What's in the mobile interface

* A project hub on startup: recent files with thumbnails, new canvas presets
  (phone, square, A4 at 300 dpi, 1080p/720p animation with frame rate),
  open/import, recovery of autosaved work, join/browse/host sessions.
* A canvas-first editor: compact top bar, tool bar (bottom in portrait, side in
  landscape) with the most recently used tools and a drawer with all tools,
  brush size and opacity sliders on the screen edge, eyedropper and color
  button.
* All of Drawpile's panels (brush settings, brushes, colors, layers, timeline,
  onion skins, navigator, reference images, chat, session status) open in bottom
  sheets (portrait) or side panels (landscape, tablets) that can be swiped
  away.
* Every main menu command is reachable through a searchable command sheet that
  is generated from the live menus.
* Russian and Ukrainian translations for the new interface, plus translations
  for upstream strings that were still untranslated in those languages.

## Where the code lives

The fork keeps its changes isolated to make merging upstream updates easy:

| Path | What |
|---|---|
| `src/desktop/mobile/` | All of the mobile interface (new files only) |
| `src/desktop/mobile/i18n/` | Translations and the generator script |
| `.github/workflows/mobileui-android.yml` | CI job building the APKs |
| `src/desktop/mainwindow.{h,cpp}` | 4 small hooks, marked `Drawpile Mobile (fork)` |
| `src/desktop/CMakeLists.txt` | Includes the module, Android id/name |
| `src/desktop/android/.../ScalingDialog.java`, `DrawpileResources.java.in` | Resource alias so non-`net.drawpile` package names build |

See `git diff 2.3.1-beta.2 -- src/desktop/mainwindow.cpp` for the complete
upstream-facing footprint.

## Building

Desktop (for development and testing), Linux:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCLIENT=ON \
        -DSERVER=OFF -DBUILTINSERVER=ON -DTOOLS=OFF -DTESTS=ON \
        -DUSE_GENERATORS=OFF
    cmake --build build
    # Run in the mobile interface mode:
    build/bin/drawpile    # then Preferences ▸ User interface ▸ Mobile

Android: follow upstream's instructions at
<https://docs.drawpile.net/help/development/buildingfromsource> using
`pkg/android/build.bash`, or push this branch to a GitHub fork as `mobileui`
(or `mobile-ui`) and let the "Drawpile Mobile (fork) Android APK" workflow
build the APKs. Add the repository secrets `ANDROID_KEYSTORE` (base64-encoded
keystore with a key alias `drawpile`) and `ANDROID_KEYSTORE_PASS` to sign with
your own key. Without them the APKs are signed with a test key that the
workflow generates once and keeps in the Actions cache; never commit a
keystore to the repository. The upstream `main.yml` CI skips these branches.

## Translations

The fork's catalogs are generated:

    cd src
    lupdate -no-obsolete -locations none desktop/mobile -I . \
        -ts desktop/mobile/i18n/mobileui_template.ts
    python3 desktop/mobile/i18n/generate.py

Translations of the fork's own strings live in
`i18n/data/mobile_strings.json`. Fixes for upstream strings live in
`i18n/data/upstream_fixes.json` and are only emitted while upstream still lacks
a translation, so upstream translations automatically take over after merging.

## License

Same as Drawpile: GNU General Public License version 3 or later, see
[LICENSE.txt](LICENSE.txt). The fork's new icons in `src/desktop/mobile/icons`
were drawn for this fork and are under the same license. Other icons come from
Drawpile's bundled icon theme.
