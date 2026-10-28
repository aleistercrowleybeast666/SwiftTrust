"""Compile actual ADS1220 transport/capture sources against a bounded HAL model."""
import subprocess
import tempfile
from pathlib import Path
from local_tools import executable

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="swift-ads1220-") as directory:
        exe = Path(directory) / "test.exe"
        sources = ["Acquisition/src/st_ads.c", "Acquisition/ports/st_ads_hal.c",
                   "Acquisition/ports/st_f4_capture.c", "Models/test_ads1220_capture.c"]
        command = [str(executable("HOST_CC", "gcc")), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow",
                   "-I", str(ROOT / "Models/ads1220_model"),
                   "-I", str(ROOT / "Acquisition/include"),
                   "-I", str(ROOT / "Acquisition/ports"),
                   *[str(ROOT / source) for source in sources], "-o", str(exe)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=10)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
