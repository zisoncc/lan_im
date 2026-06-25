# Changelog

## 1.0.1 - 2026-06-26

### Added
- Android server mode now supports one-to-one chats by selecting a connected client.
- Android and Windows chat messages can be copied from the chat history.
- Android message bubbles are focusable for TV remote control usage.
- Android requests LAN discovery and file storage permissions at startup.
- Added Android TV-friendly file picker failure handling.

### Changed
- Android received files are saved under `Download/LanChat`.
- Android file receiving now has a fallback path for older Android TV systems.
- Windows chat history now uses IM-style alignment, with local messages on the right and received messages on the left.
- Android version updated to `1.0.1`.

### Fixed
- Fixed Android client disconnects caused by network writes running on the main thread.
- Fixed Chinese text garbling when Android sends messages to the Windows side.
- Fixed Windows file sending for paths containing Chinese characters.
- Fixed file transfer stalls caused by concurrent socket writes.
- Fixed Android server text/file sending to Windows clients.
- Fixed Android server echoing messages back to the sender.

