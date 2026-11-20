#!/usr/bin/env python3
"""Check Launcher search bounds and frame dimensions across display sizes.

Build mup and the ART artifacts first. Pass a dedicated copy of an Android
rootfs, never a running instance, using --sysroot PATH. PNGs are saved under
build/tests for visual inspection. Requires macOS Hypervisor access.
"""
from pathlib import Path
import os, subprocess, time, socket, struct, re, argparse
import zlib

def save_png(path, w, h, rgba):

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 4294967295)
    scan = b''.join((b'\x00' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h)))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b''))
root = Path(__file__).resolve().parents[2]
os.chdir(root)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sysroot', type=Path, required=True)
sysroot = parser.parse_args().sysroot.resolve()
assert (sysroot / 'system/framework/framework.jar').exists(), 'Android rootfs required'
sock = sysroot / f'tmp/r{os.getpid()}.sock'
service_log = open('/tmp/muplar-responsive-service.log', 'w')
service = subprocess.Popen([str(root / 'build/bin/muplard'), '--socket', str(sock), '--registry', str(root / 'build/tests/responsive-packages.properties'), '--pid-file', '/tmp/muplar-responsive-service.pid', '--settings', str(root / 'build/tests/responsive-settings.db')], stdout=service_log, stderr=subprocess.STDOUT)
try:
    for _ in range(100):
        if sock.exists():
            break
        time.sleep(0.05)
    for w, h in [(720, 1280), (1080, 1920), (1920, 1920), (1920, 1080)]:
        name = f'{w}x{h}'
        logpath = Path('/tmp/muplar-responsive-' + name + '.log')
        frame = root / f'muplar/frames/responsive-{name}-{os.getpid()}.mhr'
        frame.unlink(missing_ok=True)
        env = dict(os.environ, MUPLAR_SERVICE_SOCKET=f'/tmp/r{os.getpid()}.sock', MUPLAR_ANDROID_DISPLAY_WIDTH=str(w), MUPLAR_ANDROID_DISPLAY_HEIGHT=str(h), MUPLAR_DEBUG_VIEWS='1', MUPLAR_ANDROID_SOFTWARE_FRAME_PATH=f'/data/local/tmp/muplar/frames/responsive-{name}-{os.getpid()}.mhr')
        log = open(logpath, 'w')
        proc = subprocess.Popen([str(root / 'build/bin/mup'), '--sysroot', str(sysroot), '--apk', str(root / 'build/launcher3/fixture/Launcher3.apk')], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if 'entering main looper' in logpath.read_text(errors='replace') and frame.exists():
                    break
                if proc.poll() is not None:
                    raise RuntimeError(name + ' exited ' + str(proc.returncode))
                time.sleep(0.1)
            assert frame.exists(), name + ' initial frame missing'
            before = frame.stat().st_mtime_ns
            s = socket.socket(socket.AF_UNIX)
            s.settimeout(5)
            s.connect(str(sock))
            data = b'all-apps\ncom.android.launcher3'
            s.sendall(struct.pack('<IHHI4xQ', 1297436740, 1, 23, len(data), 1) + data)
            header = s.recv(24)
            s.recv(struct.unpack('<IHHI4xQ', header)[3])
            s.close()
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                text = logpath.read_text(errors='replace')
                matches = re.findall(r"AppsSearchContainerLayout w=(\d+) h=(\d+) vis=0 x=(\d+) y=(\d+)", text)
                matches = [m for m in matches if int(m[0]) > 0 and int(m[1]) > 0]
                if matches and frame.stat().st_mtime_ns != before:
                    break
                time.sleep(0.1)
            assert 'StackOverflowError' not in text, name + ' recursive layout'
            assert matches, name + ' search bounds missing'
            sw, sh, left, top = map(int, matches[-1])
            assert 0 < sw <= w and sh >= 144, (name, sw, sh)
            raw = frame.read_bytes()
            magic, fw, fh, stride, size = struct.unpack('<IIIIQ', raw[:24])
            assert (fw, fh) == (w, h), (name, fw, fh)
            save_png(root / f'build/tests/responsive-{name}.png', fw, fh, raw[24:])
            sample = 24 + ((top + sh // 2) * fw + left + 12) * 4
            assert min(raw[sample:sample + 3]) >= 220, (name, 'dark search background')
            print(name + f': frame {fw}x{fh}, search {sw}x{sh}', flush=True)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            log.close()
            frame.unlink(missing_ok=True)
finally:
    service.terminate()
    service.wait(timeout=5)
    service_log.close()
    for directory in (root / 'muplar/frames', root / 'muplar'):
        if directory.exists() and (not any(directory.iterdir())):
            directory.rmdir()
