# DREADNOUGHT for iPad

The DREADNOUGHT web editor as an iPad app. The app works with the FM-1 on the iPad, where Safari can't: Safari has no
Web MIDI, and the editor talks to the FM-1 over MIDI. The app carries the same pages as the website (the editor, the
breaks page and the flasher) and gives them what Chrome would: the iPad's MIDI ports through CoreMIDI, downloads, a
projects folder and the clipboard. It's the editor of the same commit, so it changes when the editor does.

## Install

**SideStore / AltStore (no Mac needed after setup).** Download the newest `DREADNOUGHT-iPad-….ipa` from the
[**ipad** release](https://github.com/TwistedGenetics/DREADNOUGHT/releases/tag/ipad) on the iPad, open SideStore (or
AltStore), tap **+** and pick the file. They sign it with your Apple ID. With a free Apple ID it lasts 7 days:
SideStore refreshes it on the iPad, AltStore from a computer on your network.

**Xcode (a Mac).** `brew install xcodegen`, then in this folder `xcodegen` and open `DreadnoughtEditor.xcodeproj`.
In the target's **Signing & Capabilities** tab, pick your team (your Apple ID), plug in the iPad and press Run. With a
free Apple ID it lasts 7 days (Run again to renew it); with a paid developer account, a year.

## Use

Connect the FM-1 to the iPad with a USB-C cable (or, on a Lightning iPad, Apple's Lightning to USB 3 camera adapter).
Open the app and press **Connect FM-1**. Everything else is as on a computer.

- **Saved files** (Save project without a folder, backups, sounds) go to **Files › On My iPad › DREADNOUGHT**.
- **Choose a projects folder** uses **Projects** in there; projects you put there in Files show up in the list.
- **Loading files** (samples, DX7 banks, wavetables, projects) opens the iPad's file picker.
- The links to the breaks page and the flasher open them in the app; swipe from the left edge to go back.
- The screen stays on while the app is open, so a long transfer isn't cut off by the iPad locking.
- **Pinch** to zoom if the text is too small.

The flasher works in the app too, but it hasn't been tried with a real FM-1 on an iPad yet: update firmware from a
computer until it has been.

## How it works

- `Sources/BundleSchemeHandler.swift`: serves the repo's `web/` folder at `dreadnought://app/`, so the pages and their
  modules load as on GitHub Pages. The flasher's release list and firmware files come from the live site.
- `Resources/bridge.js`: runs before the pages. It provides `navigator.requestMIDIAccess` (with SysEx, with ports
  that come and go), saves `download` links to the app's folder, and provides `showDirectoryPicker` (the Projects
  folder) and `navigator.clipboard.writeText`.
- `Sources/MIDIBridge.swift`: CoreMIDI. It listens to every source, joins up SysEx split across packets, sends
  frames out, and tells the page when ports change.
- `Sources/FilesBridge.swift`: the app's folder in Files.
- `Sources/EditorViewController.swift`: the full-screen web view, the pages' alert / confirm, links to the web in
  Safari, and a self-test (`-selftest`: the editor in demo mode, its state and a picture saved to Documents) that CI
  runs in an iPad simulator.

## Tests and builds

`.github/workflows/ipad.yml` runs on every push that changes `ios/` or `web/`:

- `test/bridge_e2e.py` runs the editor in Chromium, connected through `bridge.js` to the simulated FM-1. It checks
  connecting, ports coming and going, saving and loading a project through the folder, downloads and the clipboard.
- The app is built and started in an iPad simulator, and its self-test must pass.
- An unsigned `.ipa` is built. From `main`, it replaces the one on the **ipad** pre-release (kept apart from the
  firmware releases, so the firmware stays the latest release and the flasher's list doesn't change).
