"""Headless GTK checks in an isolated desktop/session, never the user's UI."""
import os
import sys
import tempfile
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'linux'))
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib
from app import Application
from core import autostart, autostart_enabled, desktop_exec


def pump(seconds=0.1):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        time.sleep(0.005)


def controls(window):
    children = next(child for child in window.get_child().get_children() if isinstance(child, Gtk.Box)).get_children()
    views = [item.get_child() for item in children if isinstance(item, Gtk.ScrolledWindow)]
    button = next(item for item in children if isinstance(item, Gtk.Button) and item.get_label() == '翻译为中文')
    return views[0], views[1], button


def text(view):
    buffer = view.get_buffer()
    return buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)


application = Application(engine_only=True)
try:
    application.show_translation()
    source, output, button = controls(application.translation)
    source.get_buffer().set_text('I am in a meeting.')
    button.emit('clicked')
    deadline = time.monotonic() + 5
    while not button.get_sensitive() and time.monotonic() < deadline:
        pump()
    assert text(output) == '我正在开会', text(output)
    assert not output.get_editable()
    print('PASS GTK offline translation and copyable readonly output', flush=True)
    source.get_buffer().set_text('Stateless GitHub App installation tokens rolled out')
    button.emit('clicked')
    deadline = time.monotonic() + 60
    while not button.get_sensitive() and time.monotonic() < deadline:
        pump()
    assert '无状态' in text(output) and 'GitHub' in text(output) and '令牌' in text(output), text(output)
    assert '未收录' not in text(output)
    print('PASS GTK local neural sentence translation', flush=True)
    assert application.custom_panel
    application.show_candidates('发展: 1 develop   2 development   3 growth', (300, 500, 1, 20))
    pump()
    assert application.panel.window.get_visible()
    assert not application.panel.window.get_accept_focus()
    application.hide_candidates()
    assert not application.panel.window.get_visible()
    for kind in ('translation', 'candidates'):
        application.show_background(kind)
        pump()
        application.editors[-1].destroy()
    print('PASS GTK backgrounds and non-focus X11 candidate panel', flush=True)
    source.get_buffer().set_text('hello ' * 1000)
    button.emit('clicked')
    application.translation.destroy()
    application.show_translation()
    pump(0.5)
    assert text(controls(application.translation)[1]) == ''
    print('PASS closing/reopening during translation cannot show a stale reply', flush=True)
    application.reload()
    assert '发展' in application.core.query('zh', 'development')['text']
    print('PASS wordbank reload', flush=True)
    with tempfile.TemporaryDirectory() as directory:
        os.environ['XDG_CONFIG_HOME'] = directory
        assert not autostart_enabled()
        autostart(True)
        assert autostart_enabled()
        entry = Path(directory) / 'autostart' / 'EnglishAssistant.desktop'
        assert 'app.py' in entry.read_text()
        autostart(False)
        assert not autostart_enabled()
    assert desktop_exec('path with spaces', '100%') == '"path with spaces" "100%%"'
    print('PASS isolated autostart and relocated path escaping', flush=True)
finally:
    if application.translation:
        application.translation.destroy()
    application.core.close()
    if application.panel:
        application.panel.close()
