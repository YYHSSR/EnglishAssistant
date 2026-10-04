"""IBus adapter. Chinese remains the distribution's actual libpinyin engine.

Create a separate engine through its native factory, rather than a nested
InputContext: nested contexts would steal IBus's global focus/engine.
"""
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
import gi
gi.require_version('IBus', '1.0')
from gi.repository import IBus, Gio, GLib, GObject

# Newer libibus only deserializes GTypes that the client has registered.
for _name in ('Text', 'Attribute', 'AttrList', 'LookupTable', 'Property', 'PropList', 'EngineDesc', 'Component'):
    GObject.type_ensure(getattr(IBus, _name).__gtype__)

SERVICE = 'org.freedesktop.IBus.Libpinyin'
INTERFACE = 'org.freedesktop.IBus.Engine'


def ensure_libpinyin(connection):
    def owner():
        return connection.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
                                    'org.freedesktop.DBus', 'NameHasOwner',
                                    GLib.Variant('(s)', (SERVICE,)), None,
                                    Gio.DBusCallFlags.NONE, 1000, None).unpack()[0]
    if owner():
        return
    component = ET.parse('/usr/share/ibus/component/libpinyin.xml').getroot()
    command = shlex.split(component.findtext('exec'))
    subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 5
    while not owner():
        if time.monotonic() >= deadline:
            raise RuntimeError('无法启动原生 ibus-libpinyin，请运行 ./install.sh')
        time.sleep(0.02)


