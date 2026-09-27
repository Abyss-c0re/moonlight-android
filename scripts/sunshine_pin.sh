#!/bin/bash
# Enter a Moonlight PIN into Sunshine on this machine. Nothing else.
#
#   scripts/sunshine_pin.sh 8730
#   scripts/sunshine_pin.sh
#
# Asks for the Sunshine web password on this terminal. Does not save it.
# Does not talk to any other device.
set -eu

PIN="${1:-}"
NAME="${SUNSHINE_CLIENT_NAME:-Titan}"
WEB="https://127.0.0.1:47990"
TTY=/dev/tty

USER_NAME="$(python3 -c 'import json,pathlib; print(json.loads((pathlib.Path.home()/".config/sunshine/sunshine_state.json").read_text()).get("username",""))')"
if [[ -z "$USER_NAME" ]]; then
    echo "No Sunshine username in ~/.config/sunshine/sunshine_state.json" >&2
    exit 1
fi

if [[ -t 0 ]]; then
    PROMPT_FD=0
elif [[ -r "$TTY" && -w "$TTY" ]]; then
    PROMPT_FD=3
    exec 3<>"$TTY"
else
    echo "Run this in your SSH session so it can ask for the web password." >&2
    exit 1
fi

if [[ -z "$PIN" ]]; then
    read -r -u "$PROMPT_FD" -p "PIN: " PIN
fi
case "$PIN" in
    [0-9][0-9][0-9][0-9]) ;;
    *)
        echo "PIN must be 4 digits" >&2
        exit 1
        ;;
esac

echo "PIN ${PIN}. The password is not shown as you type."
read -r -s -u "$PROMPT_FD" -p "Sunshine web password for ${USER_NAME}: " SUNSHINE_PASSWORD
echo
if [[ -z "$SUNSHINE_PASSWORD" ]]; then
    echo "No web password entered" >&2
    exit 1
fi
export SUNSHINE_PASSWORD PIN NAME WEB

python3 - <<'PY'
import base64, json, os, pathlib, ssl, time, urllib.error, urllib.request

ctx = ssl._create_unverified_context()
user = json.loads(
    (pathlib.Path.home() / ".config/sunshine/sunshine_state.json").read_text()
).get("username") or ""
password = os.environ["SUNSHINE_PASSWORD"]
pin = os.environ["PIN"]
name = os.environ["NAME"]
web = os.environ["WEB"]

def call(method, path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(web + path, data=data, method=method)
    token = base64.b64encode(f"{user}:{password}".encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=15) as res:
            return res.status, json.loads(res.read().decode() or "{}")
    except urllib.error.HTTPError as err:
        raw = err.read().decode(errors="replace")
        try:
            parsed = json.loads(raw)
        except Exception:
            parsed = {"error": raw[:200]}
        return err.code, parsed

code, body = call("POST", "/api/pin", {"pin": pin, "name": name})
print(f"pin HTTP {code} status={body.get('status')} error={body.get('error') or ''}")
if code == 401:
    raise SystemExit(1)
if code != 200 or not body.get("status"):
    print("Sunshine did not accept the PIN.")
    raise SystemExit(1)

clients = []
for _ in range(12):
    _ccode, cbody = call("GET", "/api/clients/list")
    rows = cbody.get("named_certs") or cbody.get("clients") or []
    if isinstance(rows, dict):
        rows = list(rows.values())
    clients = [r for r in rows if isinstance(r, dict)]
    if any((r.get("name") or "") == name for r in clients):
        break
    time.sleep(1)

match = [r for r in clients if (r.get("name") or "") == name]
target = match[-1] if match else (clients[-1] if clients else None)
if not target or not target.get("uuid"):
    print(f"PIN accepted. No client named {name} is in the list yet.")
    raise SystemExit(0)

ecode, ebody = call("POST", "/api/clients/update", {"uuid": target["uuid"], "enabled": True})
print(f"client {target.get('name')} enabled={ebody.get('status')} http={ecode}")
if ecode != 200 or ebody.get("status") is False:
    raise SystemExit(1)
PY
unset SUNSHINE_PASSWORD
