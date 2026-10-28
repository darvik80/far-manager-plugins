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

Install only through `install-plugin.bat <Name>` or `install-all.bat`. They elevate and copy into `Plugins\<Name>`. Do not copy into Program Files from the agent. Far must be closed; a loaded DLL will not overwrite. Do not commit unless asked. Runtime is `MultiThreaded`. New C++ exports go in `exports.def` with the next ordinal.

## LuaFAR plugins

Do not write C++ for a Lua plugin. `cmake/LuaFarPlugin.cmake` exposes `add_luafar_plugin(<Name> LUA_FILES ...)`, and the generic `luaplug` host lives only in `third_party/luafar` (never copy it into a plugin). A Lua plugin dir contains `CMakeLists.txt`, `_globalinfo.lua` (unique GUID/title/version), and `lua/*.lua`. The host loads `_globalinfo.lua` and `<DLL name>.lua` from the DLL directory, so every plugin installs into its own `Plugins\<Name>` and the installer copies the generated `.lua` files next to the DLL. Behavior belongs in Lua. FAR's Lua strings are UTF-8-aware, so byte loops over binary data are wrong; see the BOM/UTF-16 helpers in HtmlViewer `plugin.lua`.

## Dialogs

Layout lives in `sdk/far/form.hpp`: width 63, labels at X=2, edits at X=12 through 60, title `" Name "`, separator then `{ OK } [ Отмена ]`. Open with `farform::OpenGuarded` (leftover Enter/F4/F7) or `farform::AskText`. Unique DialogGuid, never the F11 MenuGuid. `FarOpenDialog` in `sdk/far/dialog.hpp` is the low-level call. Far's `DialogInit` takes `intptr_t` X1/Y1/X2/Y2. Declaring them as `int` on x64 leaves garbage in the high 32 bits of the stack slots: Far sees a huge size, returns `INVALID_HANDLE_VALUE`, and the same call often works the second time. A dialog proc that returns 0 for `DN_DRAWDIALOG` never paints and hangs the UI. Unhandled messages go to `Info.DefDlgProc`. A null proc is safe. Read edits with `DM_GETCONSTTEXTPTR` (63), checks with `DM_GETCHECK` (25, `BSTATE_CHECKED` is 1). `DialogInit(-1,-1,w,h)` centers. Password fields are `DI_PSWEDIT`. User-guide PNGs: `python docs/tools/far_shots.py`.

Create is `Shift+F4`, edit is `F4`. Far does not deliver key-up to `ProcessPanelInputW` for function keys, so do not wait for it. Open F4/F7/Shift+F4 dialogs from `ProcessSynchroEventW` after `AdvControl(MainGuid, ACTL_SYNCHRO, 0, param)` — never from inside the keydown handler. `DialogInit` Id must be a unique DialogGuid, not the live F11 `MenuGuid`. Do not open the next dialog with Enter: that key is still queued and activates `DIF_DEFAULTBUTTON`. A mouse click does not. Eat leftover Enter/F4/F7 only for ~400 ms after `DN_INITDIALOG`, and stop on `DN_EDITCHANGE` / `DN_BTNCLICK` so the user's Save Enter is not swallowed. `DN_INPUT` (only after `DM_SETINPUTNOTIFY`, 49) returns 0 to consume; `DN_CONTROLINPUT` returns nonzero. Never refuse `DN_CLOSE` for Save (that made the first save appear to fail). Never refuse `param1 < 0` or `DialogInit` fails. `DN_DRAWDIALOG` always goes to `DefDlgProc`. Read edits with `DM_GETTEXT` (7) + `FarDialogItemData` while the dialog is alive (`DN_BTNCLICK` / `DN_CLOSE`); `DM_GETCONSTTEXTPTR` (63) can miss the live line editor. Classify Shift with `SHIFT_PRESSED`, not `GetAsyncKeyState(VK_SHIFT)` on every F4.

## Menus and keys

Official menu flags, not the low bits in the subset header: `MIF_SELECTED` `0x10000`, `MIF_SEPARATOR` `0x40000`, `FMENU_SHOWAMPERSAND` `0x1`, `FMENU_WRAPMODE` `0x2`. `Menu` returns the item index, or -1 on Esc.

