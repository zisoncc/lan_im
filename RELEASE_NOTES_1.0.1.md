# LanChat 1.0.1 Release Notes

Release date: 2026-06-26

## Highlights

- Better Android TV support, including startup permission requests, focusable chat messages, and safer file picker behavior.
- Android server mode now supports selecting a connected PC client for one-to-one chat instead of broadcasting to every client.
- File transfer stability is improved across Android and Windows, including Chinese file path support on Windows.
- Chat history is more IM-like: sent messages appear on the right, received messages on the left, and text messages can be copied.

## Install Notes

- Android APK: `LanChatAndroid/app/build/outputs/apk/release/app-release.apk`
- Windows executable: `bin/LanChat.exe`
- Android upgrades require the same signing certificate. If Android reports a signature mismatch, uninstall the old package before installing this version.

## Known Notes

- Received Android files are stored in `Download/LanChat`.
- On some Android TV systems, sending files depends on whether the system provides a compatible file picker.
