-- HTML to styled lines converter for the HtmlViewer FAR plugin.
--
-- Entry point:
--   html_to_lines(html) -- array of lines for luaplug_ui.ShowStyledText
--
-- Lua 5.1 / LuaFAR: strings are UTF-8-aware, so all indexing is by character.
--
-- Supported markup: headings, paragraphs, bold/italic/underline/strike,
-- inline code and pre blocks, links, images, lists, blockquotes, rules,
-- tables, entities, inline CSS (color/background-color/font-weight/
-- font-style/text-decoration/display) and simple <style> tag/class/id rules.

local named_entities = {
  nbsp = " ", amp = "&", lt = "<", gt = ">", quot = '"', apos = "'",
  mdash = "-", ndash = "-", laquo = '"', raquo = '"', ldquo = '"', rdquo = '"',
  lsquo = "'", rsquo = "'", hellip = "...", copy = "(c)", reg = "(r)",
  trade = "(tm)", euro = "EUR", deg = "deg", times = "x", middot = "-", bull = "*",
  shy = "",
}

local named_colors = {
  black = 0x000000, silver = 0xC0C0C0, gray = 0x808080, grey = 0x808080,
  white = 0xFFFFFF, maroon = 0x800000, red = 0xFF0000, purple = 0x800080,
  fuchsia = 0xFF00FF, magenta = 0xFF00FF, green = 0x008000, lime = 0x00FF00,
  olive = 0x808000, yellow = 0xFFFF00, navy = 0x000080, blue = 0x0000FF,
  teal = 0x008080, aqua = 0x00FFFF, cyan = 0x00FFFF, orange = 0xFFA500,
  brown = 0xA52A2A, pink = 0xFFC0CB, gold = 0xFFD700, darkred = 0x8B0000,
  darkgreen = 0x006400, darkblue = 0x00008B,
  lightgray = 0xD3D3D3, lightgrey = 0xD3D3D3,
}

local function utf8_char(code)
  if code < 0x80 then
    return string.char(code)
  elseif code < 0x800 then
    return string.char(0xC0 + math.floor(code / 0x40), 0x80 + code % 0x40)
  elseif code < 0x10000 then
    return string.char(0xE0 + math.floor(code / 0x1000),
                       0x80 + math.floor(code / 0x40) % 0x40,
                       0x80 + code % 0x40)
  end
  return string.char(0xF0 + math.floor(code / 0x40000),
                     0x80 + math.floor(code / 0x1000) % 0x40,
                     0x80 + math.floor(code / 0x40) % 0x40,
                     0x80 + code % 0x40)
end

local function decode_entities(s)
  s = s:gsub("&#[xX](%x+);", function(hex)
    local code = tonumber(hex, 16)
    if code and code > 0 and code < 0x110000 then return utf8_char(code) end
    return ""
  end)
  s = s:gsub("&#(%d+);", function(dec)
    local code = tonumber(dec)
    if code and code > 0 and code < 0x110000 then return utf8_char(code) end
    return ""
  end)
  s = s:gsub("&(%a+);", function(name)
    local value = named_entities[name:lower()]
    if value then return value end
    return "&" .. name .. ";"
  end)
  return s
end

local function trim(s)
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- ---------------------------------------------------------------- CSS ----

local function clamp_channel(value)
  value = tonumber(value) or 0
  if value < 0 then return 0 end
  if value > 255 then return 255 end
  return math.floor(value)
end

local function parse_color(value)
  if not value then return nil end
  value = trim(value:lower())
  if value == "" or value == "transparent" or value == "inherit" or value == "currentcolor" then
    return nil
  end
  local hex = value:match("^#(%x%x%x%x%x%x)$")
  if hex then return tonumber(hex, 16) end
  hex = value:match("^#(%x%x%x)$")
  if hex then
    local r = hex:sub(1, 1)
    local g = hex:sub(2, 2)
    local b = hex:sub(3, 3)
    return tonumber(r .. r, 16) * 0x10000 + tonumber(g .. g, 16) * 0x100 + tonumber(b .. b, 16)
  end
  local r, g, b = value:match("^rgb%(%s*(%d+)%s*,%s*(%d+)%s*,%s*(%d+)%s*%)$")
  if not r then
    r, g, b = value:match("^rgba%(%s*(%d+)%s*,%s*(%d+)%s*,%s*(%d+)%s*,[^%)]*%)$")
  end
  if r then
    return clamp_channel(r) * 0x10000 + clamp_channel(g) * 0x100 + clamp_channel(b)
  end
  return named_colors[value]
