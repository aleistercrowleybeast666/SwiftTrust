"""Run a reviewed CubeMX command script with bounded memory; never installs packs."""
import argparse
import os,tempfile
import subprocess
from pathlib import Path


def run(script, log):
    cube = Path(os.environ.get("CUBEMX_ROOT",r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX"))
    commands = script.read_text(encoding="utf-8").replace("${PROJECT_ROOT}",str(Path(__file__).resolve().parents[1]))
    forbidden = ("project generate", "generate code", "swmgr ", "login", "xcubedl", "xcopy")
    if any(line.strip().lower().startswith(forbidden) for line in commands.splitlines()):
        raise ValueError("This pinout-only runner refuses code generation, package or login operations")
    with tempfile.TemporaryDirectory(prefix="swift-cube-") as tmp:
        actual=Path(tmp)/script.name
        actual.write_text(commands,encoding="utf-8")
        try:
            result = subprocess.run(
                [str(cube / "jre/bin/java.exe"), "-Xmx384m", "-jar", str(cube / "STM32CubeMX.exe"), "-q", str(actual)],
                cwd=cube, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired as error:
            partial = (error.stdout or b"").decode("utf-8", errors="replace")
            log.write_text(partial + "\nCUBEMX_TIMEOUT=120s; no validation success claimed\n", encoding="utf-8")
            raise SystemExit("CubeMX timed out; partial diagnostic output saved to " + str(log)) from error
    lines = result.stdout.decode("utf-8", errors="replace").splitlines()
    # Keep commands/results and warnings relevant to validation, not routine startup logs.
    kept = [line for line in lines if "[INFO]" not in line and "[WARN]" not in line]
    text = "\n".join(kept) + f"\nCUBEMX_PROCESS_EXIT={result.returncode}\n"
    log.write_text(text, encoding="utf-8")
    print(text)
    # CubeMX can return process status 0 after a command returned KO.
    if result.returncode or any(line.strip() == "KO" or "Error: true" in line for line in kept):
        raise SystemExit("CubeMX command failed; inspect the log and actual .ioc")
    from validate_candidates import PARTS, ROOT, read_csv, validate_ioc
    if script.parent.name in PARTS:
        board = script.parent.name
        actual = validate_ioc(board, read_csv(ROOT / board / "pin_candidates.csv"))
        print(f"SAVED_IOC_MATCHES_PLAN={actual}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("script", type=Path)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    run(args.script.resolve(), args.log.resolve())
