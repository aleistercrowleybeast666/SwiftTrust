"""Offline resource/analog math regression. Not a firmware, timing or hardware test."""
import csv
import os
import io
import json
import re
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DB = Path(os.environ.get("CUBEMX_ROOT",r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeMX"))/"db/mcu"
NS = {"m": "http://mcd.rou.st.com/modules.php?name=mcu"}
PARTS = {
    "Acquisition": ("STM32F411RCT6", "STM32F411R(C-E)Tx.xml", "LQFP64"),
    "Controller": ("STM32G071KBT6", "STM32G071K(6-8-B)Tx.xml", "LQFP32"),
}


def read_csv(path):
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def pin_db(filename):
    root = ET.parse(DB / filename).getroot()
    return {p.get("Name"): (p.get("Position"), {s.get("Name") for s in p.findall("m:Signal", NS)})
            for p in root.findall("m:Pin", NS)}


def validate_pins(rows, pins):
    seen_positions, seen_exti = set(), set()
    for row in rows:
        pin, signal = row["pin"], row["signal"]
        if pin not in pins:
            raise ValueError(f"Pin not present in exact package: {pin}")
        position, signals = pins[pin]
        if str(position) != row["package_position"]:
            raise ValueError(f"Physical pin mismatch: {pin}")
        if position in seen_positions:
            raise ValueError(f"Duplicate physical position: {position}")
        seen_positions.add(position)
        if signal == "RESERVED":
            continue
        if signal.startswith("GPIO_") or signal.startswith("GPXTI"):
            if "GPIO" not in signals:
                raise ValueError(f"GPIO unavailable: {pin}")
        elif signal not in signals:
            raise ValueError(f"Alternate function unavailable: {pin}/{signal}")
        if signal.startswith(("GPXTI", "GPIO_EXTI")):
            exti = int(re.search(r"\d+$", signal).group())
            if int(re.search(r"\d+", pin).group()) != exti or exti in seen_exti:
                raise ValueError("Invalid or conflicting EXTI line")
            seen_exti.add(exti)


def dma_db():
    root = ET.parse(DB / "IP/DMA-STM32F411_dma_v2_0_Modes.xml").getroot()
    streams, channels = {}, {}
    for e in root.iter():
        tag, name = e.tag.split("}")[-1], e.get("Name", "")
        if tag == "Mode" and re.fullmatch(r"DMA[12]_Stream\d", name):
            streams[name] = {m.get("Name") for m in e.iter() if m.tag.split("}")[-1] == "Mode"}
        if tag == "RefMode":
            values = set()
            for param in e:
                if param.get("Name") == "Channel":
                    values.update(p.text for p in param if p.text)
            if values:
                channels[name] = values
    return streams, channels


def validate_dma(rows, mapping):
    streams, channels = mapping
    used = set()
    for row in rows:
        stream = f'{row["controller"]}_Stream{row["stream"]}'
        mode = row["mode_name"]
        if stream in used:
            raise ValueError("Exclusive DMA stream conflict")
        used.add(stream)
        if mode not in streams.get(stream, set()):
            raise ValueError(f"DMA request not mapped: {stream}/{mode}")
        if f'DMA_CHANNEL_{row["channel"]}' not in channels.get(mode, set()):
            raise ValueError(f"Wrong DMA channel: {mode}")


def read_ioc(path):
    return dict(line.split("=", 1) for line in path.read_text(encoding="utf-8").splitlines()
                if "=" in line and not line.startswith("#"))


def validate_ioc(board, rows):
    part, _, package = PARTS[board]
    path = ROOT / board / f"SwiftThrust_{board}_Candidate" / f"SwiftThrust_{board}_Candidate.ioc"
    values = read_ioc(path)
    if values.get("Mcu.CPN") != part or values.get("Mcu.Package") != package:
        raise ValueError(f"{board}: wrong exact device or package")
    constants = dict(item.split(",", 1) for item in values.get("Mcu.UserConstants", "").split(";") if "," in item)
    if constants.get("SWIFTTHRUST_BOARD_BOUND") != "0":
        raise ValueError("Board must remain unbound")
    if board == "Acquisition":
        expected_constants = {"SWIFTTHRUST_ADC_AVDD_MV_CANDIDATE": "3300", "SWIFTTHRUST_ADC_DVDD_MV_CANDIDATE": "3300", "SWIFTTHRUST_BRIDGE_EXC_MV_CANDIDATE": "2500", "SWIFTTHRUST_CH2_SCALE_PPM_CANDIDATE": "400000"}
        if any(constants.get(k) != v for k, v in expected_constants.items()):
            raise ValueError("Analog candidate annotations not saved in CubeMX")
    for row in rows:
        signal, pin = row["signal"], row["pin"]
        actual = values.get(f"{pin}.Signal")
        if signal == "RESERVED":
            if actual:
                raise ValueError(f"Reserved pin unexpectedly enabled: {pin}/{actual}")
            continue
        # CubeMX may emit shared ADCx/TIM signals with an explicit SH mapping.
        shared = any(k.startswith(f"SH.{actual}.") and (v == signal or v.startswith(signal + ","))
                     for k, v in values.items()) if actual else False
        if actual != signal and not shared:
            raise ValueError(f"{board}: saved .ioc mismatch {pin}: {actual!r}, expected {signal!r}")
        if signal.startswith("GPIO_EXTI") and not shared:
            raise ValueError("EXTI shared signal is not bound to the actual GPIO interrupt")
        if row["role"] in ("SWDIO", "SWCLK") and values.get(f"{pin}.Mode") != "Serial_Wire":
            raise ValueError("SWD pin is not assigned to Serial_Wire mode")
        if values.get(f"{pin}.GPIO_Label") != "CAND_" + row["role"]:
            raise ValueError(f"{board}: saved pin role label does not match candidate table: {pin}")
        if row["status"] == "idle_high_candidate" and values.get(f"{pin}.PinState") != "GPIO_PIN_SET":
            raise ValueError(f"Candidate CS/reset idle state missing: {pin}")
    if board == "Acquisition":
        if values.get("SPI1.CLKPhase") != "SPI_PHASE_2EDGE":
            raise ValueError("ADS SPI must be mode 1")
        if values.get("SPI1.Mode") != "SPI_MODE_MASTER" or values.get("SPI2.Mode") != "SPI_MODE_MASTER":
            raise ValueError("SPI master configuration missing")
        for pin in ["PC8", "PC9", "PC10", "PC11", "PC12", "PD2"]:
            if values.get(f"{pin}.Mode") != "SD_4_bits_Wide_bus":
                raise ValueError("SDIO four-bit mode missing")
    if board == "Controller":
        for signal in ["TIM2_CH1", "TIM2_CH2"]:
            if not any(v == signal + ",Encoder_Interface" for k, v in values.items() if k.startswith("SH.")):
                raise ValueError("Encoder pins are not assigned to Encoder_Interface")
    return path


def adc_input_fits(avdd, gain, positive, negative):
    if gain not in (1, 2, 4, 8, 16, 32, 64, 128) or not 2.7 <= avdd <= 3.6:
        return False
    ceiling = avdd if gain <= 4 else avdd - 1.8
    return (-1.3 <= positive <= ceiling and -1.3 <= negative <= ceiling
            and abs(positive - negative) <= 1.2 / gain)


def ordinary_rate(clock_hz, osr):
    # Applies only to normal continuous conversion with matching power mode;
    # not global-chop, paused, settling or physical oscillator accuracy.
    if clock_hz is None or osr is None:
        return None
    return clock_hz / (2 * osr)


class Regression(unittest.TestCase):
    def test_all_exact_package_pins(self):
        for board, (_, filename, _) in PARTS.items():
            validate_pins(read_csv(ROOT / board / "pin_candidates.csv"), pin_db(filename))

    def test_gp_pinout_rejects_n_substitution(self):
        rows = read_csv(ROOT / "Controller/pin_candidates.csv")
        with self.assertRaises(ValueError):
            validate_pins(rows, pin_db("STM32G071K(8-B)TxN.xml"))

    def test_duplicate_physical_pin_rejected(self):
        rows = read_csv(ROOT / "Acquisition/pin_candidates.csv")
        with self.assertRaises(ValueError):
            validate_pins(rows + [rows[0]], pin_db(PARTS["Acquisition"][1]))

    def test_invalid_alternate_function_rejected(self):
        rows = read_csv(ROOT / "Acquisition/pin_candidates.csv")
        rows[0]["signal"] = "SPI2_SCK"
        with self.assertRaises(ValueError):
            validate_pins(rows, pin_db(PARTS["Acquisition"][1]))

    def test_same_exti_line_on_different_ports_rejected(self):
        rows = read_csv(ROOT / "Acquisition/pin_candidates.csv")
        rows.append(dict(pin="PB4",package_position="56",signal="GPXTI4"))
        with self.assertRaises(ValueError):
            validate_pins(rows, pin_db(PARTS["Acquisition"][1]))

    def test_dma_plan_in_official_database(self):
        validate_dma(read_csv(ROOT / "Acquisition/dma_candidates.csv"), dma_db())

    def test_dma_stream_conflict_even_when_channel_differs(self):
        rows = read_csv(ROOT / "Acquisition/dma_candidates.csv")
        rows[1]["stream"], rows[1]["channel"] = rows[0]["stream"], "2"
        with self.assertRaises(ValueError):
            validate_dma(rows, dma_db())

    def test_dma_wrong_channel_rejected(self):
        rows = read_csv(ROOT / "Acquisition/dma_candidates.csv")
        rows[0]["channel"] = "0"
        with self.assertRaises(ValueError):
            validate_dma(rows, dma_db())

    def test_avdd_2v5_is_not_normal_supply(self):
        self.assertFalse(adc_input_fits(2.5, 1, 1.25, 1.25))

    def test_3v0_high_gain_bridge_common_mode_fails(self):
        self.assertFalse(adc_input_fits(3.0, 128, 1.2525, 1.2475))

    def test_3v3_nominal_bridge_math_only(self):
        self.assertTrue(adc_input_fits(3.3, 128, 1.2525, 1.2475))

    def test_differential_full_scale_still_limits(self):
        self.assertFalse(adc_input_fits(3.3, 128, 1.26, 1.24))

    def test_excitation_direct_input_fails(self):
        self.assertFalse(adc_input_fits(3.3, 1, 2.5, 0.0))

    def test_example_attenuation_math_only(self):
        self.assertTrue(adc_input_fits(3.3, 1, 2.5 * 0.4, 0.0))

    def test_requested_rate_is_not_a_clock_selection(self):
        self.assertEqual(ordinary_rate(8192000, 2048), 2000)
        self.assertEqual(ordinary_rate(8192000, 1024), 4000)
        self.assertIsNone(ordinary_rate(None, None))

    def test_pending_values_not_silently_selected(self):
        config = json.loads((ROOT / "Models/config_pending.json").read_text(encoding="utf8"))
        self.assertIs(config["board_bound"], False)
        for field in ["adc_clock_source", "adc_clock_hz", "adc_power_mode", "osr", "pga_ch0", "pga_ch1", "divider_ratio_ch1", "regulator", "charger", "rtos"]:
            self.assertIsNone(config["acquisition"][field], field)
        profile = config["radio"]["active_profile"]
        self.assertEqual(profile["scope"], "ReferenceApp_only_not_hardware_verified")
        self.assertEqual((profile["frequency_hz"], profile["sf"], profile["bandwidth_hz"]),
                         (2473000000, 10, 812500))
        reference = config["reference_application"]
        self.assertIs(reference["hardware_tested"], False)
        self.assertIs(reference["physical_board_bound"], False)
        self.assertEqual(reference["adc"]["pga_ch0"], 1)

    def test_executable_reference_pinout_and_reserved_pins(self):
        all_rows = read_csv(ROOT / "Models/reference_pins.csv")
        for board, (_, filename, _) in PARTS.items():
            pins = pin_db(filename)
            rows = [dict(row) for row in all_rows if row["board"] == board]
            if board == "Controller":
                for row in rows:
                    row["pin"] = {"PA11":"PA11 [PA9]", "PA12":"PA12 [PA10]"}.get(row["pin"],row["pin"])
            validate_pins(rows, pins)
            reserved = [row for row in rows if row["role"] == "STATIC_FUTURE_PLACEHOLDER"]
            self.assertEqual(len(reserved), 2)
            self.assertTrue(all(row["signal"] == "RESERVED" for row in reserved))

    def test_actual_cube_saved_pinout_matches_plan(self):
        for board in PARTS:
            validate_ioc(board, read_csv(ROOT / board / "pin_candidates.csv"))

    def test_ignition_single_pulse_saved_in_cube(self):
        values = read_ioc(ROOT / "Acquisition/SwiftThrust_Acquisition_Candidate/SwiftThrust_Acquisition_Candidate.ioc")
        self.assertEqual(values["TIM2.Prescaler"], "15")
        self.assertEqual(values["TIM2.Period"], "1000000") # simulation candidate; hardware disabled
        self.assertEqual(values["TIM2.Pulse"], "1")
        self.assertEqual(values["TIM2.OCMode_PWM"], "TIM_OCMODE_PWM2")
        self.assertEqual(values["VP_TIM2_VS_OPM.Mode"], "OPM_bit")
        self.assertEqual(values["ADC1.SamplingTime-0\\#ChannelRegularConversion"], "ADC_SAMPLETIME_480CYCLES")

    def test_r8_link_led_is_free_gpio_and_saved_low(self):
        for board, (_, filename, _) in PARTS.items():
            rows=read_csv(ROOT/board/'pin_candidates.csv')
            led=[r for r in rows if r['role']=='LINK_LED']
            self.assertEqual(len(led),1)
            self.assertEqual(led[0]['pin'],'PB0')
            self.assertEqual(led[0]['signal'],'GPIO_Output')
            validate_pins(rows,pin_db(filename))
            values=read_ioc(ROOT/board/f'SwiftThrust_{board}_Candidate'/f'SwiftThrust_{board}_Candidate.ioc')
            self.assertEqual(values['PB0.PinState'],'GPIO_PIN_RESET')

    def test_analog_candidate_stays_unverified_and_unbound(self):
        config = json.loads((ROOT / "Models/config_pending.json").read_text(encoding="utf8"))
        candidate = config["hardware_candidate"]
        self.assertEqual(candidate["status"], "recommended_candidate_not_board_verified")
        self.assertEqual(candidate["controller_exact_part"], "STM32G071KBT6")
        self.assertTrue(adc_input_fits(candidate["adc_avdd_v"], 128, candidate["bridge_excitation_v"] / 2 + 0.0025, candidate["bridge_excitation_v"] / 2 - 0.0025))
        self.assertIs(config["board_bound"], False)
        self.assertIsNone(candidate["ch2_divider_resistors"])
        self.assertIsNone(config["acquisition"]["pga_ch0"])

    def test_candidate_tolerance_budget_example(self):
        config = json.loads((ROOT / "Models/config_pending.json").read_text(encoding="utf8"))
        candidate = config["hardware_candidate"]
        avdd = candidate["high_gain_avdd_min_v_budget_example"]
        excitation = candidate["excitation_max_v_budget_example"]
        # Budget only: assumes symmetric bridge. Real common-mode offset is unknown.
        self.assertTrue(adc_input_fits(avdd, 128, excitation / 2 + 1.2 / 128 / 2, excitation / 2 - 1.2 / 128 / 2))
        self.assertTrue(adc_input_fits(avdd, candidate["ch2_pga_candidate"], excitation * candidate["ch2_effective_differential_scale_candidate"], 0))

    def test_replaceable_sensor_profile_does_not_claim_auto_identification(self):
        config = json.loads((ROOT / "Models/config_pending.json").read_text(encoding="utf8"))
        requirement = config["sensor_calibration_requirements"]
        self.assertIn("do_not_guess", requirement["automatic_identification"])
        self.assertIs(requirement["static_calibration_proves_dynamic_bandwidth"], False)
        for field in ["force_unit", "rated_sensitivity_mV_per_V", "actual_excitation_V", "zero_raw", "calibration_coefficients", "calibration_time", "calibration_validity"]:
            self.assertIn(field, requirement["profile_fields"])

    def test_voltage_leds_unbound_and_oled_exact_module(self):
        config = json.loads((ROOT / "Models/config_pending.json").read_text(encoding="utf8"))
        leds = config["acquisition"]["voltage_leds"]
        self.assertEqual(leds["count"], 4)
        self.assertIsNone(leds["thresholds_mv"])
        self.assertIs(leds["physical_output_enabled"], False)
        self.assertIs(leds["precise_SOC_claim"], False)
        rows = [row for row in read_csv(ROOT / "Acquisition/pin_candidates.csv") if row["role"].startswith("LED_VOLT_")]
        self.assertEqual([row["pin"] for row in rows], leds["pins_candidate"])
        self.assertTrue(all(row["signal"] == "GPIO_Input" for row in rows))
        display = config["controller"]["display"]
        self.assertEqual((display["layout_width"], display["layout_height"]), (128, 64))
        self.assertEqual(display["diagonal_inches"], 1.3)
        self.assertEqual(display["controller"], "SSD1306")
        self.assertIn("Common SSD1306", display["module"])
        self.assertEqual(display["address_7bit"], 0x3C)
        self.assertIs(display["hardware_verified"], False)
        self.assertEqual(config["controller"]["sram_budget_kib"], 32)

    def test_bsp_host_syntax_and_target_rejection(self):
        for board, header in [("Acquisition", "st_bsp.h"), ("Controller", "st_controller_bsp.h")]:
            command = [os.environ.get("HOST_CC","gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-x", "c", "-fsyntax-only", "-include", str(ROOT / board / "include" / header), "-"]
            source = b"int main(void) { return 0; }\n"
            host = subprocess.run(command, input=source, capture_output=True, timeout=10)
            self.assertEqual(host.returncode, 0, host.stderr.decode(errors="replace"))
            target = subprocess.run(command[:1] + ["-DST_TARGET_BUILD"] + command[1:], input=source, capture_output=True, timeout=10)
            self.assertNotEqual(target.returncode, 0)
            self.assertIn(b"UNBOUND", target.stderr)
            arm_marker = subprocess.run(command[:1] + ["-D__arm__"] + command[1:], input=source, capture_output=True, timeout=10)
            self.assertNotEqual(arm_marker.returncode, 0)
            self.assertIn(b"UNBOUND", arm_marker.stderr)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Regression))
    text = stream.getvalue()
    print(text)
    raise SystemExit(0 if result.wasSuccessful() else 1)
