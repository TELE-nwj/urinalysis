"""汇总已运行回归、复制匹配烧录镜像、核对基线与备份，不访问设备。"""
from pathlib import Path
import difflib
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
PROJECT = ROOT / 'git/third_logtest_20261012'

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

suites = {
    '会话联合回归': PROJECT / 'tests/session-files/.build/d-green-results.json',
    '控制与写盘保护': PROJECT / 'tests/storage-write-guard/.build/results.json',
    '地址兼容与回读': PROJECT / 'tests/address-diagnostics/.build/results.json',
    '存储服务': PROJECT / 'tests/address-diagnostics/.build/storage-results.json',
    '字节写与等待': PROJECT / 'tests/log-sequence/.build/results.json',
}
tests = {}
for name, source in suites.items():
    data = read_json(source)
    cases = data['current'] if name == '存储服务' else data
    assert isinstance(cases, list), (name, data)
    assert all(case.get('passed') for case in cases), (name, data)
    tests[name] = {'passed': len(cases), 'evidence': str(source.relative_to(ROOT))}
    shutil.copy2(source, OUT / (source.parent.parent.name + '-' + source.name))

red_source = OUT / 'address-red-results.json'
red = read_json(red_source)
red_failures = sum(not x['passed'] for x in red)
assert len(red) == 37 and red_failures == 9
shutil.copy2(red_source, OUT / 'initial-address-red-results.json')
shutil.copy2(PROJECT / 'tests/session-files/.build/d-green-results.json', OUT / 'session-green-results.json')

formal_manifest = read_json(ROOT / 'outputs/third-logtest-20261012/baseline-manifest.json')
formal_differences = [entry['Path'] for entry in formal_manifest
                      if digest(ROOT / 'git/third' / entry['Path']) != entry['SHA256'].upper()]
assert not formal_differences, formal_differences
backup_manifest = read_json(OUT / 'before-manifest.json')
assert all(digest(OUT / 'before' / entry['Path']) == entry['SHA256'] for entry in backup_manifest)

build = PROJECT / 'build'
metadata = read_json(build / 'project_description.json')
assert metadata['target'] == 'esp32s3' and metadata['project_name'] == 'third'
flash = read_json(build / 'flasher_args.json')
assert flash['extra_esptool_args']['chip'] == 'esp32s3'
binary = (build / 'third.bin').read_bytes()
assert '[存储保护修复版] 20261015-D'.encode('utf8') in binary
assert len(binary) < 0x100000
images = []
for offset, relative in flash['flash_files'].items():
    source = build / relative
    target = PROJECT / 'firmware' / relative
    target.parent.mkdir(exist_ok=True, parents=True)
    shutil.copy2(source, target)
    assert digest(source) == digest(target)
    images.append({'Path': relative, 'Offset': offset, 'Bytes': target.stat().st_size, 'SHA256': digest(target)})
for name in ['flash_args', 'flasher_args.json']:
    shutil.copy2(build / name, PROJECT / 'firmware' / name)
(PROJECT / 'firmware/manifest.json').write_text(json.dumps(images, ensure_ascii=False, indent=2), encoding='utf8')
(PROJECT / 'firmware/SHA256SUMS.txt').write_text(''.join(f"{item['SHA256']}  {item['Path']}\n" for item in images), encoding='utf8')

changes = []
diff = []
for path in sorted(PROJECT.rglob('*')):
    if not path.is_file():
        continue
    relative = path.relative_to(PROJECT)
    if any(part in ['build', '.build', '.git', '__pycache__', 'firmware'] for part in relative.parts):
        continue
    old = OUT / 'before' / relative
    if old.exists() and digest(old) == digest(path):
        continue
    changes.append(str(relative))
    if path.suffix in ['.c', '.h', '.py', '.md']:
        old_text = old.read_text(encoding='utf-8-sig').splitlines(True) if old.exists() else []
        diff.extend(difflib.unified_diff(old_text, path.read_text(encoding='utf-8-sig').splitlines(True),
                                         fromfile='before/' + relative.as_posix(), tofile='current/' + relative.as_posix()))
(OUT / 'source.diff').write_text(''.join(diff), encoding='utf8')
summary = {'version': '20261015-D', 'build_succeeded': True, 'image_marker_verified': True,
           'test_suites': tests, 'total_current_passed': sum(s['passed'] for s in tests.values()),
           'new_regression_red_total': len(red), 'new_regression_red_failures': red_failures,
           'formal_baseline_files_unchanged': len(formal_manifest), 'before_backup_verified': len(backup_manifest),
           'images': images, 'changed_or_added_text_files': changes,
           'hardware_acceptance': '未执行；现盘FAT损坏，D预期拒写；1E数据页变量时序需实机验证'}
(OUT / 'verification.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(summary, ensure_ascii=False, indent=2))
