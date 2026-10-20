"""Focused packet-runtime regression. No SDK download, hardware or full suite rerun."""
import datetime,hashlib,os,subprocess,tempfile
from pathlib import Path
from local_tools import executable
R=Path(__file__).resolve().parents[1]
def main():
    cc=executable('HOST_CC','gcc');vendor=R/'Acquisition/vendor/semtech_sx128x'
    sources=[R/p for p in ['Acquisition/src/st_core.c','Acquisition/src/st_logger.c','Acquisition/src/st_link2.c','Acquisition/src/st_radio.c','Acquisition/src/st_link_runtime.c','Models/test_link_runtime.c']]
    lines=[datetime.datetime.now(datetime.timezone.utc).isoformat(),'Focused RF/protocol runtime regression on software event model only. No radio, GPIO, COM, ADC or card accessed.','Prior full43-group calibration baseline is not rerun or relabelled as this test.']
    with tempfile.TemporaryDirectory(prefix='swift-runtime-') as d:
        t=Path(d);obj=t/'vendor.o';exe=t/'runtime.exe'
        subprocess.run([str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(vendor),'-c',str(vendor/'sx128x.c'),'-o',str(obj)],check=True,capture_output=True)
        cmd=[str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic','-I',str(R/'Acquisition/include'),'-I',str(vendor),*[str(p) for p in sources],str(obj),'-o',str(exe)]
        run=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
        if run.returncode:print(run.stdout+run.stderr);return run.returncode
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        if run.returncode:print(run.stdout+run.stderr);return run.returncode
        lines.append(run.stdout.strip());lines.append('Source/header SHA256:')
        for p in sources+[vendor/'sx128x.c',R/'Acquisition/include/st_link_runtime.h',R/'Acquisition/include/st_radio.h',R/'Acquisition/include/st_link2.h']:
            lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+' '+p.relative_to(R).as_posix())
    output='\n'.join(lines)+'\n';(R/'Models/link_runtime_validation.log').write_text(output,encoding='utf-8',newline='\n');print(output);return 0
if __name__=='__main__':raise SystemExit(main())
