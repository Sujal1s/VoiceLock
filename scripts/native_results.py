"""Summarize retained native test counters; does not record or infer new audio."""
import json
from pathlib import Path

summary={}
for name in ('native-live-60s','native-live-90s-4threads','native-clean-120s','native-final-80ms-cushion','native-release-60s'):
    path=Path('reports')/(name+'.jsonl')
    if not path.exists():continue
    rows=[json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if not rows:continue
    summary[name]={
        'observations':len(rows),
        'final':rows[-1],
        'mean_cpu_percent':sum(r['cpu_percent'] for r in rows)/len(rows),
        'max_queue_ms':max(r['queue_ms'] for r in rows),
        'min_ram_mb':min(r['ram_mb'] for r in rows),
        'max_ram_mb':max(r['ram_mb'] for r in rows),
        'note':'Initial implementation before playback-trim/scheduling corrections' if name=='native-live-60s' else '400 ms injected inference stall at second 11; intermediate buffering configuration' if '90s' in name or name=='native-final-80ms-cushion' else 'No deliberately injected fault; ordinary uncontrolled desktop workload'
    }
Path('reports/native-live-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({name:{'seconds':r['observations'],'running':r['final']['running'],'overruns':r['final']['overruns'],'underruns':r['final']['underruns'],'recoveries':r['final']['recoveries'],'rtf_p95':r['final']['rtf_p95'],'mean_cpu_percent':r['mean_cpu_percent']} for name,r in summary.items()},indent=2))
