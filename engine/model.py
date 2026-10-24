"""Pinned, integrity-checked downloads; inference never accesses the network."""
import hashlib
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort
from voxlock.core import VoxLock, PRECISIONS, REPO, REVISION
from .settings import ROOT, atomic_json

def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''): h.update(part)
    return h.hexdigest()

def setup_models():
    from huggingface_hub import HfApi, hf_hub_download
    root = ROOT / 'models'
    info = HfApi().model_info(REPO, revision=REVISION, files_metadata=True)
    manifest = {'revision': REVISION, 'files': {}}
    wanted = {'onnx/enroll.onnx', 'onnx/chunk_int8.onnx', 'onnx/chunk_int8_full.onnx'}
    for entry in info.siblings:
        if entry.rfilename not in wanted: continue
        path = Path(hf_hub_download(REPO, entry.rfilename, revision=REVISION, local_dir=root))
        sha = digest(path)
        expected = entry.lfs.sha256 if entry.lfs else None
        if not expected or sha != expected: raise RuntimeError(f'Model integrity check failed: {entry.rfilename}')
        manifest['files'][path.name] = sha
    if len(manifest['files']) != 3: raise RuntimeError('Official model repository is missing required files')
    atomic_json(root / 'manifest.json', manifest)
    return manifest

def load_model(settings):
    root = ROOT / 'models'
    manifest = json.loads((root / 'manifest.json').read_text())
    if manifest['revision'] != REVISION: raise RuntimeError('Model revision mismatch; run setup')
    names = PRECISIONS[settings.model]
    paths = [root / 'onnx' / (n + '.onnx') for n in names]
    for p in paths:
        if digest(p) != manifest['files'].get(p.name): raise RuntimeError(f'Corrupted model: {p.name}; run setup')
    options = ort.SessionOptions()
    options.intra_op_num_threads = settings.threads
    options.inter_op_num_threads = 1
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.add_session_config_entry('session.intra_op.allow_spinning', '0')
    options.add_session_config_entry('session.inter_op.allow_spinning', '0')
    # Use the inspected upstream Stream implementation, with explicitly configured sessions.
    model = VoxLock.__new__(VoxLock)
    model.enroll, model.chunk = [ort.InferenceSession(str(p), options, providers=['CPUExecutionProvider']) for p in paths]
    return model

def validate_enrollment(x):
    x = np.asarray(x, dtype=np.float32)
    if x.ndim != 1 or len(x) < 40000 or not np.isfinite(x).all(): raise ValueError('Record at least 2.5 seconds of clean speech')
    if np.max(np.abs(x)) >= .995: raise ValueError('Recording clips; lower microphone gain and rerecord')
    frames = x[:len(x)//160*160].reshape(-1,160)
    energy = np.sqrt(np.mean(frames**2, axis=1))
    if np.mean(energy > .003) < .2: raise ValueError('Too little audible speech; move closer and rerecord')
    return x
