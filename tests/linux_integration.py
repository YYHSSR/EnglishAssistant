"""Real IBus/libpinyin integration in a private Xvfb + D-Bus test session."""
import json
import os
import subprocess
import sys
import time
from pathlib import Path
import gi
gi.require_version('IBus', '1.0')
from gi.repository import IBus, GLib

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'linux'))
from core import Core


def pump(seconds=0.15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        time.sleep(0.005)


def wait(predicate, message, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pump(0.05)
        if predicate():
            return
    raise AssertionError(message)


core = Core()
assert core.query('en', '趋于完美了')['senses'][0] == 'It is approaching perfection.'
assert core.query('en', '电脑软件设计开发流程')['reference']
assert '发展' in core.query('zh', 'development')['text']
core.close()
print('PASS shared-core IPC', flush=True)

IBus.init()
bus = IBus.Bus()
assert bus.is_connected()
application = subprocess.Popen(['/usr/bin/python3', str(root / 'linux' / 'app.py'), '--ibus'])
try:
    wait(lambda: any(e.get_name() == 'EnglishAssistant' for e in bus.list_active_engines()), 'Engine did not register')
    context = bus.create_input_context('EnglishAssistantIntegration')
    context.set_capabilities(int(IBus.Capabilite.PREEDIT_TEXT | IBus.Capabilite.AUXILIARY_TEXT |
                                 IBus.Capabilite.LOOKUP_TABLE | IBus.Capabilite.FOCUS))
    committed = []
    auxiliary = []
    chinese = []
    context.connect('commit-text', lambda _, text: committed.append(text.get_text()))
    context.connect('update-auxiliary-text', lambda _, text, visible: auxiliary.append(text.get_text()) if visible else None)
    context.connect('update-lookup-table', lambda _, table, visible: chinese.append([table.get_candidate(i).get_text() for i in range(min(10, table.get_number_of_candidates()))]) if visible else None)
    context.focus_in()
    context.set_engine('EnglishAssistant')
    wait(lambda: context.get_engine() and context.get_engine().get_name() == 'EnglishAssistant', 'Engine activation')

    def key(keyval, state=0):
        context.process_key_event(keyval, 0, state)
        pump()

    for letter in 'fazhan':
        key(ord(letter))
    wait(lambda: chinese and any('发展' in row for row in chinese), 'Native Chinese candidates missing')
    wait(lambda: auxiliary and any('development' in text and 'growth' in text for text in auxiliary), 'Noun senses missing')
    key(IBus.KEY_Control_L)
    key(IBus.KEY_1, int(IBus.ModifierType.CONTROL_MASK))
    key(IBus.KEY_1, int(IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.RELEASE_MASK))
    key(IBus.KEY_Control_L, int(IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.RELEASE_MASK))
    wait(lambda: committed, 'Ctrl selection did not commit')
    assert committed == ['develop'], committed
    print('PASS real libpinyin candidates and Ctrl+1 English commit', flush=True)

    committed.clear()
    for letter in 'fazhan':
        key(ord(letter))
    key(IBus.KEY_Control_L)
    key(IBus.KEY_Down, int(IBus.ModifierType.CONTROL_MASK))
    key(IBus.KEY_Down, int(IBus.ModifierType.CONTROL_MASK))
    key(IBus.KEY_Control_L, int(IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.RELEASE_MASK))
    wait(lambda: committed, 'Arrow selection did not commit')
    assert committed == ['development'], committed
    print('PASS Ctrl+arrows English selection', flush=True)

    committed.clear()
    for letter in 'fazhan':
        key(ord(letter))
    key(IBus.KEY_space)
    wait(lambda: committed, 'Normal Chinese selection did not commit')
    assert committed == ['发展'], committed
    print('PASS native Chinese selection preserved', flush=True)

    committed.clear()
    for letter in 'fazhan':
        key(ord(letter))
    key(IBus.KEY_Control_L)
    key(IBus.KEY_1, int(IBus.ModifierType.CONTROL_MASK))
    context.focus_out()
    pump()
    key(IBus.KEY_Control_L, int(IBus.ModifierType.CONTROL_MASK | IBus.ModifierType.RELEASE_MASK))
    assert committed == [], committed
    print('PASS focus-out cancels pending English commit', flush=True)
finally:
    application.terminate()
    application.wait(timeout=5)
