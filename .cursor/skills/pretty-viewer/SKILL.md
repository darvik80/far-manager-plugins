---
name: pretty-viewer
description: >-
  Pretty-print JSON, YAML, and XML in the Far PrettyViewer plugin. Use when
  editing plugins/pretty-viewer, adding a structured format, or when the user
  mentions JSON, YAML, XML, or pretty view.
---

# Pretty Viewer

One plugin, `plugins/pretty-viewer`, DLL `PrettyViewer`. It owns `.json`, `.yaml`, `.yml`, and `.xml`. F3 on those files opens it. Other extensions return 0 so Markdown Viewer still receives F3 for `.md`.

Open from F11 **Pretty Viewer** or the command `pretty <path>`. Inside the view: F2 toggles the highlighted original, F7 searches, Shift+F7 searches backward, F8 / Shift+F8 cycles themes, Esc closes. The first theme is **Far** (navy `0x000080`), then Midnight, Darcula, Light.

## Layout

- `src/format.cpp` parses and pretty-prints. `FormatDocument` picks the format from the extension.
- `src/viewer.cpp` paints `ViewLine` spans. Token colors live in the theme table there.
- `src/main.cpp` reads UTF-8, UTF-16 BOM, or the ANSI code page, and owns the F3 hook.
- `PrettyViewerTest` checks the formatters. Build target `PrettyViewerTest` and run `build-msvc/plugins/pretty-viewer/Release/PrettyViewerTest.exe`.

Install with Far closed: `install-plugin.bat PrettyViewer`.

## Formats

JSON is strict: objects, arrays, strings, numbers, `true`, `false`, `null`, two-space indent. A failure keeps a loose highlight and a first line `ошибка: ...`.

YAML reindents block maps, lists, comments, and `|` / `>` blocks to two spaces. It does not expand anchors, aliases, merge keys, or tags. A line it does not understand is shown as text, not a hard error.

XML pretty-prints elements, attributes, comments, CDATA, processing instructions, and doctype. Short text stays on the open tag's line.

Depth is capped at 64. Add a format by extending `FormatDocument`, `IsPrettyFile` in `main.cpp`, and a case in `format_test.cpp`. Do not give the new extension to another plugin's F3 hook.