end

local function parse_declarations(text)
  local decls = {}
  if not text then return decls end
  text = text:gsub("!%s*important", "")
  for part in text:gmatch("[^;]+") do
    local name, value = part:match("^%s*([%w_%-]+)%s*:%s*(.-)%s*$")
    if name and value and value ~= "" then
      decls[name:lower()] = value:lower()
    end
  end
  return decls
end

local function parse_selector(selector)
  selector = trim(selector)
  if selector == "" then return nil end
  selector = selector:gsub("%s+", " ")
  local last = selector:match("([^%s>+~]+)%s*$") or selector
  last = last:gsub(":.*$", "")
  last = last:gsub("%[.*%]$", "")
  if last == "" then return nil end

  local tag = last:match("^([%w_%-%*]+)") or ""
  if tag == "*" then tag = "" end
  local id = last:match("#([%w_%-]+)")
  local classes = {}
  for cls in last:gmatch("%.([%w_%-]+)") do classes[#classes + 1] = cls end

  local spec = #classes * 10 + (tag ~= "" and 1 or 0) + (id and 100 or 0)
  return { tag = tag:lower(), id = id, classes = classes, spec = spec }
end

local function parse_css(text)
  local rules = {}
  if not text or text == "" then return rules end
  text = text:gsub("/%*.-%*/", "")
  local order = 0
  for selectors, body in text:gmatch("([^{}]+){([^{}]*)}") do
    if not selectors:find("@", 1, true) then
      local decls = parse_declarations(body)
      if next(decls) then
        for selector in selectors:gmatch("[^,]+") do
          local rule = parse_selector(selector)
          if rule then
            order = order + 1
            rule.decls = decls
            rule.order = order
            rules[#rules + 1] = rule
          end
        end
      end
    end
  end
  return rules
end

local function attribute_classes(attrs)
  local classes = {}
  for cls in (attrs.class or ""):gmatch("%S+") do classes[cls] = true end
  return classes
end

local function rule_matches(rule, tag, attrs, classes)
  if rule.tag ~= "" and rule.tag ~= tag then return false end
  if rule.id and attrs.id ~= rule.id then return false end
  for _, cls in ipairs(rule.classes) do
    if not classes[cls] then return false end
  end
  return true
end

local function element_declarations(tag, attrs, css_rules)
  local classes = attribute_classes(attrs)
  local matched = {}
  for _, rule in ipairs(css_rules) do
    if rule_matches(rule, tag, attrs, classes) then matched[#matched + 1] = rule end
  end
  table.sort(matched, function(a, b)
    if a.spec == b.spec then return a.order < b.order end
    return a.spec < b.spec
  end)

  local decls = {}
  for _, rule in ipairs(matched) do
    for name, value in pairs(rule.decls) do decls[name] = value end
  end
  if attrs.style then
    local inline = parse_declarations(attrs.style)
    for name, value in pairs(inline) do decls[name] = value end
  end
  return decls
end

local function declarations_to_delta(decls)
  local delta = {}
  local color = parse_color(decls.color)
  if color then delta.fg = color end
  local background = parse_color(decls["background-color"] or decls.background)
  if background then delta.bg = background end

  local weight = decls["font-weight"]
  if weight then
    weight = trim(weight)
    local number = tonumber(weight)
    if weight == "bold" or weight == "bolder" or (number and number >= 600) then
      delta.bold = true
    elseif weight == "normal" or weight == "lighter" or (number and number < 600) then
      delta.bold = false
    end
  end

  local font_style = decls["font-style"]
  if font_style then
    font_style = trim(font_style)
    if font_style == "italic" or font_style == "oblique" then
      delta.italic = true
    elseif font_style == "normal" then
      delta.italic = false
    end
  end

  local decoration = decls["text-decoration"] or decls["text-decoration-line"]
  if decoration then
    if decoration:find("none", 1, true) then
      delta.underline = false
      delta.strike = false
    else
      if decoration:find("underline", 1, true) then delta.underline = true end
      if decoration:find("line-through", 1, true) then delta.strike = true end
    end
  end

  local display = decls.display
  if display and trim(display) == "none" then delta.skip = true end
  return delta
end

local function element_delta(tag, attrs, css_rules)
  local delta = declarations_to_delta(element_declarations(tag, attrs, css_rules))
  if tag == "font" and attrs.color then
    local color = parse_color(attrs.color)
    if color then delta.fg = color end
  end
  if attrs.bgcolor and (tag == "body" or tag == "table" or tag == "tr" or tag == "td" or tag == "th") then
    local color = parse_color(attrs.bgcolor)
    if color then delta.bg = color end
  end
  if tag == "body" and attrs.text then
    local color = parse_color(attrs.text)
    if color then delta.fg = color end
  end
  return delta
end

-- ------------------------------------------------------------- helpers ----

local void_tags = {
  area = true, base = true, br = true, col = true, embed = true, hr = true,
  img = true, input = true, link = true, meta = true, param = true,
  source = true, track = true, wbr = true,
}

local block_tags = {
  address = true, article = true, aside = true, center = true, details = true,
  dd = true, div = true, dl = true, dt = true, fieldset = true, figcaption = true,
  figure = true, footer = true, form = true, header = true, main = true,
  nav = true, section = true, summary = true,
}

local function ci_pattern(word)
  local out = {}
  for i = 1, #word do
    local ch = word:sub(i, i)
    out[#out + 1] = "[" .. ch:upper() .. ch:lower() .. "]"
  end
  return table.concat(out)
end

local function parse_attrs(text)
  local attrs = {}
  local i = 1
  local length = #text
  while i <= length do
    local _, spaces = text:find("^[%s/]+", i)
    if spaces then i = spaces + 1 end
    if i > length then break end

    local name, name_end = text:match("^([%w_:%-]+)()", i)
    if not name then
      i = i + 1
    else
      i = name_end
      local _, eqspaces = text:find("^%s*", i)
      i = eqspaces + 1
      local value = ""
      if text:sub(i, i) == "=" then
        i = i + 1
        local _, vspaces = text:find("^%s*", i)
        i = vspaces + 1
        local quote = text:sub(i, i)
        if quote == '"' or quote == "'" then
          local close = text:find(quote, i + 1, true)
          if close then
            value = text:sub(i + 1, close - 1)
            i = close + 1
          else
            value = text:sub(i + 1)
            i = length + 1
          end
        else
          local _, value_end = text:match("^([^%s>]*)()", i)
          value = text:sub(i, value_end - 1)
          i = value_end
        end
      end
      attrs[name:lower()] = value
    end
  end
  return attrs
end

local function class_language(attrs)
  local class = attrs.class or ""
  local language = class:match("language%-([%w_+%-]+)") or class:match("lang%-([%w_+%-]+)")
  if language then return language:lower() end
  return ""
end

local function preprocess(html)
  html = html:gsub("<!%-%-.-%-%->", "")

  local css = {}
  local style_open = "<" .. ci_pattern("style") .. "([^>]*)>"
  local style_close = "</" .. ci_pattern("style") .. "%s*>"
  html = html:gsub(style_open .. "(.-)" .. style_close, function(_, body)
    css[#css + 1] = body
    return ""
  end)

  local script_open = "<" .. ci_pattern("script") .. "([^>]*)>"
  local script_close = "</" .. ci_pattern("script") .. "%s*>"
  html = html:gsub(script_open .. "(.-)" .. script_close, "")

  local head_open = "<" .. ci_pattern("head") .. "([^>]*)>"
  local head_close = "</" .. ci_pattern("head") .. "%s*>"
  html = html:gsub(head_open .. "(.-)" .. head_close, "")

  html = html:gsub("<![Dd][Oo][Cc][Tt][Yy][Pp][Ee][^>]*>", "")
  html = html:gsub("<%?.-%?>", "")
  return html, table.concat(css, "\n")
end

-- ------------------------------------------------------------- parser ----

local function build_lines(html, css_rules)
  local lines = {}
  local cur = nil
  local pending = { style = "normal", indent = 0, fence = 0, lang = "" }
  local pending_marker = nil
  local ctx = {
    bold = false, italic = false, underline = false, strike = false,
    code = false, link = false, href = nil, fg = nil, bg = nil, skip = false,
  }
  local ctx_stack = {}
  local list_stack = {}
  local quote = 0
  local pre = 0
  local pre_fence = nil
  local table_state = nil

  local function append_marker()
    if pending_marker and cur and #cur.segments == 0 then
      cur.segments[#cur.segments + 1] = pending_marker
      pending_marker = nil
    end
  end

  local function ensure_line()
    if not cur then
      local style = pending.style
      local indent = pending.indent
      if style == "normal" and quote > 0 then
        style = "quote"
        indent = quote
      end
      cur = {
        style = style,
        indent = indent,
        fence = pending.fence,
        lang = pending.lang,
        segments = {},
      }
      append_marker()
    end
    return cur
  end

  local function flush(keep_style)
    if cur then
      if #cur.segments > 0 then lines[#lines + 1] = cur end
      local style, indent, fence = cur.style, cur.indent, cur.fence
      cur = nil
      if keep_style then
        pending = { style = style, indent = indent, fence = fence, lang = "" }
      else
        pending = { style = "normal", indent = 0, fence = 0, lang = "" }
      end
    elseif not keep_style then
      pending = { style = "normal", indent = 0, fence = 0, lang = "" }
    end
  end

  local function blank()
    flush(false)
    if #lines > 0 and lines[#lines].style ~= "empty" then
      lines[#lines + 1] = { style = "empty", indent = 0, fence = 0, lang = "", segments = {} }
    end
  end

  local function start_line(style, indent, fence, lang)
    flush(false)
    pending = {
      style = style or "normal",
      indent = indent or 0,
      fence = fence or 0,
      lang = lang or "",
    }
  end

  local function push_ctx(tag, delta)
    ctx_stack[#ctx_stack + 1] = { tag = tag, prev = ctx }
    local next_ctx = {}
    for key, value in pairs(ctx) do next_ctx[key] = value end
    for key, value in pairs(delta) do next_ctx[key] = value end
    ctx = next_ctx
  end

  local function pop_ctx(tag)
    for i = #ctx_stack, 1, -1 do
      if ctx_stack[i].tag == tag then
        ctx = ctx_stack[i].prev
        for j = #ctx_stack, i, -1 do ctx_stack[j] = nil end
        return
      end
    end
  end

  local function current_base_style()
    if ctx.code then return "code" end
    if ctx.link then return "link" end
    return "normal"
  end

  local function add_segment(text)
    if text == "" or ctx.skip then return end
    local line = ensure_line()
    local base = current_base_style()
    local underline = ctx.underline or ctx.link or false
    local last = line.segments[#line.segments]
    if last and last.style == base and last.bold == ctx.bold and last.italic == ctx.italic
        and last.underline == underline and last.strike == ctx.strike
        and last.fg == ctx.fg and last.bg == ctx.bg and last.hint == ctx.href then
      last.text = last.text .. text
    else
      line.segments[#line.segments + 1] = {
        text = text,
        style = base,
        bold = ctx.bold,
        italic = ctx.italic,
        underline = underline,
        strike = ctx.strike,
        fg = ctx.fg,
        bg = ctx.bg,
        hint = ctx.href,
      }
    end
  end

  local function append_text(raw)
    if raw == "" or ctx.skip then return end
    local text = decode_entities(raw)
    text = text:gsub("[\r\n\t ]+", " ")
    if text:sub(1, 1) == " " then
      if not cur or #cur.segments == 0 then
        text = text:sub(2)
      else
        local last = cur.segments[#cur.segments]
        if last.text:sub(-1) == " " then text = text:sub(2) end
      end
    end
    if text ~= "" then add_segment(text) end
  end

  local function append_pre(raw)
    if raw == "" or ctx.skip then return end
    local text = decode_entities(raw)
    text = text:gsub("\r\n", "\n"):gsub("\r", "\n"):gsub("\t", "    ")
    local start = 1
    while true do
      local newline = text:find("\n", start, true)
      local part = newline and text:sub(start, newline - 1) or text:sub(start)
      if part ~= "" then
        local line = ensure_line()
        line.segments[#line.segments + 1] = { text = part, style = "normal" }
      end
      if not newline then break end
      flush(true)
      start = newline + 1
    end
  end

  local function close_cell()
    if not table_state or not table_state.cell then return end
    if table_state.row then
      table_state.row.cells[#table_state.row.cells + 1] = table_state.cell
    end
    table_state.cell = nil
  end

  local function close_row()
    close_cell()
    if table_state and table_state.row then
      table_state.rows[#table_state.rows + 1] = table_state.row
      table_state.row = nil
    end
  end

  local function start_row()
    close_row()
    table_state.row = { cells = {} }
  end

  local function start_cell(name, attrs)
    if not table_state.row then table_state.row = { cells = {} } end
    close_cell()
    local decls = element_declarations(name, attrs, css_rules)
    local align = attrs.align or decls["text-align"]
    if align then align = trim(align):lower() end
    local colspan = tonumber(attrs.colspan)
    if not colspan or colspan < 1 or colspan > 20 then colspan = 1 end
    table_state.cell = {
      text = "",
      header = (name == "th"),
      align = align,
      colspan = math.floor(colspan),
    }
  end

  local function append_cell(raw)
    if raw == "" or not table_state or not table_state.cell then return end
    local text = decode_entities(raw):gsub("[\r\n\t ]+", " ")
    local cell = table_state.cell
    if cell.text == "" then text = text:gsub("^ ", "") end
    if cell.text:sub(-1) == " " and text:sub(1, 1) == " " then text = text:sub(2) end
    cell.text = cell.text .. text
  end

  local function emit_table(state)
    if #state.rows == 0 then return end

    local cols = 0
    for _, row in ipairs(state.rows) do
      local count = 0
      for _, cell in ipairs(row.cells) do count = count + cell.colspan end
      if count > cols then cols = count end
    end
    if cols == 0 then return end

    local aligns = {}
    for _, row in ipairs(state.rows) do
      local column = 1
      for _, cell in ipairs(row.cells) do
        if not aligns[column] and cell.align then aligns[column] = cell.align end
        column = column + cell.colspan
      end
    end

    local header_row = nil
    if state.rows[1] then
      for _, cell in ipairs(state.rows[1].cells) do
        if cell.header then header_row = 1 break end
      end
    end

    for index, row in ipairs(state.rows) do
      local parts = {}
      local column = 1
      for _, cell in ipairs(row.cells) do
        local text = cell.text:gsub("|", "¦")
        parts[column] = trim(text)
        for _ = 2, cell.colspan do
          column = column + 1
          parts[column] = ""
        end
        column = column + 1
      end
      for pad = #parts + 1, cols do parts[pad] = "" end

      lines[#lines + 1] = {
        style = "table", indent = 0, fence = 0, lang = "",
        segments = { { text = "| " .. table.concat(parts, " | ") .. " |", style = "normal" } },
      }

      if header_row == index then
        local specs = {}
        for column_index = 1, cols do
          local align = aligns[column_index]
          if align == "center" then
            specs[column_index] = ":---:"
          elseif align == "right" then
            specs[column_index] = "---:"
          else
            specs[column_index] = "---"
          end
        end
        lines[#lines + 1] = {
          style = "table", indent = 0, fence = 0, lang = "",
          segments = { { text = "| " .. table.concat(specs, " | ") .. " |", style = "normal" } },
        }
      end
    end
  end

  local function begin_table()
    flush(false)
    if #lines == 0 or lines[#lines].style ~= "empty" then
      lines[#lines + 1] = { style = "empty", indent = 0, fence = 0, lang = "", segments = {} }
    end
    table_state = { rows = {}, row = nil, cell = nil }
  end

  local function end_table()
    close_row()
    emit_table(table_state)
    table_state = nil
    pop_ctx("table")
    blank()
  end

  local function on_start(name, attrs)
    if name == "pre" then
      flush(false)
      pre = pre + 1
      lines[#lines + 1] = {
        style = "code", indent = 0, fence = 1, lang = class_language(attrs),
        segments = {},
      }
      pre_fence = #lines
      pending = { style = "code", indent = 0, fence = 0, lang = "" }
      push_ctx(name, {})
      return
    end

    if pre > 0 then
      if name == "code" then
        local language = class_language(attrs)
        if language ~= "" and pre_fence and lines[pre_fence].lang == "" then
          lines[pre_fence].lang = language
        end
      elseif name == "br" then
        flush(true)
      end
      return
    end

    if table_state then
      if name == "tr" then
        start_row()
      elseif name == "td" or name == "th" then
        start_cell(name, attrs)
      elseif name == "br" then
        append_cell(" ")
      elseif name == "img" then
        append_cell(attrs.alt or "")
      end
      return
    end

    if name == "table" then
      begin_table()
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    local heading = name:match("^h([1-6])$")
    if heading then
      blank()
      start_line("h" .. heading)
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    if name == "p" then
      start_line("normal")
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    if name == "blockquote" then
      flush(false)
      quote = quote + 1
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    if name == "ul" or name == "ol" then
      flush(false)
      local counter = 0
      if name == "ol" then
        local start = tonumber(attrs.start)
        if start then counter = start - 1 end
      end
      list_stack[#list_stack + 1] = { ordered = (name == "ol"), counter = counter }
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    if name == "li" then
      flush(false)
      local depth = #list_stack
      if depth < 1 then depth = 1 end
      if depth > 8 then depth = 8 end
      local list = list_stack[#list_stack]
      local marker
      if list and list.ordered then
        list.counter = list.counter + 1
        marker = string.rep("  ", depth - 1) .. list.counter .. ". "
      else
        marker = string.rep("  ", depth - 1) .. "• "
      end
      start_line("normal")
      pending_marker = { text = marker, style = "marker" }
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    if name == "hr" then
      flush(false)
      lines[#lines + 1] = { style = "hr", indent = 0, fence = 0, lang = "", segments = {} }
      blank()
      return
    end

    if name == "br" then
      flush(true)
      return
    end

    if name == "img" then
      if not ctx.skip then
        local alt = decode_entities(attrs.alt or "")
        local label = alt ~= "" and ("[" .. alt .. "]") or "[image]"
        local line = ensure_line()
        line.segments[#line.segments + 1] = {
          text = label,
          style = "image",
          bold = false, italic = false, underline = false, strike = false,
          fg = ctx.fg, bg = ctx.bg, hint = attrs.src,
        }
      end
      return
    end

    if block_tags[name] then
      start_line("normal", name == "dd" and 2 or 0)
      push_ctx(name, element_delta(name, attrs, css_rules))
      return
    end

    local delta = element_delta(name, attrs, css_rules)
    if name == "b" or name == "strong" then
      delta.bold = true
    elseif name == "i" or name == "em" then
      delta.italic = true
    elseif name == "u" or name == "ins" then
      delta.underline = true
    elseif name == "s" or name == "del" or name == "strike" then
      delta.strike = true
    elseif name == "code" or name == "kbd" or name == "samp" or name == "tt" or name == "var" then
      delta.code = true
    elseif name == "mark" then
      delta.bg = 0xFFFF00
      delta.fg = 0x000000
    elseif name == "a" then
      local href = attrs.href
      if href and href ~= "" then
        delta.link = true
        delta.href = href
      end
    end

    push_ctx(name, delta)
  end

  local function on_end(name)
    if name == "pre" and pre > 0 then
      flush(true)
      lines[#lines + 1] = { style = "code", indent = 0, fence = 2, lang = "", segments = {} }
      pending = { style = "normal", indent = 0, fence = 0, lang = "" }
      pre = pre - 1
      if pre == 0 then pre_fence = nil end
      pop_ctx("pre")
      return
    end

    if pre > 0 then return end

    if table_state then
      if name == "td" or name == "th" then
        close_cell()
      elseif name == "tr" then
        close_row()
      elseif name == "table" then
        end_table()
      end
      return
    end

    if name == "p" or name:match("^h[1-6]$") then
      flush(false)
      pop_ctx(name)
      blank()
      return
    end

    if name == "li" then
      flush(false)
      pending_marker = nil
      pop_ctx(name)
      return
    end

    if name == "ul" or name == "ol" then
      flush(false)
      list_stack[#list_stack] = nil
      pop_ctx(name)
      return
    end

    if name == "blockquote" then
      flush(false)
      if quote > 0 then quote = quote - 1 end
      pop_ctx(name)
      blank()
      return
    end

    if block_tags[name] then
      flush(false)
      pop_ctx(name)
      return
    end

    pop_ctx(name)
  end

  local function on_text(raw)
    if raw == "" then return end
    if pre > 0 then
      append_pre(raw)
    elseif table_state then
      append_cell(raw)
    else
      append_text(raw)
    end
  end

  local function find_tag_end(text, start)
    local i = start + 1
    local quote = nil
    local length = #text
    while i <= length do
      local ch = text:sub(i, i)
      if quote then
        if ch == quote then quote = nil end
      elseif ch == '"' or ch == "'" then
        quote = ch
      elseif ch == ">" then
        return i
      end
      i = i + 1
    end
    return nil
  end

  local position = 1
  local length = #html
  while position <= length do
    local tag_start = html:find("<", position, true)
    if not tag_start then
      on_text(html:sub(position))
      break
    end
    if tag_start > position then
      on_text(html:sub(position, tag_start - 1))
    end

    local tag_end = find_tag_end(html, tag_start)
    if not tag_end then
      on_text(html:sub(tag_start))
      break
    end

    local raw = html:sub(tag_start + 1, tag_end - 1)
    if raw:sub(1, 3) == "!--" then
      local close = html:find("-->", tag_start + 4, true)
      if close then tag_end = close + 2 end
    elseif raw:sub(1, 1) ~= "!" and raw:sub(1, 1) ~= "?" then
      local closing = false
      local body = raw
      if body:sub(1, 1) == "/" then
        closing = true
        body = body:sub(2)
      end
      local name, rest = body:match("^%s*([%w_:%-]+)(.*)$")
      if name then
        name = name:lower()
        if closing then
          on_end(name)
        else
          local attrs = parse_attrs(rest)
          on_start(name, attrs)
          if not void_tags[name] then
            -- push happens in on_start via push_ctx
          end
        end
      end
    end

    position = tag_end + 1
  end

  flush(false)
  return lines
end

function html_to_lines(html)
  html = html or ""
  local cleaned, css_text = preprocess(html)
  local css_rules = parse_css(css_text)
  local raw_lines = build_lines(cleaned, css_rules)

  local result = {}
  for index, line in ipairs(raw_lines) do
    if line.style == "empty" then
      local next_is_table = raw_lines[index + 1] and raw_lines[index + 1].style == "table"
      if #result > 0 or next_is_table then
        if #result == 0 or result[#result].style ~= "empty" then
          result[#result + 1] = line
        end
      end
    else
      result[#result + 1] = line
    end
  end
  while #result > 0 and result[#result].style == "empty" do
    result[#result] = nil
  end
  return result
end
