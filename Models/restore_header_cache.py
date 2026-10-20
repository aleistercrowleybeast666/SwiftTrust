"""Explicit restore of official ST inputs; verifies locally supplied CMSIS/FatFs."""
import hashlib,json,urllib.request,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 cache=Path(os.environ['SDK_ROOT']).resolve()
 manifest=json.loads((ROOT/'Models/official_header_manifest.json').read_text())
 total=0;missing=[]
 for e in manifest:
  p=(cache/e['path']).resolve()
  if not p.is_relative_to(cache):raise ValueError('cache path escape')
  if p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==e['sha256']:continue
  if not e.get('url'):missing.append(e['path']);continue
  if not e['url'].startswith('https://raw.githubusercontent.com/STMicroelectronics/'):raise ValueError('nonofficial source')
  with urllib.request.urlopen(e['url'],timeout=25) as r:data=r.read(e['bytes']+1)
  if len(data)!=e['bytes'] or hashlib.sha256(data).hexdigest()!=e['sha256']:raise ValueError('Upstream changed; inspect before accepting '+e['path'])
  p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);total+=len(data)
 print('Downloaded bytes:',total)
 if missing:raise SystemExit('Supply matching licensed files in SDK_ROOT: '+', '.join(missing))
 print('All SDK inputs verified')
if __name__=='__main__':main()
