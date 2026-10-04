# Third-party material

## Qingjian bilingual glossaries

- Project: 青简 Qingjian, https://github.com/qingjian-team/qingjian
- Authors: Qingjian contributors / qingjian-team
- Source files: `assets/glossary/glossary-en.tsv` and `assets/glossary/glossary-zh.tsv`.
- Reference revision: `c08ae57cb88b6a4a46f4a5e9c1d6d11c5e69222e` (the repository revision inspected for this work on 2026-10-03).
- Source documentation: https://github.com/qingjian-team/qingjian/blob/main/assets/glossary/README.md
- Stated license: GPL-3.0-or-later. The full GPL v3 text is included in `data/LICENSE.GPL-3.0.txt`. This standalone data file retains GPL rights; the project's non-commercial restriction does not apply to it.
- The source document says the senses were generated offline using an LLM. Both files were imported from a cloned checkout and filtered by `tools/import-qingjian.ps1`; the derived files are `data/glossary-en.tsv` and `data/glossary-zh.tsv`. Empty, duplicate and untranslated senses were removed; no manual semantic validation of the full dataset is claimed. Original revision, SHA-256 values, categories, counts and rules are documented in `data/LIBRARY.md`. Derived data retains GPL-3.0-or-later rights.
- The source glossary README is preserved as `data/QINGJIAN-GLOSSARY-README.md`.
- No Qingjian program source, branding, icon or input engine is included in this implementation. The program independently reads an external TSV file; this glossary is not compiled into the executable.

## Platform and build tools

Windows APIs and system DLLs provide the application UI and accessibility interfaces. MinGW-w64 GCC was used to build the Windows binaries, with its standard runtime statically linked; the output imports only Windows system libraries and the Windows Universal C Runtime. The compiler's standard runtime is covered by its respective license and GCC Runtime Library Exception (see the compiler distribution).

Linux uses separately installed distribution packages: IBus (https://github.com/ibus/ibus), ibus-libpinyin (https://github.com/libpinyin/ibus-libpinyin), GTK 3, PyGObject and Python. The adapter invokes the system IBus/libpinyin service through D-Bus; it does not bundle or copy their engine source or binaries. Their original licenses remain applicable to those packages. The adapter and common wordbank core are independently implemented here.

## Vodyanitsa / 沃雅妮莎 icon

- Game / character: Genshin Impact / Vodyanitsa (沃雅妮莎).
- Source: official HoYoWiki, https://wiki.hoyolab.com/m/genshin/entry/11702?lang=zh-cn .
- Original asset: https://act-webstatic.hoyoverse.com/event-static-hoyowiki-admin/2026/09/21/e106be431d7c8ebbb9607b032fc66e3d_995961298610120887.png .
- Included as `resources/vodyanitsa.png`; `resources/app.ico` only converts/resizes the same transparent avatar into Windows icon sizes.
- © All rights reserved by miHoYo / HoYoverse. Other properties belong to their respective owners. This is an unofficial, non-commercial fan project; it is not endorsed by or affiliated with miHoYo / HoYoverse.
- Game artwork is excluded from the program's source license. This project does not grant permission to redistribute or commercially use the artwork; rights remain with its owners. General official content guidance: https://www.hoyolab.com/article/142895 .
