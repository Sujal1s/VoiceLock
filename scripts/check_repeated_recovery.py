import json
from pathlib import Path
rows=[json.loads(x) for x in Path('reports/repeated-overload-recovery.jsonl').read_text().splitlines()]
assert len(rows)==30 and all(r['running'] and not r['error'] for r in rows)
assert rows[-1]['recoveries']>=8 and not rows[-1]['muted']
assert any('Repeated processing stalls' in r['warning'] for r in rows)
assert max(r['capture_queue_ms'] for r in rows)<=240
print(json.dumps({'test':'passed','seconds':len(rows),'final':rows[-1]},indent=2))
