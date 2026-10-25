# FAR Manager Plugins

Коллекция плагинов для FAR Manager 3.0.

## Плагины

### Email Viewer

IMAP-клиент для просмотра почты прямо в FAR Manager.

**Возможности:**
- Подключение к IMAP-серверам (SSL/TLS)
- Просмотр списка папок (INBOX и др.)
- Просмотр списка писем
- Чтение писем с конвертацией HTML в текст
- Поддержка нескольких аккаунтов

**Установка:**
```bat
install-all.bat
```

**Использование:**
- `Alt+F2` → Email Viewer (как NetBox)
- `F11` → Email Viewer
- Командная строка: `email:`

### Markdown Viewer

Просмотр Markdown-файлов в FAR Manager.

**Возможности:**
- Рендеринг Markdown в диалоге FAR
- Поддержка заголовков, списков, кода, ссылок
- Навигация клавишами

**Использование:**
- `F3` на `.md` файле (если ассоциирован)
- `F11` → Markdown Viewer

### SSH Client

SSH-клиент для подключения к удаленным серверам.

**Возможности:**
- Подключение по SSH
- Выполнение команд
- Просмотр вывода

## Сборка

### Требования
- CMake 3.20+
- MSVC (Visual Studio 2022)
- OpenSSL (для Email Viewer)

### Сборка всех плагинов
```bat
cd build-msvc
cmake ..
cmake --build . --config Release
```

### Установка
```bat
install-all.bat
```

### Удаление
```bat
uninstall-all.bat
```

## Структура проекта

```
far-manager-plugins/
├── plugins/
│   ├── email-viewer/      # IMAP клиент
│   ├── markdown-viewer/   # Markdown просмотрщик
│   └── ssh-client/        # SSH клиент
├── sdk/                   # FAR Manager SDK
├── CMakeLists.txt
├── install-all.bat
── uninstall-all.bat
```

## Лицензия

MIT
