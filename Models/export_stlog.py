"""Read STLOG001 valid prefix. Never invent calibration or force for invalid ADC data."""
import argparse
import csv
from decimal import Decimal
import struct
import zlib
from pathlib import Path

def read_log(path):
    data=Path(path).read_bytes()
    if len(data)<512 or data[:8]!=b'STLOG001':
        raise ValueError('missing STLOG001 header')
    u16=lambda offset:struct.unpack_from('<H',data,offset)[0]
    u32=lambda offset:struct.unpack_from('<I',data,offset)[0]
    if u16(8)!=512 or u16(10)!=32 or zlib.crc32(data[:508])!=u32(508):
        raise ValueError('invalid header layout/CRC')
    flags=data[59]
    if flags&~3:
        raise ValueError('unknown header flags')
    zero=struct.unpack_from('<i',data,36)[0]
    slope=struct.unpack_from('<q',data,40)[0]
    ch1scale=struct.unpack_from('<q',data,48)[0]
    calibrated=bool(flags&1)
    if calibrated and (not u32(28) or not u32(32) or not slope or data[58] not in (4,6)):
        raise ValueError('incomplete calibration metadata')
    rows=[];footer=None;offset=512;issue=None
    while offset+32<=len(data):
        r=data[offset:offset+32]
        if zlib.crc32(r[:28])!=struct.unpack_from('<I',r,28)[0]:
            issue=f'record CRC at byte {offset}';break
        seq,time_us,ch0,ch1,status,quality,missing=struct.unpack_from('<IQiiHHI',r)
        offset+=32
        if quality&0x8000:
            if quality!=0x8000:
                issue='unknown footer flags';break
            footer={'last_sequence':seq,'dropped':ch0&0xffffffff,'adc_errors':ch1&0xffffffff}
            if offset!=len(data):issue='bytes after footer'
            break
        if quality&~0x3f:
            issue=f'unknown record flags at byte {offset-32}';break
        valid=not quality&0x0f
        rows.append({'sequence':seq,'drdy_time_us':time_us,'raw_ch0':ch0,'raw_ch1':ch1,
                     'adc_status':f'0x{status:04X}','flags':f'0x{quality:04X}','missing_before':missing,
                     'force_N':str(Decimal(ch0-zero)*Decimal(slope)/Decimal(10**9)) if calibrated and valid else '',
                     'ch1_V':str(Decimal(ch1)*Decimal(ch1scale)/Decimal(10**9)) if ch1scale and valid else '',
                     'simulated':bool(flags&2)})
    if footer is None and issue is None:
        issue='truncated tail' if offset!=len(data) else 'no end footer (unclean stop)'
    return rows,{'samples':len(rows),'clean_close':footer is not None and issue is None,
                 'issue':issue,'footer':footer,'calibrated':calibrated,'simulated':bool(flags&2)}

def export(source,destination):
    rows,report=read_log(source)
    fields=['sequence','drdy_time_us','raw_ch0','raw_ch1','adc_status','flags','missing_before','force_N','ch1_V','simulated']
    # Exclusive output creation: never overwrite an existing CSV.
    with Path(destination).open('x',encoding='utf-8',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();print(export(args.source,args.destination))
