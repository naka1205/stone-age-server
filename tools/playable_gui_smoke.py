#!/usr/bin/env python3
"""Exercise the real macOS client using native mouse/keyboard input over TLS + MySQL + Redis."""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import time

for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, 'reconfigure'):
        stream.reconfigure(encoding='utf-8', errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--client-cwd', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--control', type=Path, default=Path('/private/tmp/stoneage-gui-control'))
    parser.add_argument('--stage', choices=['login', 'character', 'full'], default='full')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
    server_config = args.output / 'server.json'
    server_config.write_text(json.dumps({'bind_addr': '127.0.0.1', 'listen_port': port, 'rng_seed': 42,
                                         'tempo': {'tick_hz': 60, 'battle_turn_interval_ms': 1000}}))
    client_config = args.output / 'client.json'
    inspection = args.output / 'client-state.json'
    client_config.write_text(json.dumps({'host': '127.0.0.1', 'port': port, 'tls_ca': str(args.runtime / 'ca.pem'),
                                         'font': str(args.client_cwd / 'assets/fonts/NotoSansCJKsc-Regular.otf'),
                                         'content': str(args.client_cwd / 'assets/content/p2-v1'), 'inspection_file': str(inspection)}))
    environment = dict(os.environ)
    processes = []
    logs = []
    manifest = json.loads((args.client_cwd / 'assets/content/p2-v1/manifest.json').read_text())
    results = {'stage': args.stage, 'passed': False, 'checks': [],
               'content_version': manifest['content_version'], 'atlas_sha256': manifest['pages'],
               'input_method': 'native macOS mouse and keyboard events'}
    account = 'ui_' + str(time.time_ns())
    password = secrets.token_urlsafe(18)
    character_name = 'p2_' + str(time.time_ns())

    def start_server():
        log = (args.output / f'server-{len(logs)}.log').open('w'); logs.append(log)
        process = subprocess.Popen([str(args.server), '--config', str(server_config), '--playable', str(args.runtime / 'storage.json')],
                                   env=environment, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        deadline = time.monotonic() + 20
        while True:
            if process.poll() is not None: raise RuntimeError('server startup failed; inspect server log')
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=0.2): break
            except OSError:
                if time.monotonic() >= deadline: raise RuntimeError('server listener not ready')
                time.sleep(0.1)
        return process

    def start_client():
        inspection.unlink(missing_ok=True)
        log = (args.output / f'client-{len(logs)}.log').open('w'); logs.append(log)
        process = subprocess.Popen([str(args.client), '--playable', str(client_config)], cwd=args.client_cwd,
                                   env=environment, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def read():
        try: return json.loads(inspection.read_text())
        except (FileNotFoundError, json.JSONDecodeError): return {}

    def wait(predicate, message, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = read()
            if state and predicate(state): return state
            if client.poll() is not None: raise RuntimeError('client exited: ' + message)
            time.sleep(0.08)
        raise RuntimeError('timed out: ' + message + '; last status: ' + read().get('status', '?'))

    def control(action, *values, text=None):
        command = [str(args.control), action, str(client.pid), *map(str, values)]
        output = subprocess.run(command, input=None if text is None else text.encode(), capture_output=True)
        if output.returncode:
            raise RuntimeError('native UI control failed: ' + output.stderr.decode(errors='replace').strip())
        return output.stdout

    def window():
        return json.loads(control('window'))

    def click(x, y, button=False):
        count = read().get('input_clicks', 0)
        for attempt in range(3 if button else 1):
            bounds = window()['bounds']
            control('click', bounds['X'] + x, bounds['Y'] + bounds['Height'] - 640 + y)
            time.sleep(0.13)
            if not button: return
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                if read().get('input_clicks', 0) > count: return
                time.sleep(0.05)
        raise RuntimeError('button did not receive native click at ' + str((x, y)))

    def fill(x, y, value):
        click(x, y); control('key', 'clear'); control('type', text=value); time.sleep(0.15)

    def capture(name):
        from PIL import Image
        (args.output / (name + '.window.json')).write_text(json.dumps(window()))
        raw = args.output / (name + '.rgba')
        raw.unlink(missing_ok=True)
        Path(str(inspection) + '.capture').write_text(str(raw) + '\n')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            data = raw.read_bytes() if raw.exists() else b''
            if len(data) >= 8:
                width, height = struct.unpack_from('<ii', data)
                if width > 0 and height > 0 and len(data) == 8 + width * height * 4:
                    Image.frombytes('RGBA', (width, height), data[8:]).save(args.output / (name + '.png'))
                    raw.unlink()
                    return
            time.sleep(0.1)
        raise RuntimeError('client framebuffer capture timed out')

    def login(create=False):
        fill(710, 246, account); fill(710, 334, password)
        click(796 if create else 649, 407, button=True)
        wait(lambda state: state['state'] == 'selecting_character', 'login')

    def check(label, state):
        results['checks'].append({'check': label, 'character': state.get('character')})
        print(json.dumps({'check': label, 'passed': True}, ensure_ascii=False), flush=True)

    def stop(process):
        if process.poll() is not None: return
        process.terminate()
        try: process.wait(timeout=20)
        except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)

    try:
        server = start_server(); client = start_client()
        wait(lambda state: state['state'] == 'login', 'TLS handshake and login screen')
        capture('01-login')
        if args.stage == 'login':
            results['passed'] = True
            return
        login(True)
        capture('02-character-selection')
        fill(708, 228, character_name)
        click(724, 411, button=True)
        state = wait(lambda value: value['online'], 'create character')
        check('create_character_over_tls', state)
        capture('03-map')
        if args.stage == 'full':
            map_file = (args.client_cwd / 'assets/content/p2-v1/map.bin').read_bytes()
            _, width, height = struct.unpack_from('<III', map_file, 4)
            walkable = map_file[16 + width * height * 8:]
            directions = [('right', 1, 1), ('left', -1, -1), ('down', -1, 1), ('up', 1, -1)]
            turns = steps = encounters = 0
            deadline = time.monotonic() + 480
            capture_seen = False
            previous = None
            while time.monotonic() < deadline:
                state = read()
                player = state['character']['player']
                if state['result']:
                    capture('result-' + str(encounters + 1))
                    click(850, 561, button=True)
                    wait(lambda value: not value['result'], 'return to map')
                    encounters += 1
                    continue
                if not state['battle'] and player['capture_count'] > 0 and player['exp'] > 0: break
                if state['battle']:
                    if state['command_sent']: time.sleep(0.1); continue
                    live = [enemy for enemy in state['enemies'] if enemy['hp'] > 0]
                    if not live: time.sleep(0.1); continue
                    enemy = live[0]
                    # UI target selection and command buttons; no protocol shortcut.
                    local = enemy['slot'] - 10
                    click(180 + (local % 3) * 90, 230 + (local // 3) * 92 - 88, button=True)
                    capture_seen = capture_seen or state['captures_this_battle'] > 0 or player['capture_count'] > 0
                    want_capture = not capture_seen
                    command = '2' if want_capture and enemy['hp'] <= max(1, enemy['max_hp'] * 0.65) else '1'
                    sent_turn = state['turn']
                    control('key', command)
                    turns += 1
                    if turns == 1: capture('04-manual-battle')
                    wait(lambda value: not value['battle'] or value['turn'] != sent_turn, 'manual battle turn', 20)
                    capture_seen = capture_seen or read()['captures_this_battle'] > 0
                    continue
                if player['hp'] <= 0: raise RuntimeError('character died before capture/reward loop completed')
                x, y = player['x'], player['y']
                candidates = [(name, x + dx, y + dy) for name, dx, dy in directions
                              if 0 <= x + dx < width and 0 <= y + dy < height and walkable[(y + dy) * width + x + dx]]
                move = next((value for value in candidates if previous and value[1:] == previous), candidates[0])
                previous = (x, y)
                control('key', move[0]); steps += 1
                wait(lambda value: value['battle'] or value['result'] or
                     (value['character']['player']['x'], value['character']['player']['y']) != (x, y), 'authoritative map move', 5)
                time.sleep(0.25)
            else: raise RuntimeError('capture and reward loop did not complete within deadline')
            results.update(steps=steps, turns=turns, encounters=encounters)
            state = read(); check('manual_movement_battle_capture_rewards', state)
            capture('05-rewards')
        click(877, 37, button=True)
        state = wait(lambda value: value['state'] == 'closed', 'durable logout')
        saved = state['character']; check('durable_logout', state)
        login(False)
        click(723, 191, button=True)
        state = wait(lambda value: value['online'], 're-login select')
        assert state['character']['char_id'] == saved['char_id']
        assert state['character']['player'] == saved['player']
        assert state['character']['pets'] == saved['pets']
        assert state['character']['items'] == saved['items']
        check('relogin_preserves_progress', state); capture('06-relogin')
        click(877, 37, button=True)
        wait(lambda value: value['state'] == 'closed', 'second logout')
        stop(client); stop(server)
        server = start_server(); client = start_client()
        wait(lambda value: value['state'] == 'login', 'client after server restart')
        login(False); click(723, 191, button=True)
        state = wait(lambda value: value['online'], 'read after server restart')
        assert state['character']['player'] == saved['player']
        assert state['character']['pets'] == saved['pets']
        assert state['character']['items'] == saved['items']
        check('server_restart_preserves_progress', state); capture('07-server-restart')
        click(877, 37, button=True); wait(lambda value: value['state'] == 'closed', 'final logout')
        results['passed'] = True
    except Exception as error:
        results['error'] = str(error).replace(password, '[redacted]')
        print(json.dumps({'failed': results['error']}, ensure_ascii=False), flush=True)
        try: capture('failure')
        except Exception: pass
        raise
    finally:
        for process in reversed(processes): stop(process)
        for log in logs: log.close()
        # No test credential is retained in the reviewable evidence.
        for path in args.output.glob('*.log'):
            text = path.read_text(errors='replace'); path.write_text(text.replace(password, '[redacted]'))
        (args.output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
