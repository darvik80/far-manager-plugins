# FAR Manager Plugins

[![Build](https://github.com/darvik80/far-manager-plugins/actions/workflows/build-release.yml/badge.svg)](https://github.com/darvik80/far-manager-plugins/actions/workflows/build-release.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Коллекция плагинов для **FAR Manager 3.0** — файловый менеджер для Windows.

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
- На списке серверов: `F4` — изменить, `F7` или `Shift+F4` — новый, `F8` — удалить
- В каталоге: `F5` — копировать, `F6` — переименовать, `F7` — папка, `F8` — удалить

---

### 🌐 HTML Viewer (Lua)

Просмотр HTML-файлов через Lua-скрипт (без компиляции).

**Возможности:**
- Открытие `.html` и `.htm` по F3
- Конвертация HTML в plain text
- Поддержка UTF-8 и UTF-16
- Декодирование HTML-entities

**Установка:**
```bat
install-lua-plugin.bat html-viewer
```

**Использование:**
- `F3` на `.html` или `.htm` файле
- `Ctrl+Shift+H` — конвертировать HTML из буфера обмена в текст
- `F11` → HTML Viewer

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
│   ── ssh-client/        # SFTP панель
── lua-plugins/
│   └── html-viewer/       # HTML просмотрщик (Lua)
├── sdk/                   # FAR Manager API headers
├── .github/workflows/     # CI/CD (GitHub Actions)
├── CMakeLists.txt         # Основной CMake файл
├── build.bat              # Сборка всех C++ плагинов
├── install-all.bat        # Установка всех C++ плагинов
├── uninstall-all.bat      # Удаление всех C++ плагинов
└── install-lua-plugin.bat # Установка Lua плагинов
```

---

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

## 📄 Лицензия

MIT License — см. [LICENSE](LICENSE) файл.

---

## 🤝 Вклад

Pull requests приветствуются! Для крупных изменений сначала откройте Issue для обсуждения.

---

##  Контакты

- Repository: https://github.com/darvik80/far-manager-plugins
- Author: darvik80
