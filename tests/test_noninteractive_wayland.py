"""Verify real SDL noninteractive surfaces submit empty Wayland input regions before mapping in an owned private compositor."""

import argparse
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import time


def verify_regions(trace):
    """Correlate each actual tagged SdlHost to its wl_surface and require an empty region before its first buffer attach."""
    lines = trace.splitlines()
    surfaces = {}
    toplevels = {}
    titles = {}
    for index, line in enumerate(lines):
        match = re.search(r'get_xdg_surface\(new id xdg_surface[#@](\d+), wl_surface[#@](\d+)\)', line)
        if match:
            surfaces[match[1]] = match[2]
        match = re.search(r'xdg_surface[#@](\d+)\.get_toplevel\(new id xdg_toplevel[#@](\d+)\)', line)
        if match:
            toplevels[match[2]] = surfaces[match[1]]
        match = re.search(r'xdg_toplevel[#@](\d+)\.set_title\("HelEngine isolated (opaque|transparent) output"\)', line)
        if match:
            titles[match[2]] = toplevels[match[1]]
    if set(titles) != {'opaque', 'transparent'}:
        raise AssertionError('Missing actual SdlHost roles in private Wayland protocol trace')
    for role, surface in titles.items():
        decoration = r'get_subsurface\(new id wl_subsurface[#@]\d+, wl_surface[#@]\d+, wl_surface[#@]' + surface + r'\)'
        if any(re.search(decoration, line) for line in lines):
            raise AssertionError(role + ' noninteractive content created a potentially interactive decoration subsurface')
        region_requests = []
        if any(re.search(r'wl_surface[#@]' + surface + r'\.set_input_region\((nil|null)\)', line) for line in lines):
            raise AssertionError(role + ' input region reverted to the default full region')
        first_attach = None
        for index, line in enumerate(lines):
            if re.search(r'wl_surface[#@]' + surface + r'\.attach\(wl_buffer[#@]', line) and first_attach is None:
                first_attach = index
            match = re.search(r'wl_surface[#@]' + surface + r'\.set_input_region\(wl_region[#@](\d+)\)', line)
            if match:
                region_requests.append([index, match[1]])
        if not region_requests:
            raise AssertionError(role + ' real wl_surface never received an explicit input region')
        for index, region in region_requests:
            creates = [offset for offset, line in enumerate(lines[:index]) if re.search(r'create_region\(new id wl_region[#@]' + region + r'\)', line)]
            if not creates or any(re.search(r'wl_region[#@]' + region + r'\.add\(', line) for line in lines[creates[-1]:index]):
                raise AssertionError(role + ' input region was not empty')
        if first_attach is None or region_requests[0][0] >= first_attach:
            raise AssertionError(role + ' input region was configured after mapping began')
    print('PASS real SdlHost opaque/transparent wl_surface empty input regions before mapping; shared-device resize/teardown preserved')


