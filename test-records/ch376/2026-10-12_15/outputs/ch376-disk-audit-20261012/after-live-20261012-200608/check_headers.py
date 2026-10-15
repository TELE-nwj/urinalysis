from pathlib import Path
import struct,json
p=Path(__file__).resolve().parent
b=(p/'boot-sector.bin').read_bytes()
bps=struct.unpack_from('<H',b,11)[0]
reserved=struct.unpack_from('<H',b,14)[0]
size=struct.unpack_from('<I',b,36)[0]
copies=b[16]
fat=(p/'fat-second.bin').read_bytes()
needle='时间(hh:mm:ss)'.encode('utf8')
entries=[]
offset=fat.find(needle)
while offset>=0:
 entries.append({'fat2_byte_offset':offset,'volume_sector':reserved+size+offset//bps,'text':fat[offset:offset+96].rstrip(b'\x00').decode('utf8','replace')})
 offset=fat.find(needle,offset+1)
report={'data_area_first_sector':reserved+copies*size,'fat2_first_sector':reserved+size,'fat_size_sectors':size,'headers_inside_fat2':entries}
(p/'misplaced-headers.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,ensure_ascii=False,indent=2))
