"""Focused host regressions; no board, flashing, EDA, or radio access."""
import os
import datetime
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get('HOST_CC', 'gcc')

def main():
    reports = [datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'Host-only UI and battery regressions. No physical ADC, OLED or radio tested.']
    sources = sorted((ROOT/'Acquisition/src').glob('*.c'))
    sources += sorted((ROOT/'Controller/src').glob('*.c'))
    sources += [ROOT/'Common/Src/st_task.c', ROOT/'Acquisition/vendor/tiny_aes/aes.c',
                ROOT/'Acquisition/vendor/semtech_sx128x/sx128x.c',
                ROOT/'Models/test_r2.c', ROOT/'Models/test_battery.c']
    with tempfile.TemporaryDirectory(prefix='swift-ui-battery-') as directory:
        temp = Path(directory)
        driver = temp/'main.c'
        driver.write_text('#include <stdio.h>\n'
                          'unsigned st_test_r2(unsigned *);\n'
                          'unsigned st_test_battery(unsigned *);\n'
                          'int main(void) { unsigned g=0, n=st_test_r2(&g); '
                          'n+=st_test_battery(&g); '
                          'printf("PASS focused: %u groups, %u assertions\\n",g,n); return 0; }\n',
                          encoding='utf-8')
        vendor = sources.pop(sources.index(ROOT/'Acquisition/vendor/semtech_sx128x/sx128x.c'))
        obj = temp/'vendor.o'
        subprocess.run([CC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(vendor.parent), '-c', str(vendor), '-o', str(obj)], check=True)
        for language in (1, 0):
            for present, verified in ((0, 0), (1, 0), (0, 1), (1, 1)):
                exe = temp/'focused.exe'
                cmd = [CC, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                       '-Wconversion', '-Wshadow', '-finput-charset=UTF-8', '-fexec-charset=UTF-8',
                       f'-DST_UI_CHINESE={language}']
                cmd += [f'-DST_ACQ_BATTERY_PRESENT={present}',
                        f'-DST_ACQ_BATTERY_VERIFIED={verified}']
                for include in ('Acquisition/include', 'Controller/include', 'Acquisition/vendor/semtech_sx128x'):
                    cmd += ['-I', str(ROOT/include)]
                subprocess.run(cmd + list(map(str, sources)) + [str(driver), str(obj), '-o', str(exe)], check=True)
                description = f'CONFIG language={language} present={present} verified={verified}'
                print(description, flush=True)
                result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
                print(result.stdout, end='')
                reports += [description, result.stdout]
    (ROOT/'Models/ui_battery_validation.log').write_text('\n'.join(reports), encoding='utf-8')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
