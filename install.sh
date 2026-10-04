#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' '此安装脚本用于 Linux。Windows 请运行 EnglishAssistant.exe。'
    exit 1
fi
if [ "$(id -u)" = 0 ]; then
    printf '%s\n' '请以普通桌面用户运行此脚本；安装依赖时会调用 sudo。'
    exit 1
fi
if [ -r /etc/os-release ]; then
    . /etc/os-release
    version_major=${VERSION_ID%%.*}
    if [ "$ID" = ubuntu ] && [ "$version_major" -lt 20 ]; then
        printf '%s\n' '最低支持 Ubuntu 20.04。'
        exit 1
    fi
fi
if ! command -v cmake >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1 ||
   ! /usr/bin/python3 -c 'import gi, venv; gi.require_version("IBus","1.0"); gi.require_version("Gtk","3.0"); gi.require_version("GstVideo","1.0"); from gi.repository import IBus,Gtk,GstVideo; import ensurepip' >/dev/null 2>&1 ||
   [ ! -f /usr/share/ibus/component/libpinyin.xml ]; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake python3-gi python3-venv python3-gi-cairo gir1.2-gtk-3.0 gir1.2-ibus-1.0 gir1.2-gstreamer-1.0 gir1.2-gst-plugins-base-1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav ibus ibus-libpinyin
fi
mkdir -p "$root/build"
cd "$root/build"
cmake -DCMAKE_BUILD_TYPE=Release "$root"
cmake --build . -- -j2
ctest --output-on-failure
/usr/bin/python3 -m venv "$root/runtime/venv"
"$root/runtime/venv/bin/python" -m pip install -r "$root/translation/requirements.txt"
/usr/bin/python3 "$root/linux/register.py"
printf '%s\n' '安装完成。运行 sh ./start.sh，然后选择 EnglishAssistant 智能拼音。' '如果输入源列表尚未出现它，请注销后重新登录。' '翻译框：sh ./start.sh --translate。原有输入法保留，可随时切回。'
