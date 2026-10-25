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
- `Alt+F2` → Email Viewer (меню смены дисков, как NetBox)
- `F11` → Email Viewer (меню плагинов)
- Командная строка FAR: `email:`

**Настройка:**
При первом запуске откроется диалог настройки:
- Host: imap.gmail.com (или другой IMAP-сервер)
- Port: 993 (SSL) или 143 (без SSL)
- Login: ваш email
- Password: пароль приложения
- SSL: включить/выключить

---

###  Markdown Viewer

Просмотр Markdown-файлов с рендерингом в диалоге FAR.

**Возможности:**
- Рендеринг Markdown в текстовый вид
- Поддержка заголовков, списков, кода, ссылок
- Навигация клавишами (Esc — выход)

**Использование:**
- `F11` → Markdown Viewer
- Выбор `.md` файла для просмотра

---

### 🔐 SSH Client

SSH-клиент для подключения к удалённым серверам.

**Возможности:**
- Подключение по SSH с аутентификацией
- Выполнение команд на удалённом сервере
- Просмотр вывода команд

**Использование:**
- `F11` → SSH Client
- Ввод хоста, логина, пароля
- Выполнение команд

---

## 🚀 Быстрый старт

### Установка готовых сборок

Скачайте последний релиз со страницы [Releases](https://github.com/darvik80/far-manager-plugins/releases) и запустите `install.bat`.

### Сборка из исходников

**Требования:**
- CMake 3.20+
- MSVC (Visual Studio 2019 или 2022)
- OpenSSL (для Email Viewer) — устанавливается через `choco install openssl`

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
│   └── ssh-client/        # SSH клиент
├── sdk/                   # FAR Manager API headers
├── .github/workflows/     # CI/CD (GitHub Actions)
├── CMakeLists.txt         # Основной CMake файл
├── build.bat              # Сборка всех плагинов
├── install-all.bat        # Установка всех плагинов
└── uninstall-all.bat      # Удаление всех плагинов
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
