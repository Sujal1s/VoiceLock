"""Collect actual installed distribution license texts for release review."""
import importlib.metadata as m
import json
import sys
from pathlib import Path
root=Path('dist/VoiceLock/licenses');root.mkdir(parents=True,exist_ok=True)
inventory=[]
for d in m.distributions():
    name=d.metadata['Name'];files=[]
    for f in d.files or []:
        if any(s in str(f).lower() for s in ('license','copying','notice')):
            src=Path(d.locate_file(f))
            if src.is_file():
                out=root/name/str(f).replace('../','').replace('..\\','')
                out.parent.mkdir(parents=True,exist_ok=True)
                out.write_bytes(src.read_bytes());files.append(str(out.relative_to(root)))
    inventory.append(dict(name=name,version=d.version,license=d.metadata.get('License-Expression') or d.metadata.get('License'),files=files))
(root/'inventory.json').write_text(json.dumps(inventory,indent=2))
python_license=Path(sys.base_prefix)/'LICENSE.txt'
if python_license.exists(): (root/'Python-LICENSE.txt').write_bytes(python_license.read_bytes())
