"""Execute the shared UART HAL and actual board IRQ/callbacks for both MCUs.

The HAL model records init/interrupt/retained-buffer calls. It does not model
electrical USART waveforms or the vendor HAL implementation.
"""
import subprocess
import tempfile
from pathlib import Path

from check_clock_fault import extract_function
from local_tools import executable

ROOT = Path(__file__).resolve().parents[1]


def main():
    for board, define, handler in (
            ("Acquisition", "STM32F411xE", "USART2_IRQHandler"),
            ("Controller", "STM32G071xx", "USART1_IRQHandler")):
        source = (ROOT / board / "board/reference_main.c").read_text()
        callbacks = "\n".join(extract_function(source, signature) for signature in (
            "void " + handler + "(void)",
            "void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)",
            "void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)",
            "void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)"))
        with tempfile.TemporaryDirectory(prefix="swift-uart-") as directory:
            temporary = Path(directory)
            (temporary / "board_uart_callbacks.inc").write_text(callbacks)
            exe = temporary / "test.exe"
            command = [str(executable("HOST_CC", "gcc")), "-std=c11", "-O2", "-Wall",
                       "-Wextra", "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow",
                       "-D" + define, "-DUART_MODEL_IRQ=" + handler,
                       "-I", str(temporary), "-I", str(ROOT / "Models/debug_uart_model"),
                       "-I", str(ROOT / "Acquisition/include"),
                       "-I", str(ROOT / "Acquisition/ports"),
                       str(ROOT / "Acquisition/ports/st_debug_uart_hal.c"),
                       str(ROOT / "Acquisition/src/st_debug.c"),
                       str(ROOT / "Models/test_debug_uart_hal.c"), "-o", str(exe)]
            subprocess.run(command, check=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
