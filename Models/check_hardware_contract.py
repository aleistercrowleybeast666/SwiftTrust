"""Offline CAD/clock contract regressions; no board, probe or serial access."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import re
import unittest
from extract_epro_contract import extract, transform

ROOT = Path(__file__).resolve().parents[1]
ACQ = json.loads((ROOT/'Models/hardware_acquisition_20261029.json').read_text(encoding='utf8'))
CTRL = json.loads((ROOT/'Models/hardware_controller_20261029.json').read_text(encoding='utf8'))


def pin(board, reference, number):
    return next(p for p in board['pins'] if p['reference'] == reference and p['number'] == str(number))


def connected(board, ref, number, other, other_number):
    target = pin(board, other, other_number)
    return f"{other}.{other_number}:{target['name']}" in pin(board, ref, number)['connected_pins']


def clock_recipe(hse, m, n, p, q):
    vco_in = Fraction(hse, m)
    vco = vco_in * n
    core, domain = vco / p, vco / q
    # RM0383 PLLM/N/P/Q and DS10314 PLL/bus operating conditions.
    if not (2 <= m <= 63 and 50 <= n <= 432 and p in (2,4,6,8) and 2 <= q <= 15
            and 1000000 <= vco_in <= 2000000 and 100000000 <= vco <= 432000000
            and core <= 100000000 and core/2 <= 50000000 and domain == 48000000):
        raise ValueError('clock operating conditions violated')
    return core, domain


class HardwareContract(unittest.TestCase):
    def test_ads_series_resistors_and_single_differential_input(self):
        for mcu, resistor, adc in [(20,'R35',2),(21,'R36',1),(22,'R37',15),(23,'R38',16),(24,'R39',14)]:
            self.assertTrue(connected(ACQ,'MCU',mcu,resistor,2))
            self.assertTrue(connected(ACQ,resistor,1,'ADC',adc))
            component = next(c for c in ACQ['components'] if c['reference'] == resistor)
            self.assertEqual(component['part'], '0603WAF470JT5E.1')
        self.assertEqual(pin(ACQ,'ADC',10)['net_labels'], ['AF+'])
        self.assertEqual(pin(ACQ,'ADC',7)['net_labels'], ['AF-'])
        for number in [6,11]:
            self.assertEqual(len(pin(ACQ,'ADC',number)['connected_pins']),1)
        self.assertEqual(len(pin(ACQ,'MCU',25)['connected_pins']),1)

    def test_adc_supply_reference_and_clock(self):
        for number in [9,12]:
            self.assertEqual(pin(ACQ,'ADC',number)['net_labels'],['5V'])
        self.assertEqual(pin(ACQ,'ADC',13)['net_labels'],['3V3'])
        for number in [4,5,8]:
            self.assertEqual(pin(ACQ,'ADC',number)['net_labels'],['GND'])
        self.assertTrue(connected(ACQ,'MCU',41,'R16',1))
        self.assertTrue(connected(ACQ,'R16',2,'ADC',3))
        self.assertEqual(next(c['part'] for c in ACQ['components'] if c['reference']=='R16'),'0603WAF330JT5E.1')
        self.assertTrue(connected(ACQ,'MCU',5,'X1',1))
        self.assertTrue(connected(ACQ,'MCU',6,'X1',2))
        self.assertEqual(next(c['value'] for c in ACQ['components'] if c['reference']=='X1'),'8.192MHz')

    def test_swd_and_controller_uart_are_distinct(self):
        for board, dio, clk in [(ACQ,46,49),(CTRL,24,25)]:
            self.assertEqual(pin(board,'MCU',dio)['net_labels'],['DIO'])
            self.assertEqual(pin(board,'MCU',clk)['net_labels'],['CLK'])
            self.assertFalse(connected(board,'MCU',dio,'MCU',clk))
            self.assertEqual(pin(board,'SWD',1)['net_labels'],['NRST'])
            self.assertEqual(pin(board,'SWD',2)['net_labels'],['3V3'])
            self.assertEqual(pin(board,'SWD',3)['net_labels'],['GND'])
        self.assertEqual(pin(CTRL,'MCU',22)['net_labels'],['DBG_MT'])
        self.assertEqual(pin(CTRL,'MCU',23)['net_labels'],['DBG_MR'])
        for number in [19,21]:
            self.assertEqual(len(pin(CTRL,'MCU',number)['connected_pins']),1)

    def test_both_battery_dividers_and_led(self):
        for board, number, top, bottom, cap in [(ACQ,8,'R14','R15','C19'),(CTRL,10,'R7','R8','C7')]:
            self.assertTrue(connected(board,'MCU',number,top,1))
            self.assertTrue(connected(board,'MCU',number,bottom,2))
            self.assertTrue(connected(board,'MCU',number,cap,2))
            self.assertEqual(pin(board,top,2)['net_labels'],['VIN'])
            self.assertEqual(pin(board,bottom,1)['net_labels'],['GND'])
            for reference in [top,bottom]:
                self.assertEqual(next(c['part'] for c in board['components'] if c['reference']==reference),'0603WAF1002T5E.1')
        self.assertEqual(pin(ACQ,'MCU',29)['net_labels'],['LED_LINK'])
        self.assertEqual(pin(CTRL,'MCU',15)['net_labels'],['LED_LINK'])

    def test_clock_recipe_matches_actual_application(self):
        text = (ROOT/'Acquisition/board/reference_main.c').read_text(encoding='utf8')
        hal = (ROOT/'Acquisition/board/stm32f4xx_hal_conf.h').read_text(encoding='utf8')
        hse = int(re.search(r'#define HSE_VALUE (\d+)U',hal)[1])
        m,n,q = [int(re.search(r'o\.PLL\.PLL'+key+r' = (\d+);',text)[1]) for key in ['M','N','Q']]
        p = int(re.search(r'o\.PLL\.PLLP = RCC_PLLP_DIV(\d+)',text)[1])
        core, domain = clock_recipe(hse,m,n,p,q)
        self.assertEqual(core,96000000)
        self.assertEqual(domain,48000000)
        self.assertEqual(core/2,48000000)
        self.assertEqual(core/32,3000000) # ADC SPI1
        self.assertEqual(core/2/16,3000000) # radio SPI2
        self.assertEqual(domain/(2+2),12000000) # SDIO data clock
        self.assertEqual(domain/(118+2),400000) # HAL identification divider
        self.assertEqual(core/4,24000000) # ADC1 PC0/VREFINT clock
        self.assertEqual(core/(95+1),1000000) # TIM2/TIM5
        self.assertIn('PWR_REGULATOR_VOLTAGE_SCALE1',text)
        self.assertIn('HAL_RCC_ClockConfig(&c, FLASH_LATENCY_3)',text)
        self.assertIn('HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_HSE, RCC_MCODIV_2)',text)
        self.assertLess(text.index('SystemClock_Config();'),text.index('st_ads_init(&ads'))
        self.assertIn('if (!clock_ready',text)
        self.assertEqual(Fraction(hse,2),4096000)

    def test_runtime_clock_failure_has_strong_bounded_handoff(self):
        text=(ROOT/'Acquisition/board/reference_main.c').read_text(encoding='utf8')
        self.assertIn('HAL_RCC_EnableCSS();',text)
        self.assertIn('PWR_FLAG_VOSRDY',text)
        self.assertIn('void NMI_Handler(void)',text)
        callback=text.split('void HAL_RCC_CSSCallback(void)',1)[1].split('void NMI_Handler',1)[0]
        for action in ['clock_fault = true', 'clock_ready = false', 'IgnitionPulse_FaultIsr()',
                       'Capture_ClockFaultIsr(&capture)', 'logger.capture_enabled, false']:
            self.assertIn(action,callback)
        for forbidden in ['f_write(', 'f_sync(', 'HAL_SPI_Abort(', 'HAL_Delay(']:
            self.assertNotIn(forbidden,callback)
        self.assertIn('if (!clock_ready || clock_fault || !voltage_ok',text)

    def test_bad_clock_recipes_rejected(self):
        for recipe in [(8000000,8,375,4,8),(8192000,8,375,2,8),(8192000,4,375,4,8),(8192000,8,432,4,8)]:
            with self.assertRaises(ValueError):clock_recipe(*recipe)
        self.assertEqual(Fraction(4096000,2072),Fraction(512000,259))
        self.assertEqual(Fraction(2072*1000000,4096000),Fraction(64750,128))
        self.assertNotEqual(Fraction(2072*1000000,4096000),500)

    def test_rotation_mirror_and_electrical_pin_origin(self):
        p={'x':20,'y':0,'length':10,'rotation':180}
        self.assertEqual(transform(p,{'x':100,'y':200,'rotation':90}), (100,180))
        self.assertEqual(transform(p,{'x':100,'y':200,'rotation':90,'isMirror':True}), (100,220))
        self.assertEqual(transform(p,{'x':100,'y':200,'rotation':0}), (120,200))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path)
    args=parser.parse_args()
    if args.source_dir:
        for board, saved in [('Acquisition',ACQ),('Controller',CTRL)]:
            actual=json.loads(json.dumps(extract(args.source_dir/f'ProDoc_{board}_1_2026-10-29.epro2')))
            if actual != saved:raise AssertionError(f'{board} extraction differs from saved evidence')
        print('PASS fresh extraction matches both checked-in CAD snapshots and SHA256')
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(HardwareContract))
    raise SystemExit(0 if result.wasSuccessful() else 1)
