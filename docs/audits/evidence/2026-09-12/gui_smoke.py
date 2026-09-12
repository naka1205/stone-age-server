#!/usr/bin/env python3
"""Run the real GUI over loopback TCP; each failure must return nonzero.

Only the new server/client executables are launched. No legacy runtime is used.
"""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import threading
import time


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--client-cwd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []

    def run_client(case, port, outcome, version=1, frames=1800):
        config = args.output / (case + "-client.json")
        config.write_text(json.dumps({"connect_host": "127.0.0.1",
                                      "connect_port": port,
                                      "protocol_version": version}), encoding="utf-8")
        command = [str(args.client), "--battle", "--config", str(config),
                   "--autotest", str(frames)]
        with (args.output / (case + "-client.log")).open("w", encoding="utf-8") as log:
            proc = subprocess.run(command, cwd=args.client_cwd, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=50)
        output = (args.output / (case + "-client.log")).read_text(encoding="utf-8", errors="replace")
        match = re.search(r"main: battle-autotest[^\n]+", output)
        summary = match.group(0) if match else "missing battle-autotest summary"
        success = (proc.returncode == (0 if outcome == 1 else 1) and
                   f"outcome={outcome} " in summary)
        if outcome == 1:
            success = success and "joined=true" in summary and "BattleScene: snapshot" in output
            counts = re.search(r"turns=(\d+) event_msgs=(\d+) events=(\d+)", summary)
            success = success and counts is not None and all(int(v) > 0 for v in counts.groups())
        row = {"case": case, "exit_code": proc.returncode, "expected_outcome": outcome,
               "passed": bool(success), "summary": summary}
        results.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)

    port = free_port()
    server_config = args.output / "server.json"
    server_config.write_text(json.dumps({"bind_addr": "127.0.0.1", "listen_port": port,
                                        "protocol_version": 1, "log_level": "debug",
                                        "tempo": {"tick_hz": 100, "battle_turn_interval_ms": 200},
                                        "demo_battle": {"enabled": True, "slot": 0}}), encoding="utf-8")
    with (args.output / "server.log").open("w", encoding="utf-8") as log:
        server = subprocess.Popen([str(args.server), "--config", str(server_config)],
                                  stdout=log, stderr=subprocess.STDOUT)
        try:
            for attempt in range(100):
                if server.poll() is not None:
                    raise RuntimeError("new server exited before listening")
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.05)
            else:
                raise RuntimeError("new server did not start listening")
            run_client("completed", port, 1)
            run_client("version_rejected", port, 4, version=2, frames=300)
        finally:
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=5)

    for case, expected in [("early_disconnect", 5), ("malformed_frame", 6), ("timeout", 7)]:
        stopped = threading.Event()
        errors = []
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(20)
            port = listener.getsockname()[1]

            def serve():
                try:
                    connection, _ = listener.accept()
                    with connection:
                        connection.settimeout(10)
                        connection.recv(4096)
                        if case == "early_disconnect":
                            return
                        if case == "malformed_frame":
                            connection.sendall(b"\0\0\0\0")
                        stopped.wait(20)
                except Exception as exc:
                    errors.append(repr(exc))

            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            try:
                run_client(case, port, expected, frames=180)
            finally:
                stopped.set()
                thread.join(timeout=2)
            if errors:
                raise RuntimeError(f"{case} fixture failed: {errors}")
    (args.output / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n",
                                             encoding="utf-8")
    return 0 if all(row["passed"] for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
