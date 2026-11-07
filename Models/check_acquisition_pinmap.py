"""Check the requested firmware map and compare real epro2 SCH/PCB evidence.

Without --source, execute offline checker regressions. With --source, compare
that actual archive and exit 2 on any mismatch; never accept stale CAD as a pass.
This checks explicit net assignments, not copper connectivity, DRC or hardware.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

from extract_epro_contract import extract, extract_pcb

ROOT = Path(__file__).resolve().parents[1]
TARGET = json.loads((ROOT/'Models/acquisition_pin_assignment_20261101.json').read_text(encoding='utf8'))
OBSERVED = ROOT/'Models/hardware_acquisition_20261031_observed.json'


def compare(snapshot, target=TARGET):
    results = []
    schematic = snapshot['pins']
    boards = snapshot.get('pcb', {}).get('boards', [])
    for domain, pins in [('schematic', schematic)] + [
            ('pcb:' + board['uuid'], board['pads']) for board in boards]:
        for required in target['pins']:
            number = str(required['package_position'])
            found = [p for p in pins if p['reference'] == 'MCU' and p['number'] == number]
            matched = len(found) == 1
            actual = found[0] if matched else {}
            labels = actual.get('net_labels', []) if domain == 'schematic' else [actual.get('net', '')]
            members = actual.get('connected_pins', [])
            if required['anchor']:
                name = '.'.join(required['anchor'])
                matched &= any(m.split(':', 1)[0] == name for m in members)
            if required['net_label']:
                matched &= required['net_label'] in labels
            # Two MCU pads on the same active signal are a conflict, even if
            # both have the expected label. Blank PCB nets never connect pads.
            matched &= sum(m.startswith('MCU.') for m in members) == 1
            results.append({'domain': domain, 'role': required['role'],
                'expected_pin': required['pin'], 'package_position': number,
                'expected_anchor': required['anchor'], 'expected_net': required['net_label'],
                'actual_labels': labels, 'actual_members': members, 'match': bool(matched)})
        for required in target['released_pins']:
            number = str(required['package_position'])
            found = [p for p in pins if p['reference'] == 'MCU' and p['number'] == number]
            members = found[0].get('connected_pins', []) if len(found) == 1 else []
            labels = found[0].get('net_labels', []) if domain == 'schematic' and found else []
            net = found[0].get('net', '') if found else ''
            matched = len(found) == 1 and not labels and not net and all(
                m.split(':', 1)[0] == 'MCU.' + number for m in members)
            results.append({'domain': domain, 'role': 'RELEASED', 'expected_pin': required['pin'],
                            'package_position': number, 'match': matched,
                            'actual_members': members, 'actual_labels': labels or [net]})
        # HY2.0 pin order remains 1=3V3, 2=GND, 3=MCU RX, 4=MCU TX.
        for number, label in [('1', '3V3'), ('2', 'GND'), ('3', 'DBG_MR'), ('4', 'DBG_MT')]:
            found = [p for p in pins if p['reference'] == 'UART' and p['number'] == number]
            labels = (found[0].get('net_labels', []) if domain == 'schematic'
                      else [found[0].get('net', '')]) if len(found) == 1 else []
            results.append({'domain': domain, 'role': 'UART_CONNECTOR_' + number,
                            'expected_net': label, 'actual_labels': labels, 'match': label in labels})
    if not boards:
        results.append({'domain': 'pcb', 'role': 'PAD_NET_EVIDENCE', 'match': False})
    return {'source': snapshot['source'], 'sha256': snapshot['sha256'],
            'target_revision': target['revision'], 'hardware_tested': False,
            'scope': 'schematic electrical joins and explicit PCB PAD_NET; not copper/DRC',
            'status': 'MATCH' if all(r['match'] for r in results) else 'MISMATCH',
            'checks': len(results), 'mismatches': sum(not r['match'] for r in results),
            'results': results}


def synthetic_snapshot():
    """Clearly synthetic input for testing the checker itself, never delivery evidence."""
    pins, pads = [], []
    for r in TARGET['pins']:
        number = str(r['package_position'])
        members = ['MCU.' + number]
        if r['anchor']:
            members.append('.'.join(r['anchor']))
        labels = [r['net_label']] if r['net_label'] else []
        pins.append(dict(reference='MCU', number=number, net_labels=labels, connected_pins=members))
        pads.append(dict(reference='MCU', number=number,
                         net=r['net_label'] or 'synthetic_' + number, connected_pins=list(members)))
    for r in TARGET['released_pins']:
        p = dict(reference='MCU', number=str(r['package_position']), net_labels=[], connected_pins=[])
        pins.append(p)
        pads.append(dict(p, net=''))
    for number, label in [('1', '3V3'), ('2', 'GND'), ('3', 'DBG_MR'), ('4', 'DBG_MT')]:
        pins.append(dict(reference='UART', number=number, net_labels=[label], connected_pins=[]))
        pads.append(dict(reference='UART', number=number, net=label, connected_pins=[]))
    return {'source': 'SYNTHETIC_TEST_ONLY', 'sha256': '', 'pins': pins,
            'pcb': {'boards': [{'uuid': 'synthetic', 'pads': pads}]}}


class PinAssignmentTests(unittest.TestCase):
    def test_pcb_keyed_ids_ignore_deleted_owners_and_empty_nets(self):
        rows = {
            ('COMPONENT', 'm'): {'x': 1, 'y': 2, 'angle': -180},
            ('COMPONENT', 'r'): {'x': 3, 'y': 4},
            ('ATTR', 'ma'): {'parentId': 'm', 'key': 'Designator', 'value': 'MCU'},
            ('ATTR', 'ra'): {'parentId': 'r', 'key': 'Designator', 'value': 'R36'},
        }
        for owner, number, net in [('m', '55', 'ADC_SCK'), ('r', '2', 'ADC_SCK'),
                                    ('m', '43', ''), ('r', '1', ''), ('deleted', '1', 'ADC_SCK')]:
            rows['PAD_NET', json.dumps(['PAD_NET', owner, number, 'local'])] = {'padNet': net}
        with patch('extract_epro_contract.documents', return_value={
                'pcb': {'header': {'docType': 'PCB'}, 'rows': rows}}):
            board = extract_pcb(Path('synthetic.epro2'))['boards'][0]
        self.assertEqual(board['components'][0]['angle'], -180)
        self.assertEqual(len(board['pads']), 4)
        for pad in board['pads']:
            self.assertEqual(pad['connected_pins'], ['MCU.55', 'R36.2'] if pad['net'] else [])

    def test_target_matches_both_firmware_pin_tables(self):
        for path in ['Acquisition/pin_candidates.csv', 'Models/reference_pins.csv']:
            with (ROOT/path).open(encoding='utf8', newline='') as handle:
                rows = [r for r in csv.DictReader(handle) if r.get('board', 'Acquisition') == 'Acquisition']
            by_role = {r['role']: r for r in rows}
            for required in TARGET['pins']:
                actual = by_role[required['role']]
                self.assertEqual(actual['package_position'], str(required['package_position']))
                self.assertEqual(actual['signal'], required['signal'])
                self.assertEqual(re.match(r'P[A-Z]\d+', actual['pin'])[0], required['pin'])

    def test_assignment_has_no_duplicate_pad_or_exti(self):
        self.assertEqual(len(TARGET['pins']), len({p['package_position'] for p in TARGET['pins']}))
        exti = [p['signal'] for p in TARGET['pins'] if p['signal'].startswith('GPIO_EXTI')]
        self.assertEqual(sorted(exti), ['GPIO_EXTI15', 'GPIO_EXTI7'])

    def test_synthetic_matching_input(self):
        self.assertEqual(compare(synthetic_snapshot())['status'], 'MATCH')

    def test_both_schematic_and_pcb_must_match(self):
        for domain in ['pins', 'pcb']:
            source = synthetic_snapshot()
            pins = source['pins'] if domain == 'pins' else source['pcb']['boards'][0]['pads']
            pins[0]['connected_pins'] = ['MCU.55']
            result = compare(source)
            self.assertEqual(result['status'], 'MISMATCH')
            self.assertEqual(result['mismatches'], 1)

    def test_shorted_mcu_pads_are_rejected(self):
        source = synthetic_snapshot()
        source['pins'][0]['connected_pins'].append('MCU.21')
        self.assertEqual(compare(source)['mismatches'], 1)

    def test_missing_pcb_evidence_is_not_a_pass(self):
        source = synthetic_snapshot()
        del source['pcb']
        self.assertEqual(compare(source)['status'], 'MISMATCH')

    def test_released_pads_and_connector_order_checked(self):
        source = synthetic_snapshot()
        next(p for p in source['pins'] if p['reference']=='MCU' and p['number']=='43')['net_labels']=['DBG_MR']
        next(p for p in source['pins'] if p['reference']=='UART' and p['number']=='1')['net_labels']=['GND']
        self.assertEqual(compare(source)['mismatches'], 2)

    def test_observed_1031_source_is_not_claimed_final(self):
        observed = json.loads(OBSERVED.read_text(encoding='utf8'))
        self.assertEqual(observed['sha256'], '8d87e4746a4a5b9685725504f523048d1c0ec119e856c391fa2fd012c1e14e60')
        result = compare(observed)
        self.assertEqual(result['status'], 'MISMATCH')
        for board in observed['pcb']['boards']:
            self.assertTrue(all(not p['connected_pins'] for p in board['pads'] if not p['net']))

    def test_final_source_hash_and_all_schematic_pcb_assignments(self):
        final = json.loads((ROOT/'Models/hardware_acquisition_20261101_final.json').read_text(encoding='utf8'))
        self.assertTrue(TARGET['final_epro2_verified'])
        self.assertEqual(final['source'], TARGET['final_epro2_source'])
        self.assertEqual(final['sha256'], TARGET['final_epro2_sha256'])
        report = compare(final)
        self.assertEqual(report['status'], 'MATCH')
        self.assertEqual(report['mismatches'], 0)
        self.assertEqual(report['checks'], 84)

    def test_completed_1107_pcb_matches_firmware_and_saved_report(self):
        final = json.loads((ROOT/'Models/hardware_acquisition_20261107_final.json').read_text(encoding='utf8'))
        self.assertEqual(final['source'], 'ProDoc_Acquisition_1_2026-11-07.epro2')
        self.assertEqual(final['sha256'], '610b23aa5d4bedaa2fcd6038ca4c10a1d99fc2341033024915da307145c79be2')
        self.assertFalse(final['hardware_tested'])
        report = compare(final)
        saved = json.loads((ROOT/'Models/acquisition_pinmap_cad_comparison_20261107.json').read_text(encoding='utf8'))
        self.assertEqual(report, saved)
        self.assertEqual(report['status'], 'MATCH')
        self.assertEqual(report['checks'], 84)
        self.assertEqual(report['mismatches'], 0)

    def test_completed_1107_sensor_connectors_and_named_pads(self):
        final = json.loads((ROOT/'Models/hardware_acquisition_20261107_final.json').read_text(encoding='utf8'))
        self.assertEqual(len(final['pcb']['boards']), 1)
        pads = {(p['reference'], p['number']): p for p in final['pcb']['boards'][0]['pads']}
        schematic = {(p['reference'], p['number']): p for p in final['pins']}
        # The finished PCB intentionally changes the external sensor pin order.
        for reference in ['SENSOR_IN1', 'SENSOR_IN2']:
            for number, net in [('1', 'A-'), ('2', 'A+'), ('3', '5V'), ('4', 'GND')]:
                self.assertEqual(schematic[reference, number]['net_labels'], [net])
                self.assertEqual(pads[reference, number]['net'], net)
        checked = 0
        for key, pin in schematic.items():
            if key in pads and pin['net_labels']:
                self.assertIn(pads[key]['net'], pin['net_labels'], str(key))
                checked += 1
        self.assertEqual(checked, 217)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--snapshot', type=Path, help='Save a newly extracted snapshot without modifying history')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if not args.source:
        return 0 if unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(PinAssignmentTests)).wasSuccessful() else 1
    snapshot = extract(args.source)
    snapshot['pcb'] = extract_pcb(args.source)
    result = compare(snapshot)
    for path, value in [(args.snapshot, snapshot), (args.report, result)]:
        if path:
            if path == args.snapshot and path.exists():
                raise FileExistsError('Refusing to overwrite CAD history: ' + str(path))
            path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(f"{result['status']}: {result['source']} SHA256={result['sha256']}; "
          f"{result['mismatches']}/{result['checks']} SCH/PCB assignments differ")
    for row in result['results']:
        if not row['match']:
            print(row['domain'], row['role'], row.get('expected_pin', ''), row.get('actual_labels', []))
    return 0 if result['status'] == 'MATCH' else 2


if __name__ == '__main__':
    raise SystemExit(main())
