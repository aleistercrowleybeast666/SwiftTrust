"""Execute the actual G071 encoder MSP and GPIO helper against recorded HAL calls.

This checks pin/AF/pull selection, peripheral identity and unrelated-pin isolation.
Existing host tests cover encoder count wrap/detents and button debounce.
No GPIO hardware, encoder electrical timing or switch bounce is simulated here.
"""
import subprocess
import tempfile
from pathlib import Path

from check_clock_fault import extract_function
from local_tools import executable

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "Controller/board/reference_main.c").read_text()
    functions = "\n".join(extract_function(source, signature) for signature in (
        "static void gpio(GPIO_TypeDef *port",
        "void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef *h)"))
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint32_t tag; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;
static GPIO_TypeDef gpio_a, gpio_b;
static int timer2, other_timer;
#define GPIOA (&gpio_a)
#define GPIOB (&gpio_b)
#define TIM2 (&timer2)
#define GPIO_PIN_0 (1u << 0)
#define GPIO_PIN_1 (1u << 1)
#define GPIO_PIN_5 (1u << 5)
#define GPIO_MODE_AF_PP 2u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_HIGH 2u
#define GPIO_AF2_TIM2 2u
static unsigned clock_calls, gpio_calls, checks;
static GPIO_TypeDef *last_port;
static GPIO_InitTypeDef last_gpio;
#define __HAL_RCC_TIM2_CLK_ENABLE() (++clock_calls)
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)
static void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *config)
{
    CHECK(clock_calls > 0u);
    ++gpio_calls;
    last_port = port;
    last_gpio = *config;
}
#include "controller_encoder_functions.inc"
int main(void)
{
    TIM_HandleTypeDef handle = {&other_timer};
    HAL_TIM_Encoder_MspInit(&handle);
    CHECK(clock_calls == 0u && gpio_calls == 0u);
    handle.Instance = TIM2;
    HAL_TIM_Encoder_MspInit(&handle);
    CHECK(clock_calls == 1u && gpio_calls == 1u);
    CHECK(last_port == GPIOA && last_port != GPIOB);
    CHECK(last_gpio.Pin == (GPIO_PIN_5 | GPIO_PIN_1));
    CHECK((last_gpio.Pin & GPIO_PIN_0) == 0u);
    CHECK(last_gpio.Mode == GPIO_MODE_AF_PP);
    CHECK(last_gpio.Pull == GPIO_PULLUP);
    CHECK(last_gpio.Speed == GPIO_SPEED_FREQ_HIGH);
    CHECK(last_gpio.Alternate == GPIO_AF2_TIM2);
    handle.Instance = &other_timer;
    HAL_TIM_Encoder_MspInit(&handle);
    CHECK(clock_calls == 1u && gpio_calls == 1u);
    printf("PASS actual G071 encoder MSP: %u checks; PA5/PA1 AF2 pullups, "
           "PA0 released, unrelated pins untouched, non-TIM2 handle ignored. Host HAL calls only.\n", checks);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="swift-controller-encoder-") as directory:
        temporary = Path(directory)
        (temporary / "controller_encoder_functions.inc").write_text(functions)
        test = temporary / "test.c"
        test.write_text(harness)
        exe = temporary / "test.exe"
        command = [str(executable("HOST_CC", "gcc")), "-std=c11", "-O2", "-Wall",
                   "-Wextra", "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow",
                   str(test), "-o", str(exe)]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(exe)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
