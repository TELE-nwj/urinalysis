import struct,json,sys,ctypes
from ctypes import wintypes
from pathlib import Path
out=Path(__file__).resolve().parent
class ReadOnlyVolume:
 def __init__(self):
  self.win=ctypes.WinDLL('kernel32',use_last_error=True)
  self.win.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
  self.win.CreateFileW.restype=wintypes.HANDLE
  self.win.ReadFile.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.c_void_p]
  self.win.SetFilePointerEx.argtypes=[wintypes.HANDLE,ctypes.c_longlong,ctypes.POINTER(ctypes.c_longlong),wintypes.DWORD]
  self.win.CloseHandle.argtypes=[wintypes.HANDLE]
  self.handle=self.win.CreateFileW(r'\\.\H:',0x80000000,3,None,3,0,None)
  if self.handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
 def __enter__(self):return self
 def __exit__(self,*args):self.win.CloseHandle(self.handle)
 def seek(self,offset):
  position=ctypes.c_longlong()
  if not self.win.SetFilePointerEx(self.handle,offset,ctypes.byref(position),0):raise ctypes.WinError(ctypes.get_last_error())
 def read(self,size):
  buffer=ctypes.create_string_buffer(size);amount=wintypes.DWORD()
  if not self.win.ReadFile(self.handle,buffer,size,ctypes.byref(amount),None):raise ctypes.WinError(ctypes.get_last_error())
  return buffer.raw[:amount.value]
try:
 with ReadOnlyVolume() as drive:
  boot=drive.read(512)
  bps=struct.unpack_from('<H',boot,11)[0];spc=boot[13];reserved=struct.unpack_from('<H',boot,14)[0];nfats=boot[16]
  sectors=struct.unpack_from('<I',boot,32)[0];fat_sectors=struct.unpack_from('<I',boot,36)[0];root_cluster=struct.unpack_from('<I',boot,44)[0]
  assert boot[510:512]==b'\x55\xaa' and bps in (512,1024,2048,4096) and 0<spc<=128 and 0<nfats<=2 and fat_sectors>0
  fat_start=reserved*bps;fat_bytes=fat_sectors*bps;data_start=(reserved+nfats*fat_sectors)*bps
  assert fat_bytes<16*1024*1024
  drive.seek(fat_start);fat=drive.read(fat_bytes)
  (out/'boot-sector.bin').write_bytes(boot);(out/'fat-first.bin').write_bytes(fat)
  same=None
  if nfats==2:
   drive.seek(fat_start+fat_bytes);fat2=drive.read(fat_bytes);same=fat==fat2;(out/'fat-second.bin').write_bytes(fat2)
  count=(sectors-reserved-nfats*fat_sectors)//spc
  used=[(i,struct.unpack_from('<I',fat,i*4)[0]&0x0fffffff) for i in range(2,min(count+2,len(fat)//4)) if struct.unpack_from('<I',fat,i*4)[0]&0x0fffffff]
  meta={'drive':'H:','mode':'read-only','bytes_per_sector':bps,'sectors_per_cluster':spc,'fat_copies':nfats,'fat_copies_equal':same,'root_cluster':root_cluster,'allocated_clusters':len(used),'cluster_links':used[:100]}
  excerpts=[]
  for cluster,next_cluster in used[:64]:
   if next_cluster==0x0ffffff7:continue
   drive.seek(data_start+(cluster-2)*bps*spc);data=drive.read(bps*spc)
   (out/f'cluster-{cluster}.bin').write_bytes(data)
   text=data.rstrip(b'\x00').decode('utf-8',errors='replace')
   if '时间(hh:mm:ss)' in text or '平均流速' in text or 'ml/h' in text:
    excerpts.append({'cluster':cluster,'next_cluster':next_cluster,'excerpt':text[:500]})
  meta['record_fragments']=excerpts
  (out/'metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf-8')
  print(json.dumps(meta,ensure_ascii=False,indent=2))
except (OSError,AssertionError) as error:
 print('READ_ONLY_VOLUME_UNAVAILABLE:',repr(error));sys.exit(1)