class AssistantEngine(IBus.Engine):
    __gtype_name__ = 'EnglishAssistantEngine'
    core = None
    ui = None

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.connection = self.props.connection
        self.options = []
        self.selected = -1
        self.page = 0
        self.frozen = False
        self.focused = False
        self.password = False
        self.suppress_commit = False
        self.swallowed = set()
        self.ctrl_keys = set()
        self.generation = 0
        self.revision = 0
        self.inner_path = self.connection.call_sync(
            SERVICE, IBus.PATH_FACTORY, 'org.freedesktop.IBus.Factory', 'CreateEngine',
            GLib.Variant('(s)', ('libpinyin',)), GLib.VariantType.new('(o)'),
            Gio.DBusCallFlags.NONE, 5000, None).unpack()[0]
        self.subscription = self.connection.signal_subscribe(
            SERVICE, INTERFACE, None, self.inner_path, None,
            Gio.DBusSignalFlags.NONE, self.inner_signal)
        self.inner('SetCapabilities', '(u)', (int(IBus.Capabilite.PREEDIT_TEXT |
                                               IBus.Capabilite.AUXILIARY_TEXT |
                                               IBus.Capabilite.LOOKUP_TABLE |
                                               IBus.Capabilite.PROPERTY |
                                               IBus.Capabilite.FOCUS),))
        self.inner('Enable')

    def inner(self, method, signature='()', values=()):
        return self.connection.call_sync(SERVICE, self.inner_path, INTERFACE, method,
                                         GLib.Variant(signature, values), None,
                                         Gio.DBusCallFlags.NONE, 2000, None)

    def clear(self):
        self.options = []
        self.selected = -1
        self.page = 0
        self.frozen = False
        self.generation += 1
        self.hide_auxiliary_text()

    def inner_signal(self, connection, sender, path, interface, signal, parameters):
        try:
            def value(index):
                variant = parameters.get_child_value(index).get_variant()
                return IBus.Serializable.deserialize_object(variant)
            if signal == 'CommitText':
                if not self.suppress_commit and self.focused and not self.password:
                    self.commit_text(value(0))
                self.clear()
            elif signal == 'UpdatePreeditText':
                args = parameters.unpack()
                self.update_preedit_text(value(0), args[1], args[2])
            elif signal == 'UpdatePreeditTextWithMode':
                args = parameters.unpack()
                self.update_preedit_text_with_mode(value(0), args[1], args[2], args[3])
            elif signal == 'UpdateLookupTable':
                table = value(0)
                visible = parameters.unpack()[1]
                self.update_lookup_table(table, visible)
                if not visible:
                    self.clear()
                elif not self.frozen:
                    self.lookup(table)
            elif signal == 'HideLookupTable':
                self.hide_lookup_table()
                self.clear()
            elif signal == 'HidePreeditText':
                self.hide_preedit_text()
            elif signal == 'ShowPreeditText':
                self.show_preedit_text()
            elif signal == 'ShowLookupTable':
                self.show_lookup_table()
            elif signal == 'RegisterProperties':
                props = value(0)
                props.append(IBus.Property(key='ea-translate', label=IBus.Text.new_from_string('英文 → 中文翻译框')))
                for key, label in [('ea-pause', '暂停 / 恢复英文候选'), ('ea-personal', '编辑个人词表'),
                                   ('ea-reload', '重新加载个人词表'), ('ea-help', '使用说明')]:
                    props.append(IBus.Property(key=key, label=IBus.Text.new_from_string(label)))
                props.append(IBus.Property(key='ea-startup', prop_type=IBus.PropType.TOGGLE,
                                          label=IBus.Text.new_from_string('开机自启动'),
                                          state=IBus.PropState.CHECKED if self.ui.startup_enabled() else IBus.PropState.UNCHECKED))
                self.register_properties(props)
            elif signal == 'UpdateProperty':
                self.update_property(value(0))
            elif signal == 'ForwardKeyEvent':
                self.forward_key_event(*parameters.unpack())
        except Exception as error:
            self.clear()
            self.ui.report(error)

    def lookup(self, table):
        if not self.focused or self.password or self.ui.paused:
            self.clear()
            return
        begin = (table.get_cursor_pos() // table.get_page_size()) * table.get_page_size()
        entries = [table.get_candidate(i).get_text() for i in range(begin, min(begin + table.get_page_size(), table.get_number_of_candidates()))]
        if any(len(text) >= 4 for text in entries):
            entries.sort(key=lambda text: -len(text))
        self.options = []
        for word in entries:
            reply = self.core.query('en', word)
            for sense in reply['senses']:
                self.options.append((word, sense, reply['reference']))
        self.selected = -1
        self.page = 0
        self.render()

    def render(self):
        if not self.options or not self.focused:
            self.hide_auxiliary_text()
            return
        lines = ['英文 · Ctrl + 数字 / 方向键，松开输出']
        last = None
        for index in range(self.page * 9, min(len(self.options), self.page * 9 + 9)):
            word, english, reference = self.options[index]
            label = word + (' · 词组参考' if reference else '')
            item = '{}{} {}'.format('▶' if index == self.selected else '', index % 9 + 1, english)
            if label == last and len(lines[-1]) + len(item) < 65:
                lines[-1] += '   ' + item
            else:
                lines.append(label + ': ' + item)
            last = label
        if len(self.options) > 9:
            lines.append('第 {} / {} 页'.format(self.page + 1, (len(self.options) + 8) // 9))
        self.update_auxiliary_text(IBus.Text.new_from_string('\n'.join(lines)), True)

    def do_process_key_event(self, keyval, keycode, state):
        if not self.focused or self.password:
            return False
        release = bool(state & IBus.ModifierType.RELEASE_MASK)
        ctrl = bool(state & IBus.ModifierType.CONTROL_MASK)
        if keyval in (IBus.KEY_Control_L, IBus.KEY_Control_R):
            if release:
                self.ctrl_keys.discard(keyval)
            else:
                self.ctrl_keys.add(keyval)
            if release and not self.ctrl_keys and self.selected >= 0:
                self.commit_english()
                return True
            return False
        if release and keyval in self.swallowed:
            self.swallowed.discard(keyval)
            return True
        invalid_modifiers = IBus.ModifierType.MOD1_MASK | IBus.ModifierType.SUPER_MASK | IBus.ModifierType.SHIFT_MASK
        if not release and ctrl and not (state & invalid_modifiers) and self.options and self.focused and not self.password and not self.ui.paused:
            index = None
            if IBus.KEY_1 <= keyval <= IBus.KEY_9:
                index = self.page * 9 + keyval - IBus.KEY_1
            elif IBus.KEY_KP_1 <= keyval <= IBus.KEY_KP_9:
                index = self.page * 9 + keyval - IBus.KEY_KP_1
            elif keyval in (IBus.KEY_Up, IBus.KEY_Left, IBus.KEY_Down, IBus.KEY_Right):
                delta = 1 if keyval in (IBus.KEY_Down, IBus.KEY_Right) else -1
                index = self.page * 9 if self.selected < 0 else max(0, min(len(self.options) - 1, self.selected + delta))
            if index is not None and index < len(self.options):
                self.selected = index
                self.page = index // 9
                self.frozen = True
                self.swallowed.add(keyval)
                self.render()
                return True
        if not release:
            self.revision += 1
            self.clear()
        try:
            return self.inner('ProcessKeyEvent', '(uuu)', (keyval, keycode, state)).unpack()[0]
        except GLib.Error as error:
            self.clear()
            self.ui.report(error)
            return False

    def commit_english(self):
        if not self.focused or self.password or self.ui.paused or not (0 <= self.selected < len(self.options)):
            self.clear()
            return
        english = self.options[self.selected][1]
        revision = self.revision
        self.suppress_commit = True
        self.inner('Reset')
        # Drain queued native reset signals before permitting new commits.
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        self.clear()
        self.suppress_commit = False
        self.hide_preedit_text()
        self.hide_lookup_table()
        if self.focused and not self.password and revision == self.revision:
            self.commit_text(IBus.Text.new_from_string(english))

    def do_focus_in(self):
        self.revision += 1
        self.focused = True
        self.inner('FocusIn')

    def do_focus_out(self):
        self.revision += 1
        self.ctrl_keys.clear()
        self.focused = False
        self.clear()
        self.inner('FocusOut')
        self.inner('Reset')

    def do_reset(self):
        self.revision += 1
        self.clear()
        self.inner('Reset')

    def do_set_cursor_location(self, x, y, width, height):
        self.inner('SetCursorLocation', '(iiii)', (x, y, width, height))

    def do_set_content_type(self, purpose, hints):
        self.password = purpose in (int(IBus.InputPurpose.PASSWORD), int(IBus.InputPurpose.PIN))
        if self.password:
            self.do_reset()
        # Older libpinyin versions may ignore this optional method.

    def do_candidate_clicked(self, index, button, state):
        self.inner('CandidateClicked', '(uuu)', (index, button, state))

    def do_page_up(self):
        self.inner('PageUp')

    def do_page_down(self):
        self.inner('PageDown')

    def do_cursor_up(self):
        self.inner('CursorUp')

    def do_cursor_down(self):
        self.inner('CursorDown')

    def do_property_activate(self, name, state):
        if name == 'ea-translate':
            self.ui.show_translation()
        elif name == 'ea-startup':
            self.ui.toggle_startup()
        elif name == 'ea-pause':
            self.ui.paused = not self.ui.paused
            self.clear()
        elif name == 'ea-personal':
            from core import ROOT
            self.ui.show_editor(ROOT / 'personal.tsv', True)
        elif name == 'ea-help':
            from core import ROOT
            self.ui.show_editor(ROOT / '使用说明.md', False)
        elif name == 'ea-reload':
            self.ui.reload()
        else:
            self.inner('PropertyActivate', '(su)', (name, state))

    def do_destroy(self):
        if getattr(self, 'subscription', None):
            self.connection.signal_unsubscribe(self.subscription)
            self.subscription = None
            try:
                self.connection.call_sync(SERVICE, self.inner_path, 'org.freedesktop.IBus.Service',
                                          'Destroy', None, None, Gio.DBusCallFlags.NONE, 1000, None)
            except GLib.Error:
                pass
        super().do_destroy()
