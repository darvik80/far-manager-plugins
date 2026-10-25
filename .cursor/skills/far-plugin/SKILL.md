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

Enter that closed a menu is queued and processed by the next dialog after the key is already up, so `GetAsyncKeyState(VK_RETURN)` does not see it. `DIF_DEFAULTBUTTON` then closes the dialog. `DN_INPUT` arrives only after `DM_SETINPUTNOTIFY` (49); return 0 to consume it. Return nonzero from `DN_CONTROLINPUT` to consume that message. Return 0 from `DN_CLOSE` to keep the dialog open. Ignore that opening Enter for a few hundred milliseconds, and refuse Save while a required field is empty. A later Enter must reach `DefDlgProc`. `DN_DRAWDIALOG` always goes to `DefDlgProc`.

## Menus and keys

Official menu flags, not the low bits in the subset header: `MIF_SELECTED` `0x10000`, `MIF_SEPARATOR` `0x40000`, `FMENU_SHOWAMPERSAND` `0x1`, `FMENU_WRAPMODE` `0x2`. `Menu` returns the item index, or -1 on Esc.

`ProcessPanelInputW` runs only for the plugin that owns the active panel. F3 on a normal file panel needs `ProcessConsoleInputW`. Plain F3 means no Ctrl/Alt/Shift. `ACTL_GETWINDOWTYPE` is 28: pass `WindowType { size_t StructSize; int Type; }`. Panels are `Type == 1`, or `Type == 0` and the call returned 1. Returning 1 swallows the key.

## Viewers

User-drawn text is a `DI_USERCONTROL` whose `VBuf` is `width * height` `FAR_CHAR_INFO`. Do not reallocate that buffer while the dialog is open. Repaint with `DM_REDRAW` (14). Fullscreen uses `ACTL_GETFARRECT` (24) and absolute dialog coordinates.
