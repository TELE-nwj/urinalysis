import json, struct
from pathlib import Path

p = Path(__file__).resolve().parent
b = (p/'boot-sector.bin').read_bytes()
u16 = lambda o: struct.unpack_from('<H',b,o)[0]
u32 = lambda o: struct.unpack_from('<I',b,o)[0]
bps, spc, reserved, nfats, fatsz = u16(11), b[13], u16(14), b[16], u32(36)
partition = json.loads((p/'partition.json').read_text(encoding='utf-8-sig'))
partition_sector = partition['Offset']//bps
result = {'read_only':True, 'geometry':{'sector_bytes':bps,'cluster_sectors':spc,'reserved_sectors':reserved,'fat_sectors':fatsz,'fat_copies':nfats,'hidden_sectors':u32(28),'actual_partition_sector':partition_sector,'ext_flags':u16(40),'fat1_volume_start':reserved,'fat2_volume_start':reserved+fatsz,'data_volume_start':reserved+nfats*fatsz}, 'matches':[],'root_entries':[], 'misplaced_directory_entries':[]}
for filename, base in [('fat-first.bin',reserved),('fat-second.bin',reserved+fatsz)]:
    data = (p/filename).read_bytes()
    for token in ['时间(hh:mm:ss)','00:01:00','5.5ml','330.0ml/h','U0000001TXT','U0000002TXT']:
        needle = token.encode('utf-8')
        cursor=0
        while True:
            at=data.find(needle,cursor)
            if at<0: break
            volume_sector=base+at//bps
            block=data[at//bps*bps:(at//bps+1)*bps]
            result['matches'].append({'region':filename,'token':token,'volume_sector':volume_sector,'physical_sector':partition_sector+volume_sector,'byte_in_sector':at%bps,'excerpt':block[:150].decode('utf-8',errors='replace')})
            cursor=at+len(needle)
    if filename=='fat-second.bin':
        for token in [b'U0000001TXT', b'U0000002TXT']:
            at=data.find(token)
            if at<0: continue
            row=data[at:at+32]
            cluster=(struct.unpack_from('<H',row,20)[0]<<16)|struct.unpack_from('<H',row,26)[0]
            result['misplaced_directory_entries'].append({'short_name':token.decode(),'volume_sector':base+at//bps,'byte_in_sector':at%bps,'cluster':cluster,'size':struct.unpack_from('<I',row,28)[0],'expected_data_volume_sector':reserved+nfats*fatsz+(cluster-2)*spc,'raw_hex':row.hex(' ')})
root=(p/'cluster-2.bin').read_bytes()
for at in range(0,len(root),32):
    row=root[at:at+32]
    if row[0]==0: break
    if row[0]==0xe5 or row[11]==0x0f: continue
    result['root_entries'].append({'short_name':row[:11].decode('ascii',errors='replace'),'attr':row[11],'cluster':(struct.unpack_from('<H',row,20)[0]<<16)|struct.unpack_from('<H',row,26)[0],'size':struct.unpack_from('<I',row,28)[0]})
a=(p/'fat-first.bin').read_bytes(); c=(p/'fat-second.bin').read_bytes()
result['fat_mismatch_sectors']=sum(a[i:i+bps]!=c[i:i+bps] for i in range(0,len(a),bps))
(p/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
