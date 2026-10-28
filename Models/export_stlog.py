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
    session_zero=None
    if data[60:64]==b'SZR1':
        session_zero={'calibration_zero_raw':struct.unpack_from('<i',data,64)[0],
                      'session_zero_raw':struct.unpack_from('<i',data,68)[0],
                      'samples':u32(72),'time_us':struct.unpack_from('<Q',data,76)[0]}
    adc={'model':'ADS131M02','channels':2,'raw_format':'signed24','clock_hz':u32(12),
         'rate_millihz':u32(20),'gain':data[56],'reference_uv':None,
         'channel_meanings':['historical CH0','historical CH1']}
    single=data[84:88]==b'ADC1'
    if single:
        if (u16(88)!=1 or u16(90)!=44 or u16(92)!=1220 or data[94:96]!=bytes((1,24)) or
            u32(16)!=0 or not u32(12) or not u32(96) or not u32(104) or
            data[112:114]!=bytes((3,2)) or data[114]>1 or data[115:120]!=bytes((1,2,1,0,0)) or
            u32(124)&~3 or data[57] or ch1scale or not data[56] or
            data[56]>128 or data[56]&(data[56]-1) or (data[114] and data[56]>4)):
            raise ValueError('invalid ADS1220 metadata')
        adc={'model':'ADS1220','channels':1,'raw_format':'signed24','clock_hz':u32(12),
             'rate_millihz':u32(20),'gain':data[56],'reference_uv':u32(96),
             'nominal_rate_millihz':u32(100),'conversion_cycles':u32(104),
             'measured_drdy_rate_millihz':u32(108) if u32(124)&2 else None,
             'theoretical_rate_hz':str(Decimal(u32(12))/Decimal(u32(104))),
             'mode':'Turbo continuous','input_mux':data[112],'pga_bypass':bool(data[114]),
             'excitation_uv':u32(120),'clock_verified':bool(u32(124)&1),
             'channel_meanings':['AIN1-AIN2 bridge differential']}
        if bool(u32(108))!=bool(u32(124)&2):
            raise ValueError('inconsistent measured rate validity')
    elif any(data[84:128]):
        raise ValueError('unknown ADC metadata extension')
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
        if single and (ch1!=-2147483648 or status or not -8388608<=ch0<=8388607):
            issue=f'invalid single-channel record at byte {offset-32}';break
        valid=not quality&0x0f
        rows.append({'sequence':seq,'drdy_time_us':time_us,'raw_ch0':ch0,'raw_ch1':'' if single else ch1,
                     'adc_status':'' if single else f'0x{status:04X}','flags':f'0x{quality:04X}','missing_before':missing,
                     'force_N':str(Decimal(ch0-zero)*Decimal(slope)/Decimal(10**9)) if calibrated and valid else '',
                     'ch1_V':str(Decimal(ch1)*Decimal(ch1scale)/Decimal(10**9)) if ch1scale and valid else '',
                     'adc_input_V':str(Decimal(ch0)*Decimal(adc['reference_uv'])/
                                       (Decimal(8388608)*Decimal(adc['gain'])*Decimal(1000000))) if single and valid else '',
                     'sample_valid':valid,
                     'simulated':bool(flags&2)})
    if footer is None and issue is None:
        issue='truncated tail' if offset!=len(data) else 'no end footer (unclean stop)'
    rates={'retained_record_rate_hz':None,'sequence_inferred_conversion_rate_hz':None,'valid_record_rate_hz':None}
    if len(rows)>1:
        span=rows[-1]['drdy_time_us']-rows[0]['drdy_time_us']
        sequence_span=(rows[-1]['sequence']-rows[0]['sequence'])&0xffffffff
        if span>0:
            rates['retained_record_rate_hz']=(len(rows)-1)*1000000/span
            rates['valid_record_rate_hz']=max(0,sum(r['sample_valid'] for r in rows)-1)*1000000/span
            if 0<sequence_span<0x80000000:
                rates['sequence_inferred_conversion_rate_hz']=sequence_span*1000000/span
    return rows,{'samples':len(rows),'clean_close':footer is not None and issue is None,
                 'issue':issue,'footer':footer,'calibrated':calibrated,'simulated':bool(flags&2),
                 'session_zero':session_zero,'adc':adc,'observed_rates':rates}

def export(source,destination):
    rows,report=read_log(source)
    fields=['sequence','drdy_time_us','raw_ch0','raw_ch1','adc_status','flags','missing_before','force_N','ch1_V','adc_input_V','sample_valid','simulated']
    # Exclusive output creation: never overwrite an existing CSV.
    with Path(destination).open('x',encoding='utf-8',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
    return report

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();print(export(args.source,args.destination))
