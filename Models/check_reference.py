"""Offline reference parameter, flash journal, reboot and provisioning tests."""
import datetime,hashlib,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cc=os.environ.get('HOST_CC','gcc')
names=['st_core','st_logger','st_link2','st_ignition','st_auto_link','st_nv','st_pairing','st_journal','st_reference','st_cal','st_console']
sources=[ROOT/'Acquisition/src'/f'{n}.c' for n in names]+[ROOT/'Acquisition/vendor/tiny_aes/aes.c',ROOT/'Models/test_reference.c']
with tempfile.TemporaryDirectory(prefix='swift-ref-host-') as d:
 exe=Path(d)/'reference.exe'
 cmd=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-Wconversion','-Wshadow',
  '-I',str(ROOT/'Acquisition/include'),'-I',str(ROOT/'Acquisition/vendor/semtech_sx128x'),
  *map(str,sources),'-o',str(exe)]
 subprocess.run(cmd,check=True,capture_output=False,timeout=60)
 result=subprocess.check_output([str(exe)],text=True,timeout=60)
 lines=[datetime.datetime.now(datetime.timezone.utc).isoformat(),result.strip(),
  'Synthetic flash model only; no MCU/TF/RF/electrical/power-loss hardware test.',
  'Strict C11 warnings including conversion/shadow are errors.']
 for p in sources:lines.append('SHA256 '+p.relative_to(ROOT).as_posix()+' '+hashlib.sha256(p.read_bytes()).hexdigest())
 (ROOT/'Models/reference_host_validation.log').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 print(result.strip())
