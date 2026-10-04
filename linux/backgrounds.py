"""Native GTK images/GIF and GStreamer video; no browser or network service."""
import json
import os
import shutil
import threading
import time
from pathlib import Path
from urllib.parse import urlparse, unquote
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gst', '1.0')
gi.require_version('GstVideo', '1.0')
from gi.repository import Gtk, Gdk, GdkPixbuf, GLib, Gst, GstVideo
from core import ROOT
Gst.init(None)
CONFIG = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'EnglishAssistant'
VIDEOS = {'.mp4', '.m4v', '.wmv', '.mov', '.avi', '.webm'}
IMAGES = {'.png', '.jpg', '.jpeg', '.bmp', '.gif'}


def preferences(kind):
    try:
        values = json.loads((CONFIG / 'backgrounds.json').read_text(encoding='utf-8')).get(kind, {})
    except (OSError, ValueError):
        values = {}
    default = ROOT / 'resources' / 'backgrounds' / ('moon-garden.png' if kind == 'translation' else 'morning-ripple.png')
    path = Path(values.get('path', str(default)))
    return path if path.is_file() else default, bool(values.get('sound', False))


def save(kind, path=None, sound=None):
    try:
        values = json.loads((CONFIG / 'backgrounds.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        values = {}
    entry = values.setdefault(kind, {})
    if path is not None:
        entry['path'] = str(path)
    if sound is not None:
        entry['sound'] = sound
    CONFIG.mkdir(parents=True, exist_ok=True)
    temporary = CONFIG / 'backgrounds.json.saving'
    temporary.write_text(json.dumps(values, ensure_ascii=False), encoding='utf-8')
    os.replace(str(temporary), str(CONFIG / 'backgrounds.json'))


class Background(Gtk.DrawingArea):
    def __init__(self, kind, preview=False):
        super().__init__()
        self.kind, self.preview = kind, preview
        self.player = None
        self.animation = self.iterator = self.frame = None
        self.timer = None
        self.lock = threading.Lock()
        self.failure = ''
        self.connect('draw', self.draw_background)
        self.connect('map', lambda *_: self.visible(True))
        self.connect('unmap', lambda *_: self.visible(False))
        self.connect('destroy', lambda *_: self.close())
        self.reload()

    def close(self):
        self.visible(False)
        if self.player:
            self.player.set_state(Gst.State.NULL)
            self.player.get_bus().remove_signal_watch()
            self.player = None

    def reload(self):
        self.close()
        self.animation = self.iterator = self.frame = None
        self.failure = ''
        path, sound = preferences(self.kind)
        try:
            if path.suffix.lower() in VIDEOS:
                self.player = Gst.ElementFactory.make('playbin', None)
                sink = Gst.ElementFactory.make('appsink', None)
                if not self.player or not sink:
                    raise RuntimeError('缺少 GStreamer 视频组件，请重新运行 install.sh')
                sink.set_property('caps', Gst.Caps.from_string('video/x-raw,format=RGB'))
                sink.set_property('emit-signals', True)
                sink.set_property('max-buffers', 1)
                sink.set_property('drop', True)
                sink.connect('new-sample', self.sample)
                self.player.set_property('video-sink', sink)
                self.player.set_property('uri', path.resolve().as_uri())
                if self.preview or not sound:
                    self.player.set_property('audio-sink', Gst.ElementFactory.make('fakesink', None))
                bus = self.player.get_bus()
                bus.add_signal_watch()
                bus.connect('message', self.message)
            else:
                self.animation = GdkPixbuf.PixbufAnimation.new_from_file(str(path))
                if self.animation.get_width() > 8192 or self.animation.get_height() > 8192:
                    raise RuntimeError('图片尺寸超过 8192 × 8192')
                self.iterator = self.animation.get_iter(None)
            self.visible(self.get_mapped())
        except Exception as error:
            self.failure = str(error)
        self.queue_draw()

    def message(self, bus, message):
        if message.type == Gst.MessageType.EOS:
            self.player.seek_simple(Gst.Format.TIME, Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT, 0)
        elif message.type == Gst.MessageType.ERROR:
            self.failure = str(message.parse_error()[0])
            self.player.set_state(Gst.State.NULL)

    def sample(self, sink):
        sample = sink.emit('pull-sample')
        info = GstVideo.VideoInfo()
        info.from_caps(sample.get_caps())
        if info.width > 4096 or info.height > 4096:
            self.failure = '视频尺寸超过 4096 × 4096'
            return Gst.FlowReturn.ERROR
        data = sample.get_buffer().extract_dup(0, sample.get_buffer().get_size())
        frame = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(data), GdkPixbuf.Colorspace.RGB, False, 8,
                                              info.width, info.height, info.stride[0])
        with self.lock:
            self.frame = frame
        return Gst.FlowReturn.OK

    def visible(self, value):
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = None
        if self.player:
            self.player.set_state(Gst.State.PLAYING if value else Gst.State.PAUSED)
        animated = self.player is not None or (self.animation and not self.animation.is_static_image())
        if value and animated:
            self.timer = GLib.timeout_add(67, lambda: (self.queue_draw(), True)[1])

    def draw_background(self, widget, context):
        context.set_source_rgb(.96, .98, 1)
        context.paint()
        with self.lock:
            frame = self.frame
        if self.iterator:
            self.iterator.advance(None)
            frame = self.iterator.get_pixbuf()
        if not frame:
            return False
        width, height = self.get_allocated_width(), self.get_allocated_height()
        scale = max(width / frame.get_width(), height / frame.get_height())
        context.save()
        context.translate(width - frame.get_width() * scale, height - frame.get_height() * scale)
        context.scale(scale, scale)
        Gdk.cairo_set_source_pixbuf(context, frame, 0, 0)
        context.paint()
        context.restore()
        return False


def show_settings(application, kind):
    window = Gtk.Window(title='EnglishAssistant — ' + ('翻译框背景' if kind == 'translation' else '英文选词框背景'))
    window.set_default_size(600, 400)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
    box.set_border_width(14)
    window.add(box)
    box.pack_start(Gtk.Label(label='拖入图片 / GIF / 视频，或点击选择。预览静音。'), False, False, 0)
    preview = Background(kind, preview=True)
    preview.set_size_request(520, 240)
    box.pack_start(preview, True, True, 0)
    label = Gtk.Label(label=str(preferences(kind)[0].name))
    label.set_line_wrap(True)
    box.pack_start(label, False, False, 0)
    row = Gtk.Box(spacing=10)
    box.pack_start(row, False, False, 0)
    def update():
        preview.reload()
        label.set_text(preview.failure or preferences(kind)[0].name)
        application.reload_backgrounds(kind)
    def choose(path):
        try:
            path = Path(path)
            if path.suffix.lower() not in IMAGES | VIDEOS:
                raise RuntimeError('请选择图片 / GIF / 视频')
            if path.suffix.lower() in IMAGES:
                GdkPixbuf.PixbufAnimation.new_from_file(str(path))
            destination = CONFIG / 'backgrounds' / (kind + '-' + str(time.time_ns()) + path.suffix.lower())
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(str(path), str(destination))
            save(kind, path=destination)
            update()
        except Exception as error:
            label.set_text(str(error))
    def picker(*_):
        dialog = Gtk.FileChooserDialog(title='选择图片 / 视频', transient_for=window, action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons('取消', Gtk.ResponseType.CANCEL, '选择', Gtk.ResponseType.OK)
        media = Gtk.FileFilter()
        media.set_name('背景图片 / 视频')
        for ext in IMAGES | VIDEOS:
            media.add_pattern('*' + ext)
        dialog.add_filter(media)
        if dialog.run() == Gtk.ResponseType.OK:
            choose(dialog.get_filename())
        dialog.destroy()
    button = Gtk.Button(label='选择图片 / 视频')
    button.connect('clicked', picker)
    row.pack_start(button, False, False, 0)
    button = Gtk.Button(label='恢复默认背景')
    button.connect('clicked', lambda *_: (save(kind, path=ROOT / 'resources' / 'backgrounds' / ('moon-garden.png' if kind == 'translation' else 'morning-ripple.png')), update()))
    row.pack_start(button, False, False, 0)
    sound = Gtk.CheckButton(label='播放视频声音')
    sound.set_active(preferences(kind)[1])
    sound.connect('toggled', lambda entry: (save(kind, sound=entry.get_active()), update()))
    row.pack_start(sound, False, False, 0)
    window.drag_dest_set(Gtk.DestDefaults.ALL, [], Gdk.DragAction.COPY)
    window.drag_dest_add_uri_targets()
    def dropped(widget, context, x, y, data, info, timestamp):
        uris = data.get_uris()
        if uris and urlparse(uris[0]).scheme == 'file':
            choose(unquote(urlparse(uris[0]).path))
            Gtk.drag_finish(context, True, False, timestamp)
    window.connect('drag-data-received', dropped)
    application.editors.append(window)
    window.connect('destroy', lambda *_: application.editors.remove(window))
    window.show_all()
    return window


class CandidatePanel:
    def __init__(self):
        self.window = Gtk.Window(type=Gtk.WindowType.POPUP)
        self.window.set_accept_focus(False)
        self.window.set_focus_on_map(False)
        self.window.set_type_hint(Gdk.WindowTypeHint.TOOLTIP)
        self.window.set_keep_above(True)
        self.overlay = Gtk.Overlay()
        self.window.add(self.overlay)
        self.background = Background('candidates')
        self.overlay.add(self.background)
        self.label = Gtk.Label()
        self.label.set_xalign(0)
        self.label.set_line_wrap(True)
        self.label.set_max_width_chars(58)
        self.label.set_margin_start(16)
        self.label.set_margin_end(16)
        self.label.set_margin_top(12)
        self.label.set_margin_bottom(12)
        style = Gtk.CssProvider()
        style.load_from_data(b'label { color: #263f55; background: rgba(255,255,255,0.78); padding: 8px; border-radius: 10px; font-size: 14px; }')
        self.label.get_style_context().add_provider(style, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        self.overlay.add_overlay(self.label)
        self.window.set_default_size(540, 220)

    def show(self, text, cursor):
        self.label.set_text(text)
        self.window.show_all()
        self.window.resize(540, max(120, self.label.get_preferred_height()[1] + 30))
        x, y, _, height = cursor
        display = self.window.get_display()
        monitor = display.get_monitor_at_point(x, y)
        area = monitor.get_workarea() if monitor else None
        width, pane_height = self.window.get_size()
        if area:
            x = max(area.x, min(x, area.x + area.width - width))
            y = max(area.y, min(y - pane_height - 44, area.y + area.height - pane_height))
        self.window.move(x, y)

    def hide(self):
        self.window.hide()

    def close(self):
        self.window.destroy()
