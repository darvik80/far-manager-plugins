-- HTML Viewer for FAR Manager, hosted by LuaFAR (luaplug wrapper).
-- Requires html_to_lines() from lua/html_convert.lua to be loaded first;
-- colored rendering is provided by the native luaplug_ui.ShowStyledText.

local F = far.Flags
local MenuGuid = win.Uuid("0B5D7E13-9C48-4A6B-AE71-5F3D8C2A6B94")
local VK_F3 = 0x72

local function ReadAll(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local data = f:read("*a")
  f:close()
  return data
end

-- FAR's Lua string library indexes UTF-8 characters, not bytes, so binary
-- UTF-16BE data has to be split into raw bytes character chunk by character chunk.
local function SwapUtf16Bytes(bytes)
  local out = {}
  local pending
  local function put(byte)
    if pending then
      out[#out + 1] = byte .. pending
      pending = nil
    else
      pending = byte
    end
  end
  local i = 1
  while true do
    local ch = bytes:sub(i, i)
    if ch == "" then break end
    if #ch == 1 then
      put(ch)
    else
      local cp = ch:byte(1)
      if cp < 0x800 then
        put(string.char(0xC0 + math.floor(cp / 0x40)))
        put(string.char(0x80 + cp % 0x40))
      elseif cp < 0x10000 then
        put(string.char(0xE0 + math.floor(cp / 0x1000)))
        put(string.char(0x80 + math.floor(cp / 0x40) % 0x40))
        put(string.char(0x80 + cp % 0x40))
      else
        put(string.char(0xF0 + math.floor(cp / 0x40000)))
        put(string.char(0x80 + math.floor(cp / 0x1000) % 0x40))
        put(string.char(0x80 + math.floor(cp / 0x40) % 0x40))
        put(string.char(0x80 + cp % 0x40))
      end
    end
    i = i + 1
  end
  if pending then out[#out + 1] = pending end
  return table.concat(out)
end

local function DecodeHtml(bytes)
  if bytes:sub(1, 1) == "\239\187\191" then
    return bytes:sub(2)
  end
  if bytes:sub(1, 2) == "\255\254" then
    return win.Utf16ToUtf8(bytes:sub(3))
  end
  if bytes:sub(1, 2) == "\254\255" then
    return win.Utf16ToUtf8(SwapUtf16Bytes(bytes:sub(3)))
  end
  if win.MultiByteToWideChar(bytes, 65001, "e") then
    return bytes
  end
  local wide = win.MultiByteToWideChar(bytes, win.GetACP(), "")
  if wide then return win.Utf16ToUtf8(wide) end
  return bytes
end

local function IsHtmlFile(name)
  local ext = name:match("%.([^%.\\/]+)$")
  if not ext then return false end
  ext = ext:lower()
  return ext == "html" or ext == "htm" or ext == "xhtml"
end

local function IsRegularFile(path)
  local info = win.GetFileInfo(path)
  if not info then return false end
  return not (info.FileAttributes and info.FileAttributes:find("d", 1, true))
end

local function ShowHtml(path)
  if not IsRegularFile(path) then
    far.Message("Файл не найден:\n" .. path, "HTML Viewer")
    return false
  end

  local bytes = ReadAll(path)
  if not bytes or bytes == "" then
    far.Message("Файл пуст или не читается.", "HTML Viewer")
    return false
  end

  local ok, lines = pcall(html_to_lines, DecodeHtml(bytes))
  if not ok then
    far.Message("Ошибка конвертации:\n" .. tostring(lines), "HTML Viewer")
    return false
  end
  if not lines or #lines == 0 then
    far.Message("В файле нет текстового содержимого.", "HTML Viewer")
    return false
  end

  if not luaplug_ui or not luaplug_ui.ShowStyledText then
    far.Message("Нативный просмотрщик недоступен.\n" ..
                "Проверьте, что плагин установлен вместе с LuaFAR-хостом.",
                "HTML Viewer")
    return false
  end

  local title = "HTML: " .. (path:match("([^\\/]+)$") or path)
  local shown, result = pcall(luaplug_ui.ShowStyledText, title, lines)
  if not shown then
    far.Message("Ошибка просмотрщика:\n" .. tostring(result), "HTML Viewer")
    return false
  end
  return result and true or false
end

local function CurrentPanelFile()
  local item = panel.GetCurrentPanelItem(nil, 1)
  if not item or not item.FileName then return nil end
  if item.FileAttributes and item.FileAttributes:find("d", 1, true) then return nil end
  if item.FileName:match("^%a:[/\\]") or item.FileName:sub(1, 2) == "\\\\" then
    return item.FileName
  end
  local dir = panel.GetPanelDirectory(nil, 1)
  if not dir or not dir.Name then return nil end
  local base = dir.Name
  if base:sub(-1) ~= "\\" then base = base .. "\\" end
  return base .. item.FileName
end

local function CommandLinePath(item)
  local name = tostring(item or "")
  name = name:gsub("^%s+", ""):gsub("%s+$", "")
  if name:lower():sub(1, 9) == "htmlview:" then
    name = name:sub(10):gsub("^%s+", "")
  end
  name = name:gsub('^"(.*)"$', "%1")
  return name
end

function export.GetPluginInfo()
  return {
    PluginMenuStrings = { "HTML Viewer" },
    PluginMenuGuids   = MenuGuid,
    CommandPrefix     = "htmlview",
  }
end

function export.Open(OpenFrom, Guid, Item)
  local path

  if OpenFrom == F.OPEN_COMMANDLINE then
    path = CommandLinePath(Item)
    if path == "" then
      far.Message("Укажите файл: htmlview: <файл>", "HTML Viewer")
      return
    end
  elseif OpenFrom == F.OPEN_PLUGINSMENU then
    path = CurrentPanelFile()
    if not path then
      far.Message("Выберите файл .html, .htm или .xhtml", "HTML Viewer")
      return
    end
  else
    return
  end

  if not IsHtmlFile(path) then
    far.Message("Это не HTML-файл:\n" .. path, "HTML Viewer")
    return
  end

  ShowHtml(path)
end

function export.ProcessConsoleInput(rec, flags)
  if rec.EventType ~= 1 or not rec.KeyDown then return 0 end
  if rec.VirtualKeyCode ~= VK_F3 then return 0 end
  if bit64.band(rec.ControlKeyState or 0, 0x1F) ~= 0 then return 0 end

  local wt = actl.GetWindowType()
  if not wt or wt.Type ~= 1 then return 0 end

  local path = CurrentPanelFile()
  if not path or not IsHtmlFile(path) or not IsRegularFile(path) then return 0 end

  ShowHtml(path)
  return 1
end

function export.Configure(Guid)
  far.Message("F3 на .html/.htm/.xhtml — просмотр с темами и стилями.\n" ..
              "F11 → HTML Viewer, команда: htmlview:\n\n" ..
              "В просмотрщике:\n" ..
              "  F8 / Shift+F8 — тема (как в Markdown Viewer)\n" ..
              "  F7 / Shift+F7 — поиск\n" ..
              "  Ctrl+↑ / Ctrl+↓ — заголовки\n" ..
              "  F3 или Esc — закрыть\n\n" ..
              "Поддерживаются заголовки, списки, таблицы, цитаты, код,\n" ..
              "inline CSS и простые правила <style> (тег, .class, #id).",
              "HTML Viewer", nil, "w")
end
