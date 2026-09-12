#!/usr/bin/env python3
"""Provision an isolated local MySQL 8.4.8 + Redis 8.6.2 test environment.

Uses existing unpacked distributions; never installs a global service or clears data.
Credentials and CA private keys stay in the explicitly selected runtime directory.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import time

REPO = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, **kwargs)


def write_private(path, text):
    with path.open('w') as stream:
        os.chmod(path, 0o600)
        stream.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'stop', 'status'])
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--mysql-home', type=Path)
    parser.add_argument('--redis-home', type=Path)
    parser.add_argument('--mysql-port', type=int, default=13306)
    parser.add_argument('--redis-port', type=int, default=16379)
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    os.chmod(root, 0o700)
    state_file = root / 'state.json'
    if args.action != 'start':
        state = json.loads(state_file.read_text()) if state_file.exists() else {}
        for name in ['mysql', 'redis']:
            pidfile = root / (name + '.pid')
            pid = int(pidfile.read_text().strip()) if pidfile.exists() else None
            alive = False
            if pid:
                result = subprocess.run(['ps', '-p', str(pid), '-o', 'command='],
                                        capture_output=True, text=True)
                alive = result.returncode == 0 and str(root) in result.stdout
                if name == 'redis' and result.returncode == 0 and state.get('redis_home'):
                    # Redis replaces its argv with the executable and listen address.
                    # Match both, so a stale PID can never stop another instance.
                    executable = str(Path(state['redis_home']) / 'src/redis-server')
                    address = '127.0.0.1:' + str(state.get('redis_port', 16379))
                    alive = alive or result.stdout.strip().startswith(executable + ' ' + address)
            if args.action == 'stop' and alive:
                os.kill(pid, signal.SIGTERM)
            print(name, 'running' if alive else 'stopped', 'stop requested' if alive and args.action == 'stop' else '')
        return
    if not args.mysql_home or not args.redis_home:
        parser.error('start requires --mysql-home and --redis-home')
    mysql = args.mysql_home.resolve() / 'bin' / 'mysql'
    mysqld = args.mysql_home.resolve() / 'bin' / 'mysqld'
    redis = args.redis_home.resolve() / 'src' / 'redis-server'
    if b'8.4.8' not in run([mysqld, '--version']).stdout:
        raise RuntimeError('MySQL 8.4.8 required')
    if b'v=8.6.2' not in run([redis, '--version']).stdout:
        raise RuntimeError('Redis 8.6.2 required')
    secrets_file = root / 'credentials.json'
    if not secrets_file.exists():
        write_private(secrets_file, json.dumps({k: secrets.token_hex(24) for k in ['root', 'mysql', 'redis']}))
    credentials = json.loads(secrets_file.read_text())
    if not (root / 'server.pem').exists():
        run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '365',
             '-subj', '/CN=Stoneage Local Development CA', '-addext', 'basicConstraints=critical,CA:TRUE',
             '-keyout', root / 'ca.key', '-out', root / 'ca.pem'])
        run([args.openssl, 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=localhost',
             '-keyout', root / 'server.key', '-out', root / 'server.csr'])
        (root / 'server.ext').write_text('subjectAltName=DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\nbasicConstraints=CA:FALSE\n')
        run([args.openssl, 'x509', '-req', '-in', root / 'server.csr', '-CA', root / 'ca.pem',
             '-CAkey', root / 'ca.key', '-CAcreateserial', '-days', '365', '-extfile', root / 'server.ext',
             '-out', root / 'server.pem'])
        for name in ['ca.key', 'server.key']:
            os.chmod(root / name, 0o600)
    datadir = root / 'mysql-data'
    if not (datadir / 'mysql').exists():
        datadir.mkdir(exist_ok=True)
        run([mysqld, '--no-defaults', '--initialize-insecure', '--basedir=' + str(args.mysql_home.resolve()),
             '--datadir=' + str(datadir), '--log-error=' + str(root / 'mysql-initialize.log')])
    socket = root / 'mysql.sock'
    mysql_config = f'''[mysqld]
basedir={args.mysql_home.resolve()}
datadir={datadir}
socket={socket}
pid-file={root / 'mysql.pid'}
log-error={root / 'mysql.log'}
bind-address=127.0.0.1
port={args.mysql_port}
mysqlx=0
skip_name_resolve=ON
require_secure_transport=ON
ssl-ca={root / 'ca.pem'}
ssl-cert={root / 'server.pem'}
ssl-key={root / 'server.key'}
character-set-server=utf8mb4
collation-server=utf8mb4_0900_ai_ci
transaction-isolation=REPEATABLE-READ
innodb-flush-log-at-trx-commit=1
sync-binlog=1
'''
    write_private(root / 'mysql.cnf', mysql_config)
    if not socket.exists():
        with (root / 'mysql-launch.log').open('ab') as log:
            subprocess.Popen([str(mysqld), '--defaults-file=' + str(root / 'mysql.cnf')],
                             stdout=log, stderr=log, stdin=subprocess.DEVNULL, start_new_session=True)
    admin = root / 'admin.cnf'
    initialized = (root / 'bootstrapped').exists()
    password_line = 'password=' + credentials['root'] + '\n' if initialized else ''
    write_private(admin, f'[client]\nuser=root\nprotocol=socket\nsocket={socket}\n' + password_line)
    deadline = time.monotonic() + 45
    while True:
        try:
            run([mysql, '--defaults-file=' + str(admin), '-e', 'SELECT 1'])
            break
        except subprocess.CalledProcessError:
            if time.monotonic() >= deadline:
                raise RuntimeError('MySQL did not become ready; see runtime/mysql.log')
            time.sleep(0.2)
    if not initialized:
        sql = (REPO / 'deploy/sql/001-session.sql').read_text()
        sql += "\nCREATE USER 'stoneage'@'127.0.0.1' IDENTIFIED WITH caching_sha2_password BY '" + credentials['mysql'] + "' REQUIRE SSL;\n"
        sql += "GRANT SELECT,INSERT,UPDATE,DELETE ON sa_session.* TO 'stoneage'@'127.0.0.1';\n"
        sql += "ALTER USER 'root'@'localhost' IDENTIFIED BY '" + credentials['root'] + "';\n"
        run([mysql, '--defaults-file=' + str(admin)], input=sql.encode())
        (root / 'bootstrapped').touch()
        write_private(admin, f'[client]\nuser=root\nprotocol=socket\nsocket={socket}\npassword={credentials["root"]}\n')
    redisdir = root / 'redis-data'
    redisdir.mkdir(exist_ok=True)
    redis_config = f'''bind 127.0.0.1
protected-mode yes
port {args.redis_port}
requirepass {credentials['redis']}
dir {redisdir}
pidfile {root / 'redis.pid'}
logfile {root / 'redis.log'}
daemonize yes
appendonly yes
appendfsync always
'''
    write_private(root / 'redis.conf', redis_config)
    if not (root / 'redis.pid').exists():
        run([redis, root / 'redis.conf'])
    settings = {'mysql_host': '127.0.0.1', 'mysql_port': args.mysql_port, 'mysql_user': 'stoneage',
                'mysql_password': credentials['mysql'], 'mysql_ca': str(root / 'ca.pem'),
                'redis_host': '127.0.0.1', 'redis_port': args.redis_port, 'redis_password': credentials['redis'],
                'tls_certificate': str(root / 'server.pem'), 'tls_key': str(root / 'server.key'),
                'content': str(REPO / 'content/p2-v1')}
    write_private(root / 'storage.json', json.dumps(settings, indent=2))
    write_private(state_file, json.dumps({'mysql_home': str(args.mysql_home.resolve()), 'redis_home': str(args.redis_home.resolve()),
                                          'mysql_port': args.mysql_port, 'redis_port': args.redis_port}))
    if not (root / 'server.json').exists():
        write_private(root / 'server.json', json.dumps({'bind_addr': '127.0.0.1', 'listen_port': 18300}))
    if not (root / 'client.json').exists():
        client = REPO.parent / 'stone-age-client'
        write_private(root / 'client.json', json.dumps({'host': '127.0.0.1', 'port': 18300,
                                                       'tls_ca': str(root / 'ca.pem'),
                                                       'font': str(client / 'assets/fonts/NotoSansCJKsc-Regular.otf'),
                                                       'content': str(client / 'assets/content/p2-v1')}))
    print('MySQL 8.4.8 / Redis 8.6.2 ready; settings:', root / 'storage.json')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        # Never print SQL or credential-bearing command input.
        raise SystemExit(f'Environment command failed (exit {error.returncode}); inspect logs in the runtime directory.') from None
