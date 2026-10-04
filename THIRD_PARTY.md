# Third-party material

## Qingjian English glossary

- Project: 青简 Qingjian, https://github.com/qingjian-team/qingjian
- Authors: Qingjian contributors / qingjian-team
- Source file: `assets/glossary/glossary-en.tsv`
- Reference revision: `c08ae57cb88b6a4a46f4a5e9c1d6d11c5e69222e` (the repository revision inspected for this work on 2026-10-03).
- Source documentation: https://github.com/qingjian-team/qingjian/blob/main/assets/glossary/README.md
- Stated license: GPL-3.0-or-later. The full GPL v3 text is included in `data/LICENSE.GPL-3.0.txt`. This standalone data file retains GPL rights; the project's non-commercial restriction does not apply to it.
- The source document says the English senses were generated offline using DeepSeek, without third-party dictionary content. The original file is distributed unchanged as `data/glossary-en.tsv`.
- The source glossary README is preserved as `data/QINGJIAN-GLOSSARY-README.md`.
- No Qingjian program source, branding, icon or input engine is included in this implementation. The program independently reads an external TSV file; this glossary is not compiled into the executable.

## Platform and build tools

Windows APIs and system DLLs provide the application UI and accessibility interfaces. MinGW-w64 GCC was used to build the Windows binaries, with its standard runtime statically linked; the output imports only Windows system libraries and the Windows Universal C Runtime. The compiler's standard runtime is covered by its respective license and GCC Runtime Library Exception (see the compiler distribution).

## JSON for Modern C++

- Project: https://github.com/nlohmann/json
- Version: 3.12.0, unmodified single-header distribution.
- Author: Niels Lohmann and contributors.
- License: MIT, included as `third_party/nlohmann/LICENSE.MIT`.
- Used to parse the optional MyMemory API response.

## Optional translation service

MyMemory is an external service operated by Translated: https://mymemory.translated.net/ . Its free anonymous `/get` endpoint is used only when the user enables network supplementation. Service limits: https://mymemory.translated.net/doc/usagelimits.php . No paid API key, account email, or `/set` contribution endpoint is used. Network results are cached only in memory.

## Vodyanitsa / 沃雅妮莎 icon

- Game / character: Genshin Impact / Vodyanitsa (沃雅妮莎).
- Source: official HoYoWiki, https://wiki.hoyolab.com/m/genshin/entry/11702?lang=zh-cn .
- Original asset: https://act-webstatic.hoyoverse.com/event-static-hoyowiki-admin/2026/09/21/e106be431d7c8ebbb9607b032fc66e3d_995961298610120887.png .
- Included as `resources/vodyanitsa.png`; `resources/app.ico` only converts/resizes the same transparent avatar into Windows icon sizes.
- © All rights reserved by miHoYo / HoYoverse. Other properties belong to their respective owners. This is an unofficial, non-commercial fan project; it is not endorsed by or affiliated with miHoYo / HoYoverse.
- Game artwork is excluded from the program's source license. This project does not grant permission to redistribute or commercially use the artwork; rights remain with its owners. General official content guidance: https://www.hoyolab.com/article/142895 .
