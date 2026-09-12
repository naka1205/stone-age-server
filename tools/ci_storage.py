#!/usr/bin/env python3
"""Linux CI: pinned real MySQL/Redis containers with verified database TLS."""
import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import tarfile
import time
import urllib.request

REPO = Path(__file__).resolve().parents[1]
CONNECTOR_URL = 'https://cdn.mysql.com/archives/mysql-connector-c++/mysql-connector-c++-8.4.0-linux-glibc2.28-x86-64bit.tar.gz'


def run(command, **kwargs):
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    runtime = build / 'environment'
    runtime.mkdir(parents=True, exist_ok=True)
    connector = runtime / 'connector.tar.gz'
    urllib.request.urlretrieve(CONNECTOR_URL, connector)
    with tarfile.open(connector, 'r:gz') as archive:
        archive.extractall(runtime, filter='data')
    connector_home = runtime / 'mysql-connector-c++-8.4.0-linux-glibc2.28-x86-64bit'
    certificates = runtime / 'tls'
    certificates.mkdir(exist_ok=True)
    run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2', '-subj', '/CN=Stoneage CI',
         '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1',
         '-keyout', certificates / 'server.key', '-out', certificates / 'server.pem'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # These one-job test keys are readable by the unprivileged container server.
    os.chmod(certificates / 'server.key', 0o644)
    mysql_password = secrets.token_hex(24)
    redis_password = secrets.token_hex(24)
    root_password = secrets.token_hex(24)
    env_file = runtime / 'mysql.env'
    env_file.write_text('MYSQL_ROOT_PASSWORD=' + root_password + '\n')
    os.chmod(env_file, 0o600)
    containers = []
    try:
        run(['docker', 'run', '--detach', '--name', 'sa-mysql-ci', '--env-file', env_file,
             '--publish', '127.0.0.1:13306:3306', '--volume', str(certificates) + ':/tls:ro',
             'mysql:8.4.8', '--mysqlx=0', '--require-secure-transport=ON', '--ssl-ca=/tls/server.pem',
             '--ssl-cert=/tls/server.pem', '--ssl-key=/tls/server.key'])
        containers.append('sa-mysql-ci')
        run(['docker', 'run', '--detach', '--name', 'sa-redis-ci', '--publish', '127.0.0.1:16379:6379',
             'redis:8.6.2', 'redis-server', '--requirepass', redis_password], stdout=subprocess.DEVNULL)
        containers.append('sa-redis-ci')
        admin = ['docker', 'exec', '-i', '-e', 'MYSQL_PWD=' + root_password, 'sa-mysql-ci', 'mysql', '-uroot', '--protocol=socket']
        deadline = time.monotonic() + 90
        while True:
            result = subprocess.run(admin, input=b'SELECT 1;', stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode == 0:
                break
            if time.monotonic() >= deadline:
                run(['docker', 'logs', 'sa-mysql-ci'])
                raise RuntimeError('MySQL readiness timed out')
            time.sleep(1)
        sql = (REPO / 'deploy/sql/001-session.sql').read_text()
        sql += "\nCREATE USER 'stoneage'@'%' IDENTIFIED WITH caching_sha2_password BY '" + mysql_password + "' REQUIRE SSL;\n"
        sql += "GRANT SELECT,INSERT,UPDATE,DELETE ON sa_session.* TO 'stoneage'@'%';\n"
        result = subprocess.run(admin, input=sql.encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode: raise RuntimeError('schema provisioning failed')
        config = runtime / 'storage.json'
        config.write_text(json.dumps({'mysql_host': '127.0.0.1', 'mysql_port': 13306, 'mysql_user': 'stoneage',
                                      'mysql_password': mysql_password, 'mysql_ca': str(certificates / 'server.pem'),
                                      'redis_host': '127.0.0.1', 'redis_port': 16379, 'redis_password': redis_password}))
        os.chmod(config, 0o600)
        run(['cmake', '-S', REPO, '-B', build, '-G', 'Ninja', '-DSA_ENABLE_MYSQL_STORAGE=ON', '-DSA_WERROR=ON', '-Dmysql-concpp_DIR=' + str(connector_home)])
        run(['cmake', '--build', build, '--target', 'stone_age_server', 'sa_mysql_storage_test', 'sa_world_persistence_test', '-j', '2'])
        environment = dict(os.environ); environment['SA_STORAGE_CONFIG'] = str(config)
        run([build / 'tests/sa_mysql_storage_test'], env=environment)
        run(['ctest', '--test-dir', build, '-R', '^world_persistence$', '--output-on-failure'])
    finally:
        for container in containers:
            subprocess.run(['docker', 'rm', '--force', container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for private in [runtime / 'storage.json', env_file, certificates / 'server.key']:
            private.unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        # Do not echo a command that may contain an ephemeral test credential.
        raise SystemExit(f'Storage CI command failed (exit {error.returncode}).') from None
