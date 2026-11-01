"""Execute actual F411 GPIO/SPI/EXTI/NV service functions with bounded HAL peers.

Only definitions and complete functions are extracted; production control flow
is not copied into the test. This checks board routing and software ordering,
not physical pin levels, DMA timing, Flash stalls, or NVIC electrical behavior.
"""
import re
import subprocess
import tempfile
from pathlib import Path

from check_clock_fault import extract_function
from local_tools import executable

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "Acquisition/board/reference_main.c").read_text()
    definitions = re.findall(r"^#define ACQ_[^\n]*(?:\\\n[^\n]*)*", source, re.MULTILINE)
    assert definitions, "Missing board pin definitions"
    led_start = source.index("static const st_f4_led_port leds =")
    definitions.append(source[led_start:source.index(";", led_start) + 1])
    functions = [extract_function(source, signature) for signature in (
        "static void gpio(GPIO_TypeDef *port",
        "static void irq(IRQn_Type n",
        "void HAL_SPI_MspInit(SPI_HandleTypeDef *h)",
        "static void spi_init(SPI_HandleTypeDef *h",
        "static void board_init(void)",
        "static bool present(void *ctx)",
        "static bool Acquisition_NvIdle(void *ctx)",
        "static void fast_service(void)\n{",
        "void EXTI15_10_IRQHandler(void)",
        "void EXTI9_5_IRQHandler(void)",
        "void HAL_GPIO_EXTI_Callback(uint16_t pin)")]
    with tempfile.TemporaryDirectory(prefix="swift-board-") as directory:
        temporary = Path(directory)
        (temporary / "acquisition_board_functions.inc").write_text(
            "\n".join(definitions + functions))
        exe = temporary / "test.exe"
        command = [str(executable("HOST_CC", "gcc")), "-std=c11", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow",
                   "-I", str(temporary), str(ROOT / "Models/test_acquisition_board.c"),
                   "-o", str(exe)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
