# HTML Viewer (Lua)

Просмотр HTML-файлов в FAR Manager через Lua-скрипт.

## Возможности

- Открытие `.html` и `.htm` файлов по F3
- Конвертация HTML в plain text
- Поддержка UTF-8 и UTF-16
- Декодирование HTML-entities
- Просмотр через FAR Viewer

## Установка

### Вариант 1: Автоматическая установка
```bat
install-lua-plugin.bat html-viewer
```

### Вариант 2: Ручная установка
Скопируйте `html-viewer.lua` в:
```
%APPDATA%\Far Manager\Profile\Macros\scripts\
```

## Использование

- **F3** на `.html` или `.htm` файле — открыть в просмотрщике
- **Ctrl+Shift+H** — конвертировать HTML из буфера обмена в текст
- **F11** → HTML Viewer — открыть файл по пути

## Требования

- FAR Manager 3.0+
- LuaMacro plugin (включён в стандартную поставку FAR)

## Отличия от C++ плагинов

| | Lua plugin | C++ plugin |
|---|---|---|
| Установка | Копирование .lua файла | Компиляция + копирование .dll |
| Зависимости | Только FAR + LuaMacro | MSVC, CMake, OpenSSL |
| Производительность | Ниже | Выше |
| Гибкость | Высокая (скриптовый) | Ограниченная |
