"""Separate local CPU worker; closing a window cancels its process."""
import subprocess
import threading
from core import ROOT


class Neural:
    def __init__(self):
        self.lock = threading.Lock()
        self.process = None
        self.cancelled = threading.Event()

    def translate(self, text):
        python = ROOT / 'runtime' / 'venv' / 'bin' / 'python'
        if not python.exists():
            raise RuntimeError('本地翻译运行库未安装，请重新运行 sh ./install.sh')
        with self.lock:
            if self.cancelled.is_set():
                raise RuntimeError('翻译已取消')
            process = subprocess.Popen([str(python), '-I', str(ROOT / 'translation' / 'model.py')],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            self.process = process
        try:
            request = 'translate\t' + text.encode('utf-8').hex() + '\n'
            output, _ = process.communicate(request.encode('ascii'), timeout=180)
            line = output.decode('ascii').splitlines()[0]
            status, encoded = line.split('\t', 1)
            result = bytes.fromhex(encoded).decode('utf-8')
            if status != 'ok':
                raise RuntimeError(result)
            return result
        except (IndexError, ValueError):
            raise RuntimeError('本地翻译模型启动失败，请检查 models 与 runtime。')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            with self.lock:
                self.process = None

    def close(self):
        self.cancelled.set()
        with self.lock:
            if self.process and self.process.poll() is None:
                self.process.kill()
