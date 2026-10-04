"""Register only this user's component/desktop entry. Never remove native IMEs."""
import os
import xml.etree.ElementTree as ET
from pathlib import Path
from core import ROOT, desktop_exec, autostart, autostart_enabled

data = Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local' / 'share')))
component = ET.Element('component')
fields = {'name': 'org.freedesktop.IBus.EnglishAssistant', 'description': 'EnglishAssistant',
          'exec': desktop_exec('/usr/bin/python3', ROOT / 'linux' / 'app.py', '--ibus'),
          'version': '0.7.0', 'author': 'YYHSSR', 'license': 'Free noncommercial',
          'homepage': 'https://github.com/YYHSSR/EnglishAssistant'}
for key, value in fields.items():
    ET.SubElement(component, key).text = value
engine = ET.SubElement(ET.SubElement(component, 'engines'), 'engine')
for key, value in {'name': 'EnglishAssistant', 'longname': 'EnglishAssistant 智能拼音',
                   'description': '系统 libpinyin + 离线英文候选', 'language': 'zh',
                   'license': 'Free noncommercial', 'author': 'YYHSSR', 'layout': 'us',
                   'icon': str(ROOT / 'resources' / 'vodyanitsa.png'), 'symbol': '英', 'rank': '80'}.items():
    ET.SubElement(engine, key).text = value
target = data / 'ibus' / 'component' / 'EnglishAssistant.xml'
target.parent.mkdir(parents=True, exist_ok=True)
ET.ElementTree(component).write(str(target), encoding='utf-8', xml_declaration=True)
target = data / 'applications' / 'EnglishAssistant.desktop'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('[Desktop Entry]\nType=Application\nName=EnglishAssistant\nComment=离线英文候选与英文转中文\nExec=' +
                  desktop_exec('/usr/bin/python3', ROOT / 'linux' / 'app.py', '--translate') +
                  '\nIcon=' + str(ROOT / 'resources' / 'vodyanitsa.png') + '\nTerminal=false\nCategories=Utility;\n', encoding='utf-8')
if autostart_enabled():
    autostart(True)
try:
    from gi.repository import Gio, GLib
    schema = 'org.gnome.desktop.input-sources'
    if Gio.SettingsSchemaSource.get_default().lookup(schema, True):
        settings = Gio.Settings.new(schema)
        sources = settings.get_value('sources').unpack()
        entry = ('ibus', 'EnglishAssistant')
        if entry not in sources:
            settings.set_value('sources', GLib.Variant('a(ss)', sources + [entry]))
            Gio.Settings.sync()
except Exception as error:
    print('请在系统设置 → 区域与语言 → 输入源中添加 EnglishAssistant：' + str(error))
