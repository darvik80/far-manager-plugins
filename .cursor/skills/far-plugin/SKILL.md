---
name: far-plugin
description: >-
  Build and install Far Manager 3 plugins in this repository. Use when editing
  plugins/, sdk/far, install bats, dialogs, menus, panels, viewers, or F3 hooks.
---

# Far plugins in this repo

Native C++ plugins for the installed Far at `C:\Program Files\Far Manager` (build 6699). SDK is `sdk/far/plugin.hpp`, a subset. Do not call `g_info.FSF`: that table is a stub and does not match Far.

## Build and install

```powershell
cmake -S D:\Projects\cpp\far-manager -B D:\Projects\cpp\far-manager\build-msvc
cmake --build D:\Projects\cpp\far-manager\build-msvc --config Release --target <Name>
```

PowerShell rejects `&&`. Use `; if ($LASTEXITCODE -eq 0)`.

Install only through `install-plugin.bat <Name>` or `install-all.bat`. They elevate and copy into `Plugins\<Name>`. Do not copy into Program Files from the agent. Far must be closed; a loaded DLL will not overwrite. Do not commit unless asked. Runtime is `MultiThreaded`. New exports go in `exports.def` with the next ordinal.

## Dialogs

A dialog proc that returns 0 for `DN_DRAWDIALOG` never paints and hangs the UI. Unhandled messages go to `Info.DefDlgProc`. A null proc is safe. Read edits with `DM_GETCONSTTEXTPTR` (63), checks with `DM_GETCHECK` (25, `BSTATE_CHECKED` is 1). `DialogInit(-1,-1,w,h)` centers. Password fields are `DI_PSWEDIT`.

Create is `Shift+F4`, edit is `F4`. Do not open the next dialog with Enter: that key is still queued and activates `DIF_DEFAULTBUTTON`. A mouse click does not. Eat that Enter until a `DN_BTNCLICK` or until one Enter has been seen and the key is up. `DN_INPUT` (only after `DM_SETINPUTNOTIFY`, 49) returns 0 to consume; `DN_CONTROLINPUT` returns nonzero. Return 0 from `DN_CLOSE` for any other close while that Enter is still pending, including a close whose id is not the Save button. Do not clear the guard from `GetAsyncKeyState` before the queued key is handled. `DN_DRAWDIALOG` always goes to `DefDlgProc`.

## Menus and keys

Official menu flags, not the low bits in the subset header: `MIF_SELECTED` `0x10000`, `MIF_SEPARATOR` `0x40000`, `FMENU_SHOWAMPERSAND` `0x1`, `FMENU_WRAPMODE` `0x2`. `Menu` returns the item index, or -1 on Esc.

`ProcessPanelInputW` runs only for the plugin that owns the active panel. F3 on a normal file panel needs `ProcessConsoleInputW`. Plain F3 means no Ctrl/Alt/Shift. `ACTL_GETWINDOWTYPE` is 28: pass `WindowType { size_t StructSize; int Type; }`. Panels are `Type == 1`, or `Type == 0` and the call returned 1. Returning 1 swallows the key.

## Viewers

User-drawn text is a `DI_USERCONTROL` whose `VBuf` is `width * height` `FAR_CHAR_INFO`. Do not reallocate that buffer while the dialog is open. Repaint with `DM_REDRAW` (14). Fullscreen uses `ACTL_GETFARRECT` (24) and absolute dialog coordinates.
