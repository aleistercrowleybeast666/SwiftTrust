"""Execute the actual board CSS/NMI callbacks with bounded interface stubs.

This verifies callback dispatch and sticky state, not oscillator hardware.
The actual pulse/capture helpers are executed separately by their driver models.
"""
import subprocess
import tempfile
from pathlib import Path

from local_tools import executable

ROOT = Path(__file__).resolve().parents[1]


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (ROOT / "Acquisition/board/reference_main.c").read_text()
    callbacks = "\n".join(extract_function(source, signature) for signature in (
        "void HAL_RCC_CSSCallback(void)", "void NMI_Handler(void)"))
    harness = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
static volatile bool clock_fault, clock_ready, source_fault;
static struct { _Atomic(bool) capture_enabled; } logger;
static int capture;
static unsigned checks, cutoffs, fences, errors, irq_disables, dispatches;
static bool css_pending;
#define EXTI4_IRQn 10
#define C(x) do { ++checks; if (!(x)) { fprintf(stderr, "CSS line%d: %s\n", __LINE__, #x); exit(1); } } while (0)
void IgnitionPulse_FaultIsr(void) { ++cutoffs; }
void HAL_NVIC_DisableIRQ(int irq) { C(irq == EXTI4_IRQn); ++irq_disables; }
void Capture_ClockFaultIsr(void *port) { C(port == &capture); ++fences; }
void st_logger_source_error_isr(void *owner) { C(owner == &logger); ++errors; }
void HAL_RCC_CSSCallback(void);
/* Mirror the official HAL's CSS flag dispatch/clear contract. */
void HAL_RCC_NMI_IRQHandler(void)
{
    ++dispatches;
    if (css_pending)
    {
        HAL_RCC_CSSCallback();
        css_pending = false;
    }
}
''' + callbacks + r'''
int main(void)
{
    clock_ready = true;
    atomic_init(&logger.capture_enabled, true);
    NMI_Handler();
    C(dispatches == 1u && !clock_fault && clock_ready && !source_fault);
    C(cutoffs == 0u && errors == 0u && atomic_load(&logger.capture_enabled));
    css_pending = true;
    NMI_Handler();
    C(dispatches == 2u && clock_fault && !clock_ready && source_fault);
    C(!atomic_load(&logger.capture_enabled) && !css_pending);
    C(cutoffs == 1u && fences == 1u && irq_disables == 1u && errors == 1u);
    css_pending = true;
    NMI_Handler();
    C(clock_fault && !clock_ready && source_fault && !atomic_load(&logger.capture_enabled));
    C(cutoffs == 2u && fences == 2u && irq_disables == 2u && errors == 1u);
    NMI_Handler();
    C(cutoffs == 2u && fences == 2u && errors == 1u && dispatches == 4u);
    printf("PASS actual board CSS/NMI callbacks: %u assertions; dispatch, immediate cutoff request, ADC fence, sticky source fault, capture disabled, one source error. Host interfaces only.\n", checks);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="swift-css-") as directory:
        source_path = Path(directory) / "css.c"
        executable_path = Path(directory) / "css.exe"
        source_path.write_text(harness)
        subprocess.run([str(executable("HOST_CC", "gcc")), "-std=c11", "-O2", "-Wall",
                        "-Wextra", "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow",
                        str(source_path), "-o", str(executable_path)], check=True, timeout=60)
        subprocess.run([str(executable_path)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
