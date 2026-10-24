from dataclasses import asdict, dataclass
from pathlib import Path
import json
import os

ROOT = Path(os.environ.get('VOICELOCK_DATA', Path(os.environ.get('LOCALAPPDATA', '.')) / 'VoiceLock'))

@dataclass
class Settings:
    model: str = 'int8-full'
    threads: int = 2
    chunk_ms: int = 80
    input_device: str = ''
    output_device: str = ''
    input_rate: int = 48000
    output_rate: int = 48000
    gaming: bool = True
    gain: float = 1.0
    diagnostics: bool = False

    def validate(self):
        if self.model not in ('int8', 'int8-full'): raise ValueError('Unsupported model')
        if type(self.threads) is not int or self.threads not in range(1, 5): raise ValueError('Threads must be 1–4')
        if self.chunk_ms not in (40, 80): raise ValueError('Chunk must be 40 or 80 ms')
        if self.input_rate not in (16000, 44100, 48000, 96000) or self.output_rate not in (16000, 44100, 48000, 96000): raise ValueError('Unsupported device rate')
        if not 0 <= self.gain <= 2: raise ValueError('Gain must be 0–2')
        return self

    def save(self, path=None):
        self.validate()
        atomic_json(path or ROOT / 'settings.json', asdict(self))

    @classmethod
    def load(cls, path=None):
        path = Path(path or ROOT / 'settings.json')
        return cls(**json.loads(path.read_text())).validate() if path.exists() else cls()

def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2), encoding='utf-8')
    temp.replace(path)
