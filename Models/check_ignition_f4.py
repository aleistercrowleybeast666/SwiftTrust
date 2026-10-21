"""Execute the actual F4 pulse driver using a small register/clock facade."""
import datetime,hashlib,subprocess,tempfile
from pathlib import Path
from local_tools import executable
ROOT=Path(__file__).resolve().parents[1]
sources=[ROOT/'Acquisition/ports/st_ignition_f4.c',ROOT/'Models/test_ignition_f4.c']
with tempfile.TemporaryDirectory(prefix='swift-pulse-') as tmp:
 exe=Path(tmp)/'pulse.exe'
 subprocess.run([str(executable('HOST_CC','gcc')),'-std=c11','-O2','-Wall','-Wextra','-Werror',
  '-Wpedantic','-Wconversion','-Wshadow','-DST_REFERENCE_APPLICATION',
  '-I',str(ROOT/'Models/ignition_f4_model'),'-I',str(ROOT/'Acquisition/ports'),
  '-I',str(ROOT/'Acquisition/include'),*map(str,sources),'-o',str(exe)],check=True)
 result=subprocess.check_output([str(exe)],text=True,timeout=15)
 report=datetime.datetime.now(datetime.timezone.utc).isoformat()+'\nHost register model only; no electrical or hardware timing claim.\n'+result
 for p in sources+[ROOT/'Models/ignition_f4_model/stm32f4xx_hal.h']:
  report+='SHA256 '+p.relative_to(ROOT).as_posix()+' '+hashlib.sha256(p.read_bytes()).hexdigest()+'\n'
 (ROOT/'Models/ignition_pulse_validation.log').write_text(report)
 print(result,end='')
