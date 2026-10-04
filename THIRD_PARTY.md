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


## Vodyanitsa / 沃雅妮莎 icon

- Game / character: Genshin Impact / Vodyanitsa (沃雅妮莎).
- Source: official HoYoWiki, https://wiki.hoyolab.com/m/genshin/entry/11702?lang=zh-cn .
- Original asset: https://act-webstatic.hoyoverse.com/event-static-hoyowiki-admin/2026/09/21/e106be431d7c8ebbb9607b032fc66e3d_995961298610120887.png .
- The original avatar was used as an identity reference for the generated full-body mascot `resources/pinyinshift.png` and the two fan backgrounds. The original avatar is not bundled. `resources/app.ico` converts/resizes the full-body mascot into Windows icon sizes, preserving its complete composition and transparency; generation notes are in `resources/ICON.md`.
- © All rights reserved by miHoYo / HoYoverse. Other properties belong to their respective owners. This is an unofficial, non-commercial fan project; it is not endorsed by or affiliated with miHoYo / HoYoverse.
- Game artwork is excluded from the program's source license. This project does not grant permission to redistribute or commercially use the artwork; rights remain with its owners. General official content guidance: https://www.hoyolab.com/article/142895 .

## Offline translation model and runtime

- OPUS / Helsinki-NLP English–Chinese model: https://huggingface.co/Helsinki-NLP/opus-mt-en-zh, Apache-2.0. The int8 CTranslate2 conversion is from https://huggingface.co/jiangzhuo9357/opus-mt-en-zh-ct2 at the revision recorded in `models/manifest.json`. Full license: `models/LICENSE.Apache-2.0.txt`. We did not alter its weights. Model provenance and inference changes: `models/README.md`.
- OPUS / Helsinki-NLP Chinese–English model: https://huggingface.co/Helsinki-NLP/opus-mt-zh-en, developed by the University of Helsinki Language Technology Research Group / OPUS contributors, CC-BY-4.0. The CTranslate2 int8 conversion is from https://huggingface.co/jiangzhuo9357/opus-mt-zh-en-ct2 at `b9f5527123795289acc264489cbd4927801601d7`; checksums and sizes are in `models/zh-en-manifest.json`. This is a quantized adaptation of the upstream model; PinyinShift did not further train or modify its weights. Full upstream license: `models/LICENSE.CC-BY-4.0.txt`; original authors retain their rights, including CC-BY attribution requirements. The project's non-commercial restriction does not apply to this model.
- Windows embedded Python: https://www.python.org/downloads/release/python-31210/ (PSF license). Runtime wheels: CTranslate2 https://github.com/OpenNMT/CTranslate2 (MIT), SentencePiece https://github.com/google/sentencepiece (Apache-2.0), NumPy https://numpy.org (BSD and its bundled numerical-library licenses), PyYAML https://pyyaml.org (MIT), setuptools https://github.com/pypa/setuptools (MIT), pip https://pip.pypa.io (MIT and vendored dependency notices). Original license files remain inside `runtime/windows-runtime.zip`; extract it to review `LICENSE.txt` and the packages' `.dist-info` directories. See `runtime/README.md` and `translation/requirements.txt` for versions.
- Video playback uses Windows system Media Foundation.
- `msvc-runtime` 14.44.35112 wheel from PyPI supplies unmodified Microsoft-signed C++ runtime files; Microsoft proprietary terms apply. Its redistribution notice is preserved in `msvc_runtime-14.44.35112.dist-info/licenses/LICENSE`. Microsoft redistribution list: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution . These files are not covered by the program license.
- The project's non-commercial restriction does not modify the licenses or rights of these third-party components.

## Generated fan backgrounds

`resources/backgrounds/moon-garden.png` and `morning-ripple.png` were generated with Codex's built-in image-generation tool using the character avatar as a reference. Design prompts are documented in `resources/backgrounds/PROMPTS.md`. These are unofficial fan backgrounds; underlying Genshin Impact character rights remain with miHoYo / HoYoverse and are excluded from the program license. No endorsement or commercial rights are granted.
