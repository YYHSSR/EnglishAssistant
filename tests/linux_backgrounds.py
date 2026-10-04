"""Real image/GIF/video decode and pause checks; private Xvfb only."""
import tempfile
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'linux'))
import backgrounds as bg
from gi.repository import Gtk, GLib, Gst


def pump(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        time.sleep(.01)


with tempfile.TemporaryDirectory() as directory:
    bg.CONFIG = Path(directory)
    gif = Path(directory) / 'animated.gif'
    header = b'GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff'
    loop = b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
    descriptor = b'\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00'
    frame = b'\x21\xf9\x04\x00\x0a\x00\x00\x00' + descriptor
    gif.write_bytes(header + loop + frame + b'\x02\x02\x44\x01\x00' + frame + b'\x02\x02\x4c\x01\x00\x3b')
    bg.save('translation', path=gif)
    window = Gtk.Window()
    background = bg.Background('translation')
    window.add(background)
    window.show_all()
    pump(.15)
    assert not background.animation.is_static_image()
    assert background.timer is not None
    window.hide()
    assert background.timer is None
    window.destroy()
    video = Path(directory) / 'video.webm'
    pipeline = Gst.parse_launch('videotestsrc num-buffers=20 pattern=ball ! video/x-raw,width=160,height=96,framerate=10/1 ! vp8enc deadline=1 ! webmmux ! filesink location=' + str(video))
    pipeline.set_state(Gst.State.PLAYING)
    message = pipeline.get_bus().timed_pop_filtered(15 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
    assert message and message.type == Gst.MessageType.EOS
    pipeline.set_state(Gst.State.NULL)
    bg.save('translation', path=video, sound=False)
    window = Gtk.Window()
    background = bg.Background('translation')
    window.add(background)
    window.show_all()
    pump(1)
    assert background.frame is not None, background.failure
    assert not background.failure, background.failure
    window.hide()
    pump(.1)
    assert background.player.get_state(2 * Gst.SECOND)[1] == Gst.State.PAUSED
    assert background.timer is None
    window.destroy()
    print('PASS real GIF/video decoding, hidden-window pause and isolated preferences', flush=True)
