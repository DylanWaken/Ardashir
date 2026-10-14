"""Explicitly capture the evidence files from the inspected local source tree.

Run after reviewing source changes. CI checks the frozen manifest; it never
regenerates evidence from a different source revision automatically.
"""
import hashlib
import json
import subprocess
from build import ROOT, HERE, sources

records = sources(verify=True)
paths = sorted({record['path'] for record in records})
snapshot = {
    'title': 'Arda graph, Inductor and offline compiler source study',
    'snapshot_date': '2026-10-14',
    'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    'source_scope': 'Local working tree including uncommitted changes; not identical to base_commit or published source.',
    'hash_convention': 'Full-file SHA-256 hashes are over raw file bytes; evidence range hashes use UTF-8 decoded lines joined with LF and no trailing LF.',
    'evidence_ranges': len(records),
    'files': [{'path': path, 'sha256': hashlib.sha256((ROOT/path).read_bytes()).hexdigest()} for path in paths],
}
(HERE/'snapshot.json').write_text(json.dumps(snapshot, indent=2)+'\n', encoding='utf-8')
print(f'Captured {len(paths)} source files and {len(records)} evidence ranges')
