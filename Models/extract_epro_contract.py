"""Read-only EasyEDA Pro snapshot extraction; never infer interior wire crossings.

Resolves tombstones, symbol pin origins, component rotations/mirroring, explicit
wire groups, coincident endpoints, and NET labels. Does not perform
ERC or claim CAD schema validation. Source archives stay outside the repository.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import zipfile


def documents(path):
    result = {}
    decoder = json.JSONDecoder()
    with zipfile.ZipFile(path) as archive:
        name, = [n for n in archive.namelist() if n.endswith('.epru')]
        for line in archive.read(name).decode('utf-8').splitlines():
            if '||' not in line:
                continue
            header, payload = line.split('||', 1)
            header = json.loads(header)
            value = decoder.raw_decode(payload)[0] if payload.startswith(('{', '[')) else None
            if header['type'] == 'DOCHEAD':
                doc = {'header': value, 'rows': {}}
                result[value['uuid']] = doc
            else:
                # The stored ticket is authoritative, including deleted rows.
                key = header['type'], header.get('id')
                old = doc['rows'].get(key)
                if old is None or header['ticket'] >= old[0]:
                    doc['rows'][key] = (header['ticket'], value)
    for doc in result.values():
        doc['rows'] = {k: v for k, (_, v) in doc['rows'].items() if v is not None}
    return result


def attrs(doc):
    out = defaultdict(dict)
    for (kind, _), value in doc['rows'].items():
        if kind == 'ATTR':
            out[value['parentId']][value['key']] = value['value']
    return out


def point(x, y):
    return round(x, 5), round(y, 5)


def transform(pin, component):
    # Pin x/y is the electrical connection origin; length points inward and
    # must NOT be added (PIN disk rotation differs from editor model by 180).
    x, y = pin['x'], pin['y']
    if component.get('isMirror'):
        x = -x
    angle = math.radians(component.get('rotation', 0))
    return point(component['x'] + x * math.cos(angle) + y * math.sin(angle),
                 component['y'] - x * math.sin(angle) + y * math.cos(angle))


def extract(path):
    docs = documents(path)
    parent = {}
    def find(x):
        parent.setdefault(x, x)
        if parent[x] != x:
            parent[x] = find(parent[x])
        return parent[x]
    def union(x, y):
        parent[find(x)] = find(y)
    pins, components, labels = [], [], []
    for page_id, doc in docs.items():
        if doc['header']['docType'] != 'SCH_PAGE':
            continue
        properties = attrs(doc)
        positions = defaultdict(list)
        for (kind, ident), value in doc['rows'].items():
            if (kind == 'LINE' and value.get('lineGroup')
                    and ('WIRE', value['lineGroup']) in doc['rows']):
                group = page_id, value['lineGroup']
                a = point(value['startX'], value['startY'])
                b = point(value['endX'], value['endY'])
                positions[a].append(group)
                positions[b].append(group)
            if kind == 'WIRE' and properties[ident].get('NET'):
                label = properties[ident]['NET']
                union((page_id, ident), ('net', label))
                labels.append(label)
            if kind != 'COMPONENT':
                continue
            prop = properties[ident]
            symbol = docs.get(prop.get('Symbol'))
            if not symbol:
                continue
            defaults = attrs(symbol)
            device = docs.get(prop.get('Device'))
            dev = dict(device['rows'].get(('META', 'META'), {})) if device else {}
            reference = prop.get('Designator') or ident
            components.append({'reference': reference, 'part': value.get('partId'),
                               'value': prop.get('Value') or prop.get('Name') or dev.get('title'),
                               'page': page_id, 'rotation': value.get('rotation', 0),
                               'mirror': value.get('isMirror', False)})
            for (pin_kind, pin_id), pin in symbol['rows'].items():
                if pin_kind != 'PIN' or (pin.get('partId') and value.get('partId')
                                        and pin['partId'] != value['partId']):
                    continue
                fields = defaults[pin_id]
                p = transform(pin, value)
                node = page_id, ident, pin_id
                positions[p].append(node)
                pins.append({'reference': reference, 'part': value.get('partId'),
                             'number': fields.get('Pin Number'), 'name': fields.get('Pin Name'),
                             'position': p, '_node': node})
                # Power symbols encode their global network on the component.
                label = prop.get('Global Net Name') or prop.get('NET')
                if label:
                    union(node, ('net', label))
                    labels.append(label)
        for p, nodes in positions.items():
            for node in nodes[1:]:
                union(nodes[0], node)
            # Electrical joins are explicit wire groups / coincident endpoints.
            # A pin that visually lies inside another segment is not sufficient:
            # EasyEDA may draw a pin body or a crossing without a junction.
    netnames = defaultdict(set)
    for label in labels:
        netnames[find(('net', label))].add(label)
    members = defaultdict(list)
    for pin in pins:
        members[find(pin['_node'])].append(f"{pin['reference']}.{pin['number']}:{pin['name']}")
    for pin in pins:
        node = find(pin.pop('_node'))
        pin['net_labels'] = sorted(netnames[node])
        pin['connected_pins'] = sorted(members[node])
    return {'source': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'method': 'snapshot symbols + electrical pin origins + coincident endpoints/wire groups + NET labels; no interior crossing or pin-body joins',
            'hardware_tested': False, 'components': components, 'pins': pins}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(extract(args.source), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
