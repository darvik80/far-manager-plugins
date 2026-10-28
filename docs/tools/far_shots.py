#!/usr/bin/env python3
"""Draw FAR-style PNGs for docs/user-guide.md. Run from repo root:

    python docs/tools/far_shots.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 80, 25
CW, CH = 10, 18
PAL = [
    (0, 0, 0),
    (0, 0, 170),
    (0, 170, 0),
    (0, 170, 170),
    (170, 0, 0),
    (170, 0, 170),
    (170, 85, 0),
    (170, 170, 170),
    (85, 85, 85),
    (85, 85, 255),
    (85, 255, 85),
    (85, 255, 255),
    (255, 85, 85),
    (255, 85, 255),
    (255, 255, 85),
    (255, 255, 255),
]
KEYS = ("Help", "User", "View", "Edit", "Copy", "RenMov", "MkFold", "Delete", "ConfMn", "Quit")


def load_font() -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for path in (
        r"C:\Windows\Fonts\consola.ttf",
        r"C:\Windows\Fonts\lucon.ttf",
        r"C:\Windows\Fonts\cour.ttf",
    ):
        try:
            return ImageFont.truetype(path, 15)
        except OSError:
            continue
    return ImageFont.load_default()


FONT = load_font()


class Screen:
    def __init__(self) -> None:
        self.ch = [[" "] * COLS for _ in range(ROWS)]
        self.fg = [[11] * COLS for _ in range(ROWS)]
        self.bg = [[1] * COLS for _ in range(ROWS)]

    def fill(self, x: int, y: int, w: int, h: int, fg: int, bg: int, ch: str = " ") -> None:
        for row in range(y, min(ROWS, y + h)):
            for col in range(x, min(COLS, x + w)):
                self.ch[row][col] = ch
                self.fg[row][col] = fg
                self.bg[row][col] = bg

    def text(self, x: int, y: int, s: str, fg: int | None = None, bg: int | None = None) -> None:
        for i, c in enumerate(s):
            col = x + i
            if y < 0 or y >= ROWS or col < 0 or col >= COLS:
                continue
            self.ch[y][col] = c
            if fg is not None:
                self.fg[y][col] = fg
            if bg is not None:
                self.bg[y][col] = bg

    def dbox(self, x: int, y: int, w: int, h: int, title: str, fg: int, bg: int) -> None:
        self.fill(x, y, w, h, fg, bg)
        self.text(x, y, "╔" + "═" * (w - 2) + "╗", fg, bg)
        for row in range(1, h - 1):
            self.text(x, y + row, "║", fg, bg)
            self.text(x + w - 1, y + row, "║", fg, bg)
        self.text(x, y + h - 1, "╚" + "═" * (w - 2) + "╝", fg, bg)
        if title:
            label = f" {title} "
            tx = x + max(2, (w - len(label)) // 2)
            self.text(tx, y, label, fg, bg)

    def sep(self, x: int, y: int, w: int, fg: int, bg: int) -> None:
        self.text(x, y, "╟" + "─" * (w - 2) + "╢", fg, bg)

    def keybar(self) -> None:
        self.fill(0, 24, COLS, 1, 0, 7)
        x = 0
        for i, name in enumerate(KEYS, start=1):
            num = str(i)
            slot = 8
            self.text(x, 24, num, 11, 1)
            x += len(num)
            pad = slot - len(num)
            self.text(x, 24, name.ljust(pad)[:pad], 0, 7)
            x += pad

    def clock(self) -> None:
        self.text(67, 0, "28.10 10:27", 14, 1)

    def panel_frame(self, x: int, y: int, w: int, h: int, title: str) -> None:
        self.dbox(x, y, w, h, "", 11, 1)
        label = f" {title} "
        self.text(x + max(2, (w - len(label)) // 2), y, label, 14, 1)

    def render(self, path: Path) -> None:
        img = Image.new("RGB", (COLS * CW, ROWS * CH), PAL[1])
        draw = ImageDraw.Draw(img)
        for y in range(ROWS):
            for x in range(COLS):
                px, py = x * CW, y * CH
                draw.rectangle((px, py, px + CW - 1, py + CH - 1), fill=PAL[self.bg[y][x]])
                ch = self.ch[y][x]
                if ch != " ":
                    draw.text((px + 1, py), ch, fill=PAL[self.fg[y][x]], font=FONT)
        path.parent.mkdir(parents=True, exist_ok=True)
        img.save(path)
        print("wrote", path)


def desktop(title_left: str, title_right: str | None = None) -> Screen:
    s = Screen()
    s.fill(0, 0, COLS, ROWS, 11, 1)
    if title_right:
        s.panel_frame(0, 0, 40, 24, title_left)
        s.panel_frame(40, 0, 40, 24, title_right)
    else:
        s.panel_frame(0, 0, COLS, 24, title_left)
    s.clock()
    s.keybar()
    return s


def rows_files(s: Screen, x: int, y: int, w: int, files: list[tuple[str, str, bool]], selected: int) -> None:
    for i, (name, info, is_dir) in enumerate(files):
        line = (name[: w - 14].ljust(w - 14) + info.rjust(12))[:w]
        fg, bg = (0, 3) if i == selected else ((14, 1) if is_dir else (11, 1))
        s.text(x, y + i, line, fg, bg)


def dialog(s: Screen, w: int, h: int, title: str) -> tuple[int, int]:
    x = (COLS - w) // 2
    y = (22 - h) // 2 + 1
    s.fill(x + 2, y + 1, w, h, 0, 0)
    s.dbox(x, y, w, h, title, 0, 7)
    return x, y


def edit(s: Screen, x: int, y: int, w: int, value: str, password: bool = False) -> None:
    shown = ("•" * min(len(value), w)) if password else value[:w].ljust(w)
    s.text(x, y, shown, 15, 3)


def buttons(s: Screen, x: int, y: int, w: int, ok: str, cancel: str) -> None:
    label = f"{{ {ok} }}  [ {cancel} ]"
    s.text(x + max(2, (w - len(label)) // 2), y, label, 0, 7)


def shot_ssh_list(out: Path) -> None:
    s = desktop("SSH: servers")
    files = [
        ("..", "<UP>", True),
        ("prod.example", "22  deploy", False),
        ("sgp1.example", "tsh  root", False),
        ("build.example", "22  ci", False),
        ("db-01", "22  postgres", False),
    ]
    rows_files(s, 1, 1, 78, files, 2)
    s.text(1, 22, " Enter SFTP  F2 terminal  F4 edit  F6 tsh login  F7 new", 11, 1)
    s.render(out)


def shot_ssh_edit(out: Path) -> None:
    s = desktop("SSH: servers")
    rows_files(s, 1, 1, 78, [("prod.example", "22  deploy", False)], 0)
    x, y = dialog(s, 63, 15, "Сервер")
    s.text(x + 2, y + 2, "Имя:", 0, 7)
    edit(s, x + 12, y + 2, 48, "prod.example")
    s.text(x + 2, y + 4, "Адрес:", 0, 7)
    edit(s, x + 12, y + 4, 48, "prod.example")
    s.text(x + 2, y + 6, "Порт:", 0, 7)
    edit(s, x + 12, y + 6, 7, "22")
    s.text(x + 21, y + 6, "Логин:", 0, 7)
    edit(s, x + 29, y + 6, 31, "deploy")
    s.text(x + 2, y + 8, "Ключ:", 0, 7)
    edit(s, x + 12, y + 8, 48, "deploy-ed25519")
    s.text(x + 2, y + 10, "Прокси:", 0, 7)
    edit(s, x + 12, y + 10, 21, "")
    s.text(x + 35, y + 10, "Пароль:", 0, 7)
    edit(s, x + 44, y + 10, 16, "vault:prod", password=True)
    s.sep(x, y + 11, 63, 0, 7)
    buttons(s, x, y + 12, 63, "Сохранить", "Отмена")
    s.render(out)


def shot_ssh_copy(out: Path) -> None:
    s = desktop("deploy@prod.example:/var/www")
    rows_files(
        s,
        1,
        1,
        78,
        [("app.tgz", " 12.4M", False), ("public", "Folder", True), ("logs", "Folder", True)],
        0,
    )
    x, y = dialog(s, 63, 9, "Копирование")
    s.text(x + 2, y + 2, "app.tgz", 0, 7)
    inner = 59
    filled = inner * 57 // 100
    s.fill(x + 2, y + 3, filled, 1, 7, 0)
    s.fill(x + 2 + filled, y + 3, inner - filled, 1, 0, 7)
    pct = " 57% "
    s.text(x + 2 + (inner - len(pct)) // 2, y + 3, pct)
    s.text(x + 2, y + 4, "7.1 МБ / 12.4 МБ", 0, 7)
    s.sep(x, y + 5, 63, 0, 7)
    label = "[ Отмена ]"
    s.text(x + (63 - len(label)) // 2, y + 7, label, 15, 3)
    s.render(out)


def shot_ssh_sftp(out: Path) -> None:
    s = desktop("deploy@prod.example:/home/deploy")
    files = [
        ("..", "<UP>", True),
        ("apps", "Folder", True),
        ("backup", "Folder", True),
        (".ssh", "Folder", True),
        ("README.md", "  4.2K", False),
        ("compose.yml", "  1.1K", False),
    ]
    rows_files(s, 1, 1, 78, files, 4)
    s.text(1, 22, " F3 view  F5 copy  F6 rename  F7 mkdir  Esc servers", 11, 1)
    s.render(out)


def shot_vault_root(out: Path) -> None:
    s = desktop("Secret Vault")
    files = [
        ("Пароли", "Folder", True),
        ("Ключи", "Folder", True),
    ]
    rows_files(s, 1, 1, 78, files, 0)
    s.text(1, 22, " F7 new  Shift+F4 new  Shift+Alt+F9 sync  F4 edit", 11, 1)
    s.render(out)


def shot_vault_password(out: Path) -> None:
    s = desktop("Secret Vault\\Пароли")
    rows_files(s, 1, 1, 78, [("prod.example", "deploy", False)], 0)
    x, y = dialog(s, 63, 15, "Пароль")
    s.text(x + 2, y + 2, "Имя:", 0, 7)
    edit(s, x + 12, y + 2, 48, "prod.example")
    s.text(x + 2, y + 4, "Логин:", 0, 7)
    edit(s, x + 12, y + 4, 21, "deploy")
    s.text(x + 35, y + 4, "Хост:", 0, 7)
    edit(s, x + 42, y + 4, 18, "prod.example")
    s.text(x + 2, y + 6, "Пароль:", 0, 7)
    edit(s, x + 12, y + 6, 48, "secret", password=True)
    s.text(x + 2, y + 8, "Заметка:", 0, 7)
    edit(s, x + 12, y + 8, 48, "prod deploy")
    s.sep(x, y + 11, 63, 0, 7)
    buttons(s, x, y + 12, 63, "Сохранить", "Отмена")
    s.render(out)


def shot_nk_notes(out: Path) -> None:
    s = desktop("NoteKeeper\\Notes")
    files = [
        ("..", "<UP>", True),
        ("runbook.md", " 12.0K", False),
        ("oncall.md", "  3.4K", False),
        ("ideas.md", "  8.1K", False),
    ]
    rows_files(s, 1, 1, 78, files, 1)
    s.text(1, 22, " F3 view  F4 edit  F7 new  F8 delete  Alt+F9 refresh", 11, 1)
    s.render(out)


def shot_email_inbox(out: Path) -> None:
    s = desktop("Email: INBOX")
    files = [
        ("..", "<UP>", True),
        ("Alerts", "Folder", True),
        ("[12:01] weekly digest", "  18K", False),
        ("[11:40] deploy green", "   4K", False),
        ("[09:12] invoice 1042", "  22K", False),
    ]
    rows_files(s, 1, 1, 78, files, 2)
    s.text(1, 22, " Enter read  F4 accounts  Alt+Shift+F9 settings", 11, 1)
    s.render(out)


def shot_md_view(out: Path) -> None:
    s = Screen()
    s.fill(0, 0, COLS, ROWS, 15, 0)
    s.dbox(0, 0, COLS, 24, "README.md", 11, 0)
    s.text(2, 2, "SSH Client", 14, 0)
    s.text(2, 4, "SFTP-панель: список серверов и каталоги на машине.", 7, 0)
    s.text(2, 6, "Горячие клавиши", 14, 0)
    s.text(4, 8, "F2   терминал (tsh ssh / ssh.exe)", 11, 0)
    s.text(4, 9, "F3   просмотр md / json / yaml / xml", 11, 0)
    s.text(4, 10, "F5   копировать, Esc — отмена", 11, 0)
    s.text(4, 11, "F6   tsh login, если сертификат истёк", 11, 0)
    s.text(2, 13, "Ключ из Secret Vault указывается именем записи.", 7, 0)
    s.keybar()
    s.render(out)


def main() -> None:
    out = Path(__file__).resolve().parents[1] / "images"
    shot_ssh_list(out / "ssh-list.png")
    shot_ssh_edit(out / "ssh-edit.png")
    shot_ssh_copy(out / "ssh-copy.png")
    shot_ssh_sftp(out / "ssh-sftp.png")
    shot_vault_root(out / "vault-root.png")
    shot_vault_password(out / "vault-password.png")
    shot_nk_notes(out / "nk-notes.png")
    shot_email_inbox(out / "email-inbox.png")
    shot_md_view(out / "md-view.png")


if __name__ == "__main__":
    main()
