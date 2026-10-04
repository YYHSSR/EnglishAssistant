#!/usr/bin/env python3
"""Native GTK/IBus desktop entry; compatible with Ubuntu 20.04's Python 3.8."""
import atexit
import fcntl
import os
import signal
import socket
import sys
import threading
from pathlib import Path
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('IBus', '1.0')
from gi.repository import Gtk, IBus, GLib, Gio, Gdk
from core import Core, ROOT, autostart, autostart_enabled, desktop_exec
from engine import AssistantEngine, ensure_libpinyin
from neural import Neural
from backgrounds import Background, CandidatePanel, show_settings


class AssistantFactory(IBus.Factory):
    def __init__(self, connection):
        super().__init__(connection=connection, object_path=IBus.PATH_FACTORY)
        self.connection = connection
        self.count = 0
        self.engines = []

    def do_create_engine(self, name):
        if name != 'EnglishAssistant':
            raise RuntimeError('Unknown engine: ' + name)
        self.count += 1
        engine = AssistantEngine(connection=self.connection,
                                 object_path='/org/freedesktop/IBus/EnglishAssistant/Engine/{}'.format(self.count))
        self.engines.append(engine)
        engine.connect('destroy', lambda item: self.engines.remove(item))
        return engine


class Application:
    def __init__(self, engine_only=False):
        self.core = Core()
        self.paused = False
        self.translation = None
        self.editors = []
        self.translate_generation = 0
        self.translation_background = None
        self.panel = None
        # GNOME owns native Wayland panel positioning; retain that panel there.
        self.custom_panel = 'x11' in type(Gdk.Display.get_default()).__name__.lower() and os.environ.get('ENGLISHASSISTANT_NATIVE_PANEL') != '1'
        self.bus = IBus.Bus()
        if not self.bus.is_connected():
            raise RuntimeError('IBus 未运行。请先启动系统 IBus 输入法，或注销后重新登录。')
        ensure_libpinyin(self.bus.get_connection())
        AssistantEngine.core = self.core
        AssistantEngine.ui = self
        self.factory = AssistantFactory(self.bus.get_connection())
        self.bus.request_name('org.freedesktop.IBus.EnglishAssistant', 0)
        component = IBus.Component(name='org.freedesktop.IBus.EnglishAssistant',
                                   description='EnglishAssistant offline IBus companion',
                                   version='0.8.0', license='Free noncommercial', author='YYHSSR',
                                   homepage='https://github.com/YYHSSR/EnglishAssistant',
                                   command_line=desktop_exec('/usr/bin/python3', ROOT / 'linux' / 'app.py', '--ibus'))
        component.add_engine(IBus.EngineDesc(name='EnglishAssistant', longname='EnglishAssistant 智能拼音',
                                            description='系统 libpinyin + 离线英文候选', language='zh',
                                            license='Free noncommercial', author='YYHSSR',
                                            icon=str(ROOT / 'resources' / 'vodyanitsa.png'), layout='us',
                                            symbol='英', rank=80))
        self.bus.register_component(component)
        self.bus.connect('disconnected', lambda *_: Gtk.main_quit())
        if not engine_only:
            self.make_tray()
        atexit.register(self.core.close)

    def report(self, error):
        print('EnglishAssistant: ' + str(error), file=sys.stderr)

    def startup_enabled(self):
        return autostart_enabled()

    def toggle_startup(self):
        autostart(not autostart_enabled())

    def make_tray(self):
        self.icon = Gtk.StatusIcon.new_from_file(str(ROOT / 'resources' / 'vodyanitsa.png'))
        self.icon.set_tooltip_text('EnglishAssistant · 离线英文候选')
        self.icon.connect('activate', lambda *_: self.show_translation())
        self.icon.connect('popup-menu', self.tray_menu)

    def tray_menu(self, icon, button, time):
        menu = Gtk.Menu()
        def item(label, callback, checked=None):
            entry = Gtk.CheckMenuItem(label=label) if checked is not None else Gtk.MenuItem(label=label)
            if checked is not None:
                entry.set_active(checked)
            entry.connect('activate', lambda *_: callback())
            menu.append(entry)
        item('暂停英文候选', lambda: setattr(self, 'paused', not self.paused), self.paused)
        item('开机自启动', self.toggle_startup, self.startup_enabled())
        item('英文 → 中文翻译框', self.show_translation)
        item('翻译框背景…', lambda: self.show_background('translation'))
        item('英文选词框背景…', lambda: self.show_background('candidates'))
        item('退出', Gtk.main_quit)
        menu.show_all()
        menu.popup(None, None, Gtk.StatusIcon.position_menu, icon, button, time)

    def show_background(self, kind):
        window = show_settings(self, kind)
        if kind == 'candidates' and not self.custom_panel:
            note = Gtk.Label(label='当前 Wayland 会话使用系统候选面板；自定义英文背景需登录 Ubuntu on Xorg。')
            note.set_line_wrap(True)
            window.get_child().pack_start(note, False, False, 0)
            note.show()

    def reload_backgrounds(self, kind):
        if kind == 'translation' and self.translation_background:
            self.translation_background.reload()
        if kind == 'candidates' and self.panel:
            self.panel.background.reload()

    def show_candidates(self, text, cursor):
        if not self.custom_panel:
            return False
        if not self.panel:
            self.panel = CandidatePanel()
        self.panel.show(text, cursor)
        return True

    def hide_candidates(self):
        if self.panel:
            self.panel.hide()

    def reload(self):
        # Swap only between requests; a worker keeps its current core alive.
        replacement = Core()
        previous = self.core
        self.core = replacement
        AssistantEngine.core = replacement
        threading.Thread(target=previous.close, daemon=True).start()

    @staticmethod
    def text_area(editable=True):
        view = Gtk.TextView()
        view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        view.set_editable(editable)
        view.set_left_margin(12)
        view.set_right_margin(12)
        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        scroll.add(view)
        return view, scroll

    def show_editor(self, path, editable):
        window = Gtk.Window(title='EnglishAssistant — ' + ('个人词表' if editable else '使用说明'))
        window.set_default_size(720, 520)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        window.add(box)
        view, scroll = self.text_area(editable)
        view.get_buffer().set_text(path.read_text(encoding='utf-8-sig') if path.exists() else '')
        box.pack_start(scroll, True, True, 0)
        if editable:
            button = Gtk.Button(label='保存并重新加载')
            def save(*_):
                buffer = view.get_buffer()
                temporary = path.with_name(path.name + '.saving')
                try:
                    with temporary.open('x', encoding='utf-8', newline='\n') as target:
                        target.write(buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True))
                    os.replace(str(temporary), str(path))
                    self.reload()
                    button.set_label('已保存')
                except Exception as error:
                    button.set_label('保存失败：' + str(error))
            button.connect('clicked', save)
            box.pack_start(button, False, False, 8)
        self.editors.append(window)
        window.connect('destroy', lambda *_: self.editors.remove(window))
        window.show_all()

    def show_translation(self):
        if self.translation:
            self.translation.present()
            return
        window = Gtk.Window(title='EnglishAssistant — 离线英文转中文')
        self.translation = window
        window.set_default_size(720, 560)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        box.set_border_width(14)
        overlay = Gtk.Overlay()
        window.add(overlay)
        background = Background('translation')
        self.translation_background = background
        overlay.add(background)
        overlay.add_overlay(box)
        box.set_margin_end(150)
        tools = Gtk.Box(spacing=6)
        for label, callback in [('翻译框背景', lambda: self.show_background('translation'))]:
            tool = Gtk.Button(label=label)
            tool.connect('clicked', lambda _, action=callback: action())
            tools.pack_start(tool, False, False, 0)
        box.pack_start(tools, False, False, 0)
        box.pack_start(Gtk.Label(label='粘贴英文 · 完全离线 · Ctrl + Enter 翻译'), False, False, 0)
        source, scroll = self.text_area()
        box.pack_start(scroll, True, True, 0)
        button = Gtk.Button(label='翻译为中文')
        box.pack_start(button, False, False, 0)
        destination, scroll = self.text_area(False)
        box.pack_start(scroll, True, True, 0)
        status = Gtk.Label(label='本地模型 · 支持完整英文句子和段落')
        status.set_line_wrap(True)
        box.pack_start(status, False, False, 0)
        neural = Neural()
        def translate(*_):
            if not button.get_sensitive():
                return
            buffer = source.get_buffer()
            text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            if len(text) > 8000:
                status.set_text('每次最多 8000 个字符，请分段翻译。')
                return
            button.set_sensitive(False)
            source.set_editable(False)
            status.set_text('正在运行本地翻译模型…')
            self.translate_generation += 1
            generation = self.translate_generation
            core = self.core
            def finish(reply, error):
                if self.translation is not window or generation != self.translate_generation:
                    return False
                button.set_sensitive(True)
                source.set_editable(True)
                if error:
                    status.set_text('翻译失败：' + error)
                else:
                    destination.get_buffer().set_text(reply['text'])
                    status.set_text('已匹配本地整句 / 词条' if reply['exact'] else '本地模型译文 · 请核对专名及专业术语')
                return False
            def work():
                try:
                    reply = core.query('zh', text)
                    if not reply['exact']:
                        reply['text'] = neural.translate(text)
                    GLib.idle_add(finish, reply, None)
                except Exception as error:
                    GLib.idle_add(finish, None, str(error))
            threading.Thread(target=work, daemon=True).start()
        button.connect('clicked', translate)
        def key(_, event):
            if event.keyval == IBus.KEY_Return and event.state & 4:
                translate()
                return True
            return False
        window.connect('key-press-event', key)
        def closed(*_):
            neural.close()
            self.translation = None
            self.translation_background = None
            self.translate_generation += 1
        window.connect('destroy', closed)
        window.show_all()


