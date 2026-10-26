-- HTML Viewer plugin for FAR Manager
-- Place in: %APPDATA%\Far Manager\Profile\Macros\scripts\html-viewer.lua
-- Requires: LuaMacro plugin (included with FAR Manager)

local HtmlViewer = {}

-- HTML to plain text conversion
function HtmlViewer.htmlToText(html)
    if not html then return "" end

    -- Remove script blocks
    html = html:gsub("<script[^>]*>.-</script>", "")
    -- Remove style blocks
    html = html:gsub("<style[^>]*>.-</style>", "")
    -- Remove comments
    html = html:gsub("<!%-%-.-%-%->", "")

    -- Replace block elements with newlines
    html = html:gsub("<br%s*/?>", "\n")
    html = html:gsub("<p[^>]*>", "\n\n")
    html = html:gsub("</p>", "\n")
    html = html:gsub("<div[^>]*>", "\n")
    html = html:gsub("</div>", "\n")
    html = html:gsub("<h[1-6][^>]*>", "\n\n")
    html = html:gsub("</h[1-6]>", "\n")
    html = html:gsub("<li[^>]*>", "\n  • ")
    html = html:gsub("<tr[^>]*>", "\n")
    html = html:gsub("<td[^>]*>", "  ")
    html = html:gsub("<th[^>]*>", "  ")

    -- Remove all remaining tags
    html = html:gsub("<[^>]+>", "")

    -- Decode HTML entities
    html = html:gsub("&nbsp;", " ")
    html = html:gsub("&amp;", "&")
    html = html:gsub("&lt;", "<")
    html = html:gsub("&gt;", ">")
    html = html:gsub("&quot;", '"')
    html = html:gsub("&apos;", "'")
    html = html:gsub("&#(%d+);", function(n) return string.char(tonumber(n)) end)
    html = html:gsub("&#x(%x+);", function(n) return string.char(tonumber(n, 16)) end)

    -- Collapse multiple newlines
    html = html:gsub("\n{3,}", "\n\n")

    -- Trim
    html = html:match("^%s*(.-)%s*$")

    return html
end

-- Read file with encoding detection
function HtmlViewer.readFile(path)
    local f = io.open(path, "rb")
    if not f then return nil end

    local content = f:read("*a")
    f:close()

    if not content then return nil end

    -- Check for UTF-16 LE BOM
    if content:sub(1, 2) == "\xFF\xFE" then
        -- Convert UTF-16 LE to UTF-8 (simplified)
        local utf8 = {}
        local i = 3
        while i <= #content - 1 do
            local low = content:byte(i)
            local high = content:byte(i + 1)
            local code = low + high * 256
            if code < 0x80 then
                table.insert(utf8, string.char(code))
            elseif code < 0x800 then
                table.insert(utf8, string.char(0xC0 + (code >> 6), 0x80 + (code & 0x3F)))
            else
                table.insert(utf8, string.char(0xE0 + (code >> 12), 0x80 + ((code >> 6) & 0x3F), 0x80 + (code & 0x3F)))
            end
            i = i + 2
        end
        return table.concat(utf8)
    end

    -- Check for UTF-8 BOM
    if content:sub(1, 3) == "\xEF\xBB\xBF" then
        return content:sub(4)
    end

    -- Assume UTF-8 or ANSI
    return content
end

-- Open HTML file in viewer
function HtmlViewer.open(path)
    local html = HtmlViewer.readFile(path)
    if not html then
        far.Message("Cannot read file: " .. path, "HTML Viewer", "OK", "w")
        return false
    end

    local text = HtmlViewer.htmlToText(html)

    if text == "" then
        far.Message("Empty or invalid HTML file", "HTML Viewer", "OK", "w")
        return false
    end

    -- Open in FAR viewer
    far.Viewer(text, "HTML: " .. far.GetFileName(path), {
        ["exit"] = function() end
    })

    return true
end

-- Plugin registration
local Guid = win.Uuid("A1B2C3D4-E5F6-7890-ABCD-EF1234567891")

Macro {
    area = "Shell";
    key = "F3";
    description = "HTML Viewer: open .html/.htm files";
    condition = function()
        local file = far.GetFileName(Panel.GetPanelInfo(nil, 1).CurrentItem)
        if not file then return false end
        local ext = file:match("%.([^.]+)$")
        return ext and (ext:lower() == "html" or ext:lower() == "htm")
    end;
    action = function()
        local file = far.GetFileName(Panel.GetPanelInfo(nil, 1).CurrentItem)
        if file then
            HtmlViewer.open(file)
        end
    end;
}

Macro {
    area = "Common";
    key = "CtrlShiftH";
    description = "HTML Viewer: convert clipboard HTML to text";
    action = function()
        local clip = far.GetClipboard()
        if clip and clip:find("<") then
            local text = HtmlViewer.htmlToText(clip)
            far.SetClipboard(text)
            far.Message("HTML converted to text", "HTML Viewer", "OK")
        else
            far.Message("Clipboard does not contain HTML", "HTML Viewer", "OK", "w")
        end
    end;
}

-- Add to F11 plugins menu
PluginMenu {
    guid = Guid;
    description = "HTML Viewer";
    action = function()
        local file = far.InputBox("HTML Viewer", "Enter HTML file path:", "")
        if file and file ~= "" then
            HtmlViewer.open(file)
        end
    end;
}
