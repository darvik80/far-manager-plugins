# История изменений

## 1.3.0

### Новое
- **NoteKeeper** — панель notes/todos: URL+PAT при первом открытии, MCP (`/mcp`) если REST 403, список не дергается на каждый refresh
- **SSH Client**: пароль и ключ из NoteKeeper vault, если Secret Vault не ответил; подбор пароля по хосту

### Безопасность
- PAT хранится через Windows DPAPI в `NoteKeeper\settings.ini`, дубль в `sync.ini` убран
- К NoteKeeper только HTTPS (`http://` — localhost)
- SSH подбирает пароль vault по точному хосту, не по подстроке
- `Plugin.SyncCall` lookup/store принимает только SSH Client
- Временные PEM и F3-файлы с ACL текущей учётки, хвосты чистятся при выходе
- Pull с NoteKeeper не перезаписывает более новую локальную запись; в непустой vault без прошлого синка — только по Shift+Alt+F9
- **Secret Vault**: синк с NoteKeeper на `Shift+Alt+F9`, чтобы не перехватывать `Alt+F9` FAR

---

## 1.2.0 (2026-10-26)

### Новое
- **Secret Vault** — новый плагин: зашифрованное хранилище паролей и OpenSSH-ключей (DPAPI, AES-256-GCM, экспорт/импорт, генерация ключей ed25519/RSA 3072)
- **HTML Viewer** — новый LuaFAR-плагин: просмотр HTML с подсветкой стилей, 16 тем, поиск, навигация по заголовкам
- **SSH Client**: поддержка Teleport (`tsh login`, `tsh proxy ssh`), интеграция с Secret Vault (пароли и ключи по имени записи), перенос открытых паролей из `sessions.ini` в хранилище
- **Email Viewer**: улучшенный HTML-конвертер, нормализация текста писем

### Инфраструктура
- Общий LuaFAR-хост (`third_party/luafar/`) и CMake-функция `add_luafar_plugin()` для Lua-плагинов
- CI/CD: добавлены HtmlViewer и SecretVault в сборку и установщик, поддержка Lua-файлов в пакете
- Удалён `lua-plugins/` и `install-lua-plugin.bat` — больше не нужны

### Исправления
- HTML Viewer: корректный перехват F3 на HTML-файлах (конфликт с остаточным LuaMacro-скриптом)
- HTML Viewer: `CurrentPanelFile` поддерживает абсолютные пути
- Markdown Viewer: удалены неиспользуемые файлы (`main_minimal.cpp`, `main_simple.cpp`)

---

## 1.1.0 (2026-10-19)

- Disk hotkeys (`Alt+F1`/`Alt+F2`) для быстрого запуска плагинов
- SSH Viewer: просмотр SSH-каталогов через SFTP-панель
- Email Viewer: редактирование учётных записей по F4

---

## 1.0.0 (2026-10-12)

- Первый релиз: Email Viewer, Markdown Viewer, Pretty Viewer, SSH Client
- IMAP-клиент с SSL/TLS, просмотр папок и писем
- Markdown-просмотрщик с рендерингом в диалоге FAR
- Подсветка JSON, YAML, XML
- SFTP-панель с поддержкой `~/.ssh/config`