def main():
    cache = Path(os.environ.get('XDG_RUNTIME_DIR', str(Path.home() / '.cache'))) / 'EnglishAssistant'
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    lock = (cache / 'app.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        if '--translate' in sys.argv:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(2)
                client.connect(str(cache / 'app.sock'))
                client.sendall(b'translate')
        else:
            print('EnglishAssistant 已运行；可在输入法菜单打开翻译框。')
        return 0
    (ROOT / 'personal.tsv').touch(exist_ok=True)
    app = Application('--ibus' in sys.argv)
    address = cache / 'app.sock'
    if address.exists():
        address.unlink()
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(address))
    os.chmod(str(address), 0o600)
    server.listen(4)
    server.setblocking(False)
    def receive(*_):
        try:
            client, _ = server.accept()
            with client:
                client.settimeout(0.2)
                if client.recv(32) == b'translate':
                    app.show_translation()
        except (OSError, socket.timeout) as error:
            app.report(error)
        return True
    GLib.io_add_watch(server.fileno(), GLib.IO_IN, receive)
    if '--translate' in sys.argv:
        app.show_translation()
    for signum in (signal.SIGTERM, signal.SIGINT):
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signum, lambda: (Gtk.main_quit(), False)[1])
    Gtk.main()
    server.close()
    address.unlink()
    app.core.close()
    if app.translation:
        app.translation.destroy()
    if app.panel:
        app.panel.close()
    for editor in list(app.editors):
        editor.destroy()
    atexit.unregister(app.core.close)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print('EnglishAssistant 启动失败：' + str(error), file=sys.stderr)
        sys.exit(1)
