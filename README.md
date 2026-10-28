# FAR Manager Plugins

[![Build](https://github.com/darvik80/far-manager-plugins/actions/workflows/build-release.yml/badge.svg)](https://github.com/darvik80/far-manager-plugins/actions/workflows/build-release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Коллекция плагинов для **FAR Manager 3.0** — файловый менеджер для Windows.

Пользовательское руководство со схемами экранов: [docs/user-guide.md](docs/user-guide.md).

---

## 📦 Плагины

### 📧 Email Viewer

IMAP-клиент для просмотра электронной почты прямо в FAR Manager.

**Возможности:**
- Подключение к IMAP-серверам с SSL/TLS шифрованием
- Просмотр списка папок (INBOX, Sent, Drafts и др.)
- Просмотр списка писем с темами
- Чтение писем с автоматической конвертацией HTML в текст
- Поддержка нескольких учётных записей
- Сохранение настроек между сессиями

**Использование:**
- `Alt+F1` / `Alt+F2` → Email Viewer. Если цифра ещё не назначена, плагин ставит `5`
- `F11` → Email Viewer
- Командная строка FAR: `email:`

**Учётные записи** (`Alt+Shift+F9`):
- `F4` — изменить, `Shift+F4` — добавить, `Esc` — выход
- Host, порт (`993` с SSL или `143` без), логин, пароль, SSL

---

###  Markdown Viewer

Просмотр Markdown-файлов с рендерингом в диалоге FAR.

**Возможности:**
- Рендеринг Markdown в текстовый вид
- Поддержка заголовков, списков, кода, ссылок
- Навигация клавишами (Esc — выход)

**Использование:**
- `F3` на `.md`, `.markdown`, `.mdown`, `.mkd` в обычной панели и в панели SSH
- `F11` → Markdown Viewer
- Командная строка FAR: `mdview:`

---

### Pretty Viewer

Подсветка JSON, YAML и XML.

**Возможности:**
- Разбор `.json`, `.yaml`, `.yml`, `.xml`
- Темы оформления, поиск по тексту

**Использование:**
- `F3` на таком файле в обычной панели и в панели SSH
- `F11` → Pretty Viewer
- В просмотре: `F2` — исходный текст, `F7` — поиск, `F8` — тема, `Esc` — выход
- Командная строка FAR: `pretty:`

---

### 🔐 SSH Client

Панель SFTP: список серверов и каталоги на удалённой машине.

**Возможности:**
- Серверы из своих настроек и из `~/.ssh/config`
- Переход по каталогам, копирование, переименование, создание папок
- `F3` на Markdown, JSON, YAML и XML открывает соответствующий просмотрщик

**Использование:**
- `Alt+F1` / `Alt+F2` → SSH. Если цифра ещё не назначена, плагин ставит `6`
- `F11` → SSH
- Командная строка FAR: `ssh:`
- На списке серверов: `F3` — карточка хоста, `F4` — изменить, `F7` или `Shift+F4` — новый, `F8` — удалить. Хосты из `tsh` и `~/.ssh/config` тоже открываются: у Teleport имя/адрес/порт только смотреть, логин и ключ можно сохранить поверх
- `F2` или `Shift+Enter` — обычный SSH-терминал в отдельном окне (`tsh ssh` для Teleport, `ssh.exe` для остальных). Enter по-прежнему открывает SFTP
- Если установлен `tsh`: `F6` — `tsh login --mfa-mode=otp` в отдельном окне (пароль и OTP, либо браузер/SSO). Ноды из `tsh ls` появляются в списке. SFTP — `ssh.exe` через `tsh proxy ssh` (сертификат Teleport), запасной путь — libssh2 по тому же proxy
- SFTP открывается тем же `ssh.exe`. Если в карточке указан ключ (файл или vault) — только он: `-i` + `IdentitiesOnly=yes` (агент может подписать этот ключ, если он там лежит). Иначе — агент и `~/.ssh/config`, как в консоли
- В каталоге: `F5` — копировать (полоса прогресса), `F6` — переименовать, `F7` — папка, `F8` — удалить
- Поля `Ключ vault` / `Пароль vault` — имена записей Secret Vault. Если локальный vault молчит, SSH берёт пароль/ключ из NoteKeeper vault (тот же URL+PAT). Если поле пустое — ищет запись по **точному** хосту (не подстроке)
- Открытый пароль из старого `sessions.ini` при наличии хранилища переносится туда

---

### Secret Vault

Зашифрованное хранилище паролей и OpenSSH-ключей.

**Возможности:**
- Файл `%APPDATA%\Far Manager\SecretVault\vault.bin` защищён Windows DPAPI текущей учётки
- Каталоги `Пароли` и `Ключи`
- Создание ключей ed25519 и RSA 3072 через `ssh-keygen`, импорт OpenSSH-файлов
- Копирование секрета в буфер (`F5` / `Ctrl+C`), очистка через 30 секунд
- Экспорт/импорт файла с паролем (AES-256-GCM)
- Синк с [NoteKeeper](https://github.com/darvik80/note-keeper): HTTPS, PAT в DPAPI (`NoteKeeper\settings.ini`), [scope `vault`](https://github.com/darvik80/note-keeper#what-it-does)
- SSH берёт пароль и ключ по имени записи через `Plugin.SyncCall` (только SSH Client); если плагин не ответил — NoteKeeper REST `/api/v1/vault`

**Использование:**
- `Alt+F1` / `Alt+F2` → Secret Vault. Если цифра ещё не назначена, плагин ставит `7`
- `F11` → Secret Vault
- Настройки: URL `https://…` и PAT. Связь проверяется при сохранении
- На корне и в каталоге: `F7` или `Shift+F4` — новая запись. На корне F7 создаёт пароль или ключ в зависимости от строки под курсором
- В каталоге: `F4` — изменить, `F5` — секрет в буфер, `F3` — логин или публичный ключ, `F8` — удалить
- Ключи с диска: встань на `id_ed25519` / `id_rsa` в другой панели и `F5` в `Ключи` (`.pub` не нужен)
- `Alt+F5` — экспорт, `Alt+F6` — импорт
- `Shift+Alt+F9` — синк с NoteKeeper (`Alt+F9` остаётся размер окна FAR). Пустой локальный vault подтягивается при открытии; если записи уже есть, первый merge — только Shift+Alt+F9. Более новая локальная revision не затирается

---

### NoteKeeper

Панель notes и todos с сервера [NoteKeeper](https://github.com/darvik80/note-keeper).

**Возможности:**
- Каталоги `Notes` и `Todos`
- Редактор FAR (`F4`) пишет Markdown обратно через REST
- Todo: `F5` / Space — `toggle-complete`
- PAT: Windows DPAPI в `%APPDATA%\Far Manager\NoteKeeper\settings.ini` (URL можно дублировать в Secret Vault `sync.ini`, токен туда больше не пишется)

**Использование:**
- `Alt+F1` / `Alt+F2` → NoteKeeper. Если цифра ещё не назначена, плагин ставит `8`
- `F11` → NoteKeeper
- Командная строка FAR: `nk:`
- Настройки плагина: HTTPS URL (`https://note.example:8443`) и PAT со scopes `notes`, `todos` (SSH vault — ещё `vault`). `http://` только для localhost
- `F3` — просмотр, `F4` — правка, `F7` / `Shift+F4` — создать, `F8` — удалить, `Alt+F9` — обновить
- В Notes/Todos с другой панели (`F5`) копируются только `.md` и `.txt`

---

### 🌐 HTML Viewer

Просмотр HTML-файлов в нативном просмотрщике с подсветкой: заголовки, списки, таблицы,
цитаты, код и ссылки. Темы те же 16 палитр, что у Markdown Viewer (`F8` / `Shift+F8`).
Разбор HTML и CSS написан на Lua и работает внутри обёртки LuaFAR (`luaplug`), а
отрисовка цветных строк — в общем нативном хосте: LuaFAR не умеет заполнять
`DI_USERCONTROL` `VBuf`. Плагин виден в `F11`, имеет собственный GUID и префикс
командной строки.

**Возможности:**
- `F3` на `.html`, `.htm` и `.xhtml` — просмотр с темами и стилями
- 16 тем, `F8` / `Shift+F8` — следующая / предыдущая (общая палитра с Markdown Viewer)
- `F7` / `Shift+F7` — поиск, `Ctrl+↑` / `Ctrl+↓` — переход по заголовкам
- Базовые стили: заголовки, `b/strong`, `i/em`, `u/ins`, `s/del`, inline `code`, `pre`,
  ссылки, изображения (`alt`/`src`), списки `ul/ol`, цитаты, `hr`, таблицы
- Inline CSS и простые правила `<style>`: тег, `.class`, `#id`;
  `color`, `background-color`, `font-weight`, `font-style`, `text-decoration`, `display:none`
- CSS-цвета применяются только если читаемы на текущей теме; заголовки всегда
  сохраняют цвет темы
- Поддержка UTF-8, UTF-16 LE/BE (BOM) и ANSI, декодирование HTML-entities
- Конвертер — обычный Lua-скрипт (`plugins/html-viewer/lua/html_convert.lua`)

**Требования:**
- FAR Manager 3.0 со штатными `lua51.dll` и `luafar3.dll` в каталоге FAR (входят в поставку)

**Установка:**
```bat
install-plugin.bat HtmlViewer
```

**Использование:**
- `F3` на HTML-файле в обычной панели
- `F11` → HTML Viewer
- Командная строка FAR: `htmlview: <файл>`
- В просмотрщике: `F8` — тема, `F7` — поиск, `F3` или `Esc` — закрыть

---

## 🧩 Новый Lua-плагин

Lua-плагины используют общий host `third_party/luafar` и функцию `add_luafar_plugin()` из `cmake/LuaFarPlugin.cmake`; отдельный C++ для каждого плагина не нужен.

1. Создайте `plugins/<Name>/`:
   - `CMakeLists.txt`:
     ```cmake
     add_luafar_plugin(<Name>
         LUA_FILES lua/plugin.lua
     )
     ```
   - `_globalinfo.lua` — уникальный GUID, `Title`, `Version`, `MinFarVersion`.
   - `lua/plugin.lua` — обработчики `export.GetPluginInfo`, `export.Open`, `export.ProcessConsoleInput` и т.д.
2. Добавьте `add_subdirectory(plugins/<Name>)` в корневой `CMakeLists.txt`.
3. Соберите `build.bat` и установите `install-plugin.bat <Name>`.

При сборке создаются `<Name>.dll`, `<Name>.lua` (склейка `LUA_FILES` по порядку) и `_globalinfo.lua`; установщик копирует их в `Plugins\<Name>`. У каждого Lua-плагина своя папка, GUID и скрипт, поэтому они не конфликтуют.

Для цветного текста в стиле Markdown Viewer хост предоставляет Lua-глобал
`luaplug_ui.ShowStyledText(title, lines)`: Lua описывает строки и сегменты
(`style`, `indent`, `fence`, `lang`, `segments` с `text`, `style`, `bold`, `italic`,
`underline`, `strike`, `fg`, `bg`, `hint`), а нативный хост рисует их, применяет
темы (`F8`), поиск и навигацию. Палитра тем — общий заголовок
`sdk/render/themes.hpp`. Подробный формат — в `.cursor/skills/far-plugin/SKILL.md`.

---

## 🚀 Быстрый старт

### Установка готовых сборок

Скачайте последний релиз со страницы [Releases](https://github.com/darvik80/far-manager-plugins/releases) и запустите `install.bat`.

### Сборка из исходников

**Требования:**
- CMake 3.20+ (в GitHub Actions — CMake 4.2 и Visual Studio 2026)
- MSVC
- OpenSSL с заголовками (Email Viewer и SSH)

**Сборка:**
```bat
build.bat
```

**Установка:**
```bat
install-all.bat
```

**Удаление:**
```bat
uninstall-all.bat
```

---

## 📁 Структура проекта

```
far-manager-plugins/
├── plugins/
│   ├── email-viewer/      # IMAP клиент
│   ├── markdown-viewer/   # Markdown просмотрщик
│   ├── pretty-viewer/     # JSON, YAML, XML
│   ├── html-viewer/       # HTML просмотрщик (LuaFAR + Lua)
│   ├── secret-vault/      # пароли и OpenSSH-ключи
│   ├── note-keeper/       # notes/todos + PAT
│   └── ssh-client/        # SFTP панель
├── common/
│   └── notekeeper/        # REST/MCP клиент, DPAPI PAT, HTTPS
├── third_party/
│   └── luafar/            # общий host Lua-плагинов (luaplug)
├── cmake/
│   └── LuaFarPlugin.cmake # add_luafar_plugin()
├── sdk/                   # FAR Manager API headers (form.hpp — общий вид диалогов)
├── docs/                  # user-guide.md и схемы экранов
├── .github/workflows/     # CI/CD (GitHub Actions)
├── CMakeLists.txt         # Основной CMake файл
├── build.bat              # Сборка всех плагинов
├── install-all.bat        # Установка всех плагинов
└── uninstall-all.bat      # Удаление всех плагинов
```

---

## Безопасность

- **PAT** шифруется DPAPI текущей учётки Windows. Старый plaintext в `settings.ini` / `sync.ini` при первом запуске переносится и стирается из `sync.ini`.
- **HTTPS обязателен** для NoteKeeper/vault. Исключение: `http://127.0.0.1`, `localhost`, `::1`.
- **SSH host match** — точное имя хоста (схема, порт и путь отбрасываются). Подстрока `evil.com` ⊂ `notevil.com` больше не срабатывает.
- **Plugin.SyncCall** Secret Vault (`lookup` / `list` / `store`) принимает GUID только SSH Client.
- **Временные ключи и F3-заметки** пишутся в `%APPDATA%\Far Manager\FarVault` / `FarNoteKeeper` с DACL «только текущий пользователь» и удаляются при закрытии панели / выходе из FAR.
- **Синк vault**: сервер не затирает локальную запись с такой же или большей `revision`. Первый pull в уже непустой локальный vault — `Shift+Alt+F9`.

IMAP-пароли Email Viewer по-прежнему в plaintext `%APPDATA%\Far Manager\EmailViewer.ini` — это отдельный плагин.

## 🛠️ Разработка

### Добавление нового плагина

1. Создайте директорию `plugins/<plugin-name>/`
2. Добавьте `CMakeLists.txt`
3. Реализуйте обязательные экспорты:
   - `SetStartupInfoW`
   - `GetGlobalInfoW`
   - `GetMinFarVersionW`
   - `GetPluginInfoW`
   - `OpenW`
   - `ExitFARW`
4. Добавьте плагин в корневой `CMakeLists.txt`
5. Добавьте в `install-all.bat`

### Важные правила FAR API

- DlgProc **обязан** вызывать `DefDlgProc` для необработанных сообщений
- Последний параметр DlgProc — `void*`, не `intptr_t`
- GUID должны быть `extern const` в header, определены в одном `.cpp`
- `PluginInfo.PluginConfig.Count` должен соответствовать реальному количеству элементов

---

## 📄 История изменений

См. [CHANGELOG.md](CHANGELOG.md).

---

## 📄 Лицензия

MIT License — см. [LICENSE](LICENSE) файл.

---

## 🤝 Вклад

Pull requests приветствуются! Для крупных изменений сначала откройте Issue для обсуждения.

---

##  Контакты

- Repository: https://github.com/darvik80/far-manager-plugins
- NoteKeeper: https://github.com/darvik80/note-keeper
- Author: darvik80
