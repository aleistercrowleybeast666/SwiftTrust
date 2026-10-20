"""Strict host C tests; executable and synthetic logs live only in temporary storage."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
sys.dont_write_bytecode=True
from export_stlog import read_log,export

ROOT=Path(__file__).resolve().parents[1]
from local_tools import executable,sdk_root
GCC=executable('HOST_CC','gcc')

def png_preview(frames,target):
    # Directly visualize the C renderer's SSD1306 page bytes, no substituted UI.
    scale=3;w=128*2*scale;h=64*((len(frames)//1024+1)//2)*scale;raw=bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            panel=(y//(64*scale))*2+x//(128*scale)
            px=(x//scale)%128;py=(y//scale)%64
            bit=(frames[panel*1024+(py//8)*128+px]>>(py%8))&1
            raw.extend((220,240,255) if bit else (7,14,20))
    def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
    image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    target.write_bytes(image)

def main():
    reports=[datetime.datetime.now(datetime.timezone.utc).isoformat(), 'Host-only synthetic regression. No MCU/RF/SD/OLED hardware accessed.']
    with tempfile.TemporaryDirectory(prefix='swiftthrust-host-') as temporary:
        t=Path(temporary);exe=t/'tests.exe';fixture=t/'synthetic.stlog';frames=t/'ui.raw';calframes=t/'calibration.raw'
        sources=sorted((ROOT/'Acquisition/src').glob('*.c'))+sorted((ROOT/'Controller/src').glob('*.c'))+[ROOT/'Models/test_core.c',ROOT/'Models/test_lowlevel.c',ROOT/'Models/test_r2.c',ROOT/'Models/test_storage.c',ROOT/'Models/test_calibration.c',ROOT/'Acquisition/ports/st_diskio.c',ROOT/'Acquisition/ports/st_fatfs_worker.c']
        sdk=sdk_root()
        fatfs=sdk/'fatfs/src/ff.c';fobj=t/'fatfs.o'
        frun=subprocess.run([str(GCC),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'Acquisition/ports/fatfs_config'),'-I',str(fatfs.parent),'-c',str(fatfs),'-o',str(fobj)],capture_output=True,text=True,timeout=60)
        if frun.returncode:print(frun.stdout+frun.stderr);return frun.returncode
        vendor=ROOT/'Acquisition/vendor/semtech_sx128x/sx128x.c';vobj=t/'vendor.o'
        vrun=subprocess.run([str(GCC),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(vendor.parent),'-c',str(vendor),'-o',str(vobj)],capture_output=True,text=True,timeout=60)
        if vrun.returncode:print(vrun.stdout+vrun.stderr);return vrun.returncode
        reports.append('Official vendor source compiled separately with -Wall -Wextra -Werror; project code retains conversion/shadow checks; vendor remains unmodified.')
        sources.append(vendor) # included in source hashes, linked via its separate object
        cmd=[str(GCC),'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wpedantic','-Wconversion','-Wshadow',
             '-I',str(ROOT/'Acquisition/vendor/semtech_sx128x'),'-I',str(ROOT/'Acquisition/include'),'-I',str(ROOT/'Controller/include'),'-I',str(ROOT/'Acquisition/ports'),'-I',str(ROOT/'Acquisition/ports/fatfs_config'),'-isystem',str(fatfs.parent),*[str(p) for p in sources if p!=vendor],str(vobj),str(fobj),'-o',str(exe)]
        built=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
        reports+=['Compiler: '+subprocess.check_output([str(GCC),'--version'],text=True).splitlines()[0],
                  'Flags: -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow',built.stdout,built.stderr]
        if built.returncode:
            print('\n'.join(reports));return built.returncode
        run=subprocess.run([str(exe),str(fixture),str(frames),str(calframes)],capture_output=True,text=True,timeout=60)
        reports += [run.stdout,run.stderr]
        if run.returncode:
            print('\n'.join(reports));return run.returncode
        rows,report=read_log(fixture)
        assert len(rows)==1500 and report['clean_close'] and report['simulated'] and not report['calibrated']
        assert all(r['force_N']=='' and r['ch1_V']=='' for r in rows)
        exported=export(fixture,t/'synthetic.csv');assert exported==report
        try:export(fixture,t/'synthetic.csv')
        except FileExistsError:pass
        else:raise AssertionError('CSV overwrite not rejected')
        original=fixture.read_bytes();fixture.write_bytes(original[:-40])
        recovered,rep=read_log(fixture);assert len(recovered)==1499 and not rep['clean_close'] and rep['issue']=='truncated tail'
        damaged=bytearray(original);damaged[512+3*32+2]^=1;fixture.write_bytes(damaged)
        recovered,rep=read_log(fixture);assert len(recovered)==3 and not rep['clean_close'] and 'CRC' in rep['issue']
        damaged=bytearray(original);damaged[100]^=1;fixture.write_bytes(damaged)
        try:read_log(fixture)
        except ValueError:pass
        else:raise AssertionError('header CRC not rejected')
        calibrated=bytearray(original);calibrated[59]|=1;calibrated[58]=4
        struct.pack_into('<II',calibrated,28,12,1);struct.pack_into('<i',calibrated,36,100)
        struct.pack_into('<q',calibrated,40,1000000);struct.pack_into('<I',calibrated,508,zlib.crc32(calibrated[:508]))
        fixture.write_bytes(calibrated);values,rep=read_log(fixture)
        assert rep['calibrated'] and values[0]['force_N']=='0.156'
        calibrated[512+22]=1;struct.pack_into('<I',calibrated,512+28,zlib.crc32(calibrated[512:512+28]))
        fixture.write_bytes(calibrated);values,_=read_log(fixture);assert values[0]['force_N']==''
        reports+=['PASS Python exporter: clean/uncalibrated/calibrated/invalid-ADC/truncated-tail/record-CRC/header-CRC/exclusive-output',
                  'Preview panels: HOME, CAPTURE ONLY, STATUS, RESERVED COMBINED (synthetic values).']
        png_preview(frames.read_bytes(),ROOT/'Models/ui_host_preview.png')
        png_preview(calframes.read_bytes(),ROOT/'Models/calibration_ui_preview.png')
        reports.append('FatFs R0.12c external SDK SHA256: '+hashlib.sha256(fatfs.read_bytes()).hexdigest())
        reports.append('Source SHA256:')
        for p in sources+sorted((ROOT/'Acquisition/include').glob('*.h'))+sorted((ROOT/'Controller/include').glob('*.h')):
            reports.append(hashlib.sha256(p.read_bytes()).hexdigest()+' '+p.relative_to(ROOT).as_posix())
    output='\n'.join(x for x in reports if x.strip())+'\n'
    (ROOT/'Models/firmware_host_validation.log').write_text(output,encoding='utf-8')
    print(output)
    return 0

if __name__=='__main__':raise SystemExit(main())