The digit in Alt+F1/Alt+F2 is not part of `DiskMenu.Strings`. Far reads it from `%APPDATA%\Far Manager\Profile\pluginhotkeys.db`: `pluginkey` is the plugin Main GUID, `menuguid` is the disk-menu GUID, `type` 0 is the drive menu. `EnsureDriveHotkey` writes that row only when the hotkey is empty.

`ProcessPanelInputW` runs only for the plugin that owns the active panel. F3 on a normal file panel needs `ProcessConsoleInputW`. Plain F3 means no Ctrl/Alt/Shift. F3 on the SSH panel is not seen by Markdown or Pretty Viewer; SSH downloads `.md`, `.json`, `.yaml`, `.yml`, and `.xml` and opens them with `Plugin.Command(pluginGuid, localPath)`. `ACTL_GETWINDOWTYPE` is 28: pass `WindowType { size_t StructSize; int Type; }`. Panels are `Type == 1`, or `Type == 0` and the call returned 1. Returning 1 swallows the key.

Secret Vault stores passwords and OpenSSH keys in `%APPDATA%\Far Manager\SecretVault\vault.bin` via Windows DPAPI. SSH looks them up with `Plugin.SyncCall` (`OPEN_FROMMACRO`): first arg is the SSH Client Main GUID, then verbs `lookup` / `list` / `store`. Other callers get an empty reply. Do not use `Plugin.Command` for that call; it yields across a C boundary. If the local vault miss, SSH falls back to NoteKeeper REST (`common/notekeeper`). PAT is DPAPI in `%APPDATA%\Far Manager\NoteKeeper\settings.ini`; `sync.ini` keeps URL and `lastSync` only. URLs must be HTTPS (http only for localhost). Vault host lookup is exact (`NkExtractHost`), not substring. Temp PEM/F3 files go through `NkSecureTempPath` under `%APPDATA%\Far Manager\<leaf>` (current-user DACL, not `%TEMP%`) and are wiped on `ExitFARW`. NoteKeeper plugin (`plugins/note-keeper`) lists notes/todos; disk hotkey `8`.

Non-Teleport SSH with a vault key or explicit key file uses that identity only (`-i`, `IdentitiesOnly=yes`, `-F NUL`). Do not set `IdentityAgent=none`: passphrase-protected files like `id_rsa` are unlocked in the agent, and SFTP `BatchMode` cannot decrypt them. Teleport still uses `tsh` certs. If `tsh.exe` is on PATH, the SSH panel shows Teleport nodes from `tsh ls` and uses `tsh proxy ssh`. Expired certs: F6 runs `tsh login --mfa-mode=otp` in a new console so password+OTP can be typed (SSO/browser still works). `tsh proxy ssh` keeps `--no-relogin`: stdin is the SSH pipe, so a second OTP prompt there would break the handshake. Per-session MFA after login needs WebAuthn or a fresh F6 login. Do not auto-login on every panel refresh. F2 / Shift+Enter opens a real terminal: `tsh ssh --mfa-mode=otp` (OTP in that window) or `ssh.exe -t`. On the host list F3 shows the card, F4 edits (Teleport name/host/port are read-only).

## Viewers

User-drawn text is a `DI_USERCONTROL` whose `VBuf` is `width * height` `FAR_CHAR_INFO`. Do not reallocate that buffer while the dialog is open. Repaint with `DM_REDRAW` (14). Fullscreen uses `ACTL_GETFARRECT` (24) and absolute dialog coordinates.

LuaFAR's `SetFarDialogItem` cannot fill `DI_USERCONTROL.VBuf`, so colored Lua viewers go through the shared host: `luaplug_ui.ShowStyledText(title, lines)` is registered in `third_party/luafar/styled_viewer.cpp`. Each line is `{ style="normal|h1..h6|code|quote|hr|empty|table", indent=N, fence=0|1|2, lang="c", segments={...} }`; each segment is `{ text, style="normal|code|link|image|marker", bold, italic, underline, strike, fg=0xRRGGBB, bg=0xRRGGBB, hint }`. The host resolves the 16-theme palette from `sdk/render/themes.hpp`, handles F8/Shift+F8, F7 find, Ctrl+Up/Down headings, table layout, scrolling and mouse wheel. CSS foreground/background is only applied when its contrast against the effective background is >= 3:1; headings always keep theme colors. Do not add per-plugin native viewers — extend the shared one.
