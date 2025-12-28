# Liftoff

<img src="data/icons/hicolor/scalable/apps/lv.martinsz.Liftoff.svg" width="128" height="128" align="left"/>

A simple and intuitive desktop launcher creator for Linux.

Easily create `.desktop` application launchers for your Linux desktop environment with Liftoff.

<br clear="left"/>

![Liftoff Screenshot](screenshot.png)

## Features

- 🎯 Easy-to-use interface for creating desktop entries
- 👁️ Live preview of the .desktop file as you type
- 🎨 Icon picker to select application icons
- ⚙️ Support for all standard desktop entry fields
- 💾 Automatically saves launchers to the correct location

## Installation

### From Flathub (recommended)

```bash
flatpak install flathub lv.martinsz.Liftoff
```

### Building from source

Requires:
- GTK 4
- libadwaita
- Meson
- Ninja

```bash
meson setup build
meson compile -C build
meson install -C build
```

## Usage

Simply launch Liftoff from your applications menu, fill in the details for your launcher, and click Create. The desktop file will be automatically created and installed.

## License

GPL-2.0-or-later

## Author

Martins Zeltins
