from pathlib import Path
import subprocess, json, sys
here=Path(__file__).resolve().parent
project=here.parents[1]
runtime={}
exec((project/'tests/storage-write-guard/rv32_host.py').read_text().replace('5_000_000','50_000_000'),runtime)
gcc,RV32=runtime['GCC'],runtime['RV32']
out=here/'.build';out.mkdir(exist_ok=True)
libgcc=subprocess.check_output([str(gcc),'-march=rv32i_zicsr_zifencei','-mabi=ilp32','-print-libgcc-file-name'],text=True).strip()
cmd=[str(gcc),'-march=rv32im','-mabi=ilp32','-std=c11','-Os','-ffreestanding','-fno-builtin','-msmall-data-limit=0','-nostdlib','-Wno-unused-variable','-Wno-unused-function','-Wl,-Ttext=0x10000','-Wl,-e,0x10000','-Wl,--no-relax','-I',str(project/'main'),'-I',str(project/'tests/storage-write-guard/monitor-stubs'),str(here/'fixture.c'),libgcc,'-o',str(out/'fixture.elf')]
subprocess.run(cmd,check=True)
engine=RV32(out/'fixture.elf')
names=['test_bpb_high_byte_and_absolute_lba','test_bpb_bad_signature','test_bpb_bad_cluster_size','test_bpb_overflow','test_sector_read_eight_chunks','test_sector_read_partial_failure','test_sector_zero_progress_bounded','test_sector_oversize_consumed_rejected','test_var32_little_endian','test_locate_lba','test_locate_eof_is_valid','test_locate_bad_length','test_probe_remount_failure_stops','test_bad_boot_remount_no_fake_geometry','test_cached_geometry_avoids_physical_read','test_reset_invalidates_geometry']
results=[]
names+=['test_legacy_correction_and_no_repeat','test_legacy_high_byte_mismatch_blocks','test_legacy_ready_wrong_base_blocks','test_new_chip_no_legacy_writes','test_legacy_root_open_failure_blocks','test_bad_data_base_blocks_write','test_directory_in_fat_blocks_write','test_valid_directory_allows_write','test_readback_bytes_match','test_readback_content_mismatch','test_readback_zero_length_blocks','test_readback_closes_without_update']
names+=['test_partial_correction_blocks']
for name in names:
    result=engine.call(name)
    results.append({'case':name,'passed':result==0,'failure_line':result})
    print(('PASS ' if result==0 else 'FAIL ')+name,flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
sys.exit(0 if all(x['passed'] for x in results) else 1)