def reject_nonwayland(binary, root):
    """Require noninteractive initialization to reject an owned X11 server before any output is shown."""
    server_log = (root / 'x11-server.log').open('w')
    server = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0', '800x600x24', '-nolisten', 'tcp'],
                              stdout=subprocess.PIPE, stderr=server_log, text=True)
    try:
        if not select.select([server.stdout], [], [], 5)[0]:
            raise RuntimeError('Owned X11 rejection fixture did not publish a display')
        display = server.stdout.readline().strip()
        if not display.isdigit():
            raise RuntimeError('Owned X11 rejection fixture failed to start')
        environment = dict(os.environ)
        environment.pop('WAYLAND_DISPLAY', None)
        environment.pop('WAYLAND_DEBUG', None)
        environment.update(DISPLAY=':' + display, SDL_VIDEODRIVER='x11', HELENGINE_ISOLATED_DISPLAY_TEST='1')
        result = subprocess.run([str(binary)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
        (root / 'nonwayland-rejection.log').write_text(result.stdout + result.stderr)
        if result.returncode == 0 or 'Noninteractive outputs require native Wayland input regions.' not in result.stderr:
            raise AssertionError('Unsupported noninteractive driver did not fail at the native Wayland initialization gate')
        print('PASS unsupported noninteractive X11 host rejected explicitly before graphics initialization or ShowWindow')
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)
        server.stdout.close()
        server_log.close()


def inside(binary, normal_binary, root):
    """Own one private compositor and run only the explicit native helper, without personal display or input injection."""
    address = os.environ.get('DBUS_SESSION_BUS_ADDRESS')
    if not address or str(root / 'runtime') not in address:
        raise RuntimeError('Native input checks require the private fixture session bus')
    os.environ['DBUS_SYSTEM_BUS_ADDRESS'] = address
    compositor_log = (root / 'compositor.log').open('w')
    compositor = subprocess.Popen(['gnome-shell', '--headless', '--wayland', '--no-x11', '--virtual-monitor=800x600',
                                   '--wayland-display=gevo-noninteractive'], stdout=compositor_log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 10
        socket = root / 'runtime/gevo-noninteractive'
        while not socket.exists():
            if compositor.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError('Owned private compositor did not become ready; see ' + str(root / 'compositor.log'))
            time.sleep(0.05)
        environment = dict(os.environ)
        environment.update(WAYLAND_DISPLAY='gevo-noninteractive', WAYLAND_DEBUG='client', HELENGINE_ISOLATED_DISPLAY_TEST='1')
        result = subprocess.run([str(binary)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
        (root / 'helper.stdout').write_text(result.stdout)
        (root / 'wayland-client.log').write_text(result.stderr)
        if result.returncode:
            raise RuntimeError('Native SdlHost helper failed: ' + result.stdout[-1000:] + result.stderr[-2000:])
        verify_regions(result.stderr)
        if normal_binary:
            normal = subprocess.run([str(normal_binary)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
            (root / 'normal.stdout').write_text(normal.stdout)
            (root / 'normal-wayland-client.log').write_text(normal.stderr)
            if normal.returncode:
                raise RuntimeError('Default interactive SDL host regressed: ' + normal.stdout[-1000:] + normal.stderr[-2000:])
            print('PASS default interactive SDL Vulkan presentation and resize unchanged')
        reject_nonwayland(binary, root)
    finally:
        compositor.terminate()
        try:
            compositor.wait(timeout=5)
        except subprocess.TimeoutExpired:
            compositor.kill()
            compositor.wait(timeout=5)
        compositor_log.close()


def main():
    """Create a private bus and XDG tree with explicit lavapipe/toolkit paths and no inherited desktop environment."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-binary', required=True, type=Path)
    parser.add_argument('--normal-test-binary', type=Path)
    parser.add_argument('--inside', action='store_true')
    arguments = parser.parse_args()
    root = Path('/tmp/helengine/gevo/linux/noninteractive-wayland')
    if arguments.inside:
        inside(arguments.test_binary, arguments.normal_test_binary, root)
        return
    for directory in ('home', 'runtime', 'config', 'data', 'cache', 'state'):
        (root / directory).mkdir(parents=True, exist_ok=True)
    (root / 'runtime').chmod(0o700)
    configuration = root / 'dbus.conf'
    configuration.write_text('<busconfig><type>session</type><listen>unix:tmpdir=' + str(root / 'runtime') +
                             '</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/>'
                             '<allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
    prefix = '/home/helena/dev/helworks/tools/linux-native/usr'
    environment = {'PATH':'/usr/bin:/bin', 'HOME':str(root / 'home'), 'XDG_RUNTIME_DIR':str(root / 'runtime'),
                   'XDG_CONFIG_HOME':str(root / 'config'), 'XDG_DATA_HOME':str(root / 'data'), 'XDG_CACHE_HOME':str(root / 'cache'),
                   'XDG_STATE_HOME':str(root / 'state'), 'XDG_DATA_DIRS':'/usr/share', 'XDG_SESSION_TYPE':'wayland',
                   'XDG_CURRENT_DESKTOP':'GNOME', 'GSETTINGS_BACKEND':'keyfile', 'LIBGL_ALWAYS_SOFTWARE':'1',
                   'SDL_VIDEODRIVER':'wayland', 'VK_DRIVER_FILES':'/usr/share/vulkan/icd.d/lvp_icd.json',
                   'VK_LAYER_PATH':prefix + '/share/vulkan/explicit_layer.d', 'LD_LIBRARY_PATH':prefix + '/lib/x86_64-linux-gnu'}
    command = ['dbus-run-session', '--config-file=' + str(configuration), sys.executable, str(Path(__file__).resolve()),
               '--inside', '--test-binary', str(arguments.test_binary.resolve())]
    if arguments.normal_test_binary:
        command.extend(['--normal-test-binary', str(arguments.normal_test_binary.resolve())])
    result = subprocess.run(command, env=environment, timeout=60)
    if result.returncode:
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
