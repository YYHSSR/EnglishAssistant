"""Python 3.6+ client for the shared, persistent C++ wordbank process."""
import json
import os
import subprocess
import threading
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Core:
    def __init__(self):
        self.lock = threading.Lock()
        self.cache = OrderedDict()
        executable = ROOT / 'build' / 'EnglishAssistantCore'
        self.process = subprocess.Popen(
            [str(executable), '--serve', str(ROOT)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, universal_newlines=True, encoding='utf-8',
            bufsize=1)
        ready = json.loads(self.process.stdout.readline())
        if not ready.get('ready'):
            raise RuntimeError('无法加载离线词库')

    def query(self, direction, text):
        if len(text) > 8000:
            raise ValueError('每次最多 8000 个字符')
        key = (direction, text)
        with self.lock:
            if key in self.cache:
                self.cache.move_to_end(key)
                return self.cache[key]
            self.process.stdin.write(direction + '\t' + text.encode('utf-8').hex() + '\n')
            self.process.stdin.flush()
            result = json.loads(self.process.stdout.readline())
            if 'error' in result:
                raise RuntimeError(result['error'])
            self.cache[key] = result
            if len(self.cache) > 256:
                self.cache.popitem(last=False)
            return result

    def close(self):
        with self.lock:
            if self.process.stdin.closed:
                return
            self.process.stdin.close()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=2)


def desktop_exec(*arguments):
    # freedesktop Exec quoting, not shell quoting. % must be doubled.
    return ' '.join('"' + str(arg).replace('\\', '\\\\').replace('"', '\\"')
                    .replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
                    for arg in arguments)


def autostart(enabled):
    target = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'autostart' / 'EnglishAssistant.desktop'
    if enabled:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('[Desktop Entry]\nType=Application\nName=EnglishAssistant\nExec=' +
                          desktop_exec('/usr/bin/python3', ROOT / 'linux' / 'app.py') +
                          '\nX-GNOME-Autostart-enabled=true\n', encoding='utf-8')
    elif target.exists():
        target.unlink()


def autostart_enabled():
    return (Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) /
            'autostart' / 'EnglishAssistant.desktop').exists()
