"""Offline English/Chinese translation. No network calls or input history."""
import re
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Translator:
    def __init__(self, direction='en-zh'):
        import ctranslate2
        import sentencepiece
        self.direction = direction
        folder = ROOT / 'models' / direction
        self.source = sentencepiece.SentencePieceProcessor(model_file=str(folder / 'source.spm'))
        self.target = sentencepiece.SentencePieceProcessor(model_file=str(folder / 'target.spm'))
        self.engine = ctranslate2.Translator(str(folder), device='cpu', compute_type='int8',
                                             inter_threads=1, intra_threads=2)
        self.lock = threading.Lock()

    def translate(self, text, cancelled=None):
        if len(text) > 8000:
            raise ValueError('每次最多 8000 个字符，请分段翻译。')
        output = []
        with self.lock:
            for line in text.splitlines(keepends=True):
                if cancelled and cancelled():
                    raise InterruptedError('翻译已取消')
                ending = '\n' if line.endswith(('\n', '\r')) else ''
                line = line.strip()
                if not line:
                    output.append(ending)
                    continue
                sentences = re.split(r'(?<=[。！？])|(?<=[.!?])\s+(?=[A-Z0-9"“])', line)
                translated = []
                for sentence in sentences:
                    if not sentence:
                        continue
                    pieces = self.source.encode(sentence, out_type=str)
                    for start in range(0, len(pieces), 384):
                        if cancelled and cancelled():
                            raise InterruptedError('翻译已取消')
                        prefix = ['>>cmn_Hans<<'] if self.direction == 'en-zh' else []
                        source = prefix + pieces[start:start + 384] + ['</s>']
                        result = self.engine.translate_batch([source], beam_size=4,
                                                             max_decoding_length=512,
                                                             repetition_penalty=1.1)[0]
                        translated.append(self.target.decode(result.hypotheses[0]))
                result = ('' if self.direction == 'en-zh' else ' ').join(translated)
                # OPUS's general corpus uses political senses for some software
                # terms. Apply corrections only when the source is technical.
                technical = bool(re.search(r'\b(app|software|server|api|request|token|github|function)\b', line, re.I))
                if self.direction == 'en-zh' and technical and re.search(r'\bstateless\b', line, re.I):
                    result = result.replace('无国籍', '无状态')
                if self.direction == 'en-zh' and technical and re.search(r'\btokens?\b', line, re.I):
                    result = result.replace('安装标记', '安装令牌').replace('访问标记', '访问令牌')
                output.append(result + ending)
        return ''.join(output)


def serve():
    translators = {}
    for line in sys.stdin:
        try:
            command, payload = line.rstrip('\r\n').split('\t', 1)
            if command not in ('translate', 'en-zh', 'zh-en'):
                raise ValueError('Unknown command')
            text = bytes.fromhex(payload).decode('utf-8')
            direction = 'en-zh' if command == 'translate' else command
            if direction not in translators:
                translators[direction] = Translator(direction)
            result = translators[direction].translate(text)
            print('ok\t' + result.encode('utf-8').hex(), flush=True)
        except Exception as error:
            print('error\t' + str(error).encode('utf-8').hex(), flush=True)


if __name__ == '__main__':
    serve()
