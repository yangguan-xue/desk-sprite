"""Run with sudo on the server after pulling the Broker image; never prints secrets."""

import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess

os.umask(0o077)
runtime = Path("/opt/desk-sprite/runtime")
runtime.mkdir(parents=True, exist_ok=True)
os.chown(runtime, 0, 1883)
runtime.chmod(0o710)
credentials_path = runtime / "credentials.json"
if not credentials_path.exists():
    credentials_path.write_text(json.dumps({
        "api_key": "spr_live_" + secrets.token_urlsafe(32),
        "webhook_secret": secrets.token_urlsafe(32),
        "cloud_admin": secrets.token_urlsafe(32),
        "host_macbook_pro": secrets.token_urlsafe(32),
        "dev_esp32_c049ef": secrets.token_urlsafe(32),
    }) + "\n")
credentials_path.chmod(0o600)
credentials = json.loads(credentials_path.read_text())
profile = {"owner_id": "owner_desk_sprite", "allowed_devices": ["esp32_c049ef"],
           "default_host": "macbook_pro", "allowed_hosts": ["macbook_pro"]}
api_keys = json.dumps({credentials["api_key"]: profile}, separators=(",", ":"))
env = runtime / "cloud.env"
env.write_text("MQTT_USERNAME=cloud_admin\n"
               + "MQTT_PASSWORD=" + credentials["cloud_admin"] + "\n"
               + "GITHUB_WEBHOOK_SECRET=" + credentials["webhook_secret"] + "\n"
               + "SPRITE_API_KEYS='" + api_keys + "'\n")
env.chmod(0o600)
password_file = runtime / "password.txt"
# Never rewrite the password file used by a running Broker on repeated setup.
if not password_file.exists():
    pending = runtime / "password.pending"
    try:
        pending.write_text("".join(name + ":" + credentials[name] + "\n" for name in
                                  ("cloud_admin", "host_macbook_pro", "dev_esp32_c049ef")))
        subprocess.run(["docker", "run", "--rm", "--user", "0:0", "--entrypoint", "mosquitto_passwd",
                        "-v", str(runtime) + ":/runtime", "eclipse-mosquitto:2.0.22",
                        "-U", "/runtime/password.pending"], check=True)
        pending.replace(password_file)
    finally:
        pending.unlink(missing_ok=True)
os.chown(password_file, 1883, 1883)
password_file.chmod(0o600)
acl = runtime / "acl.conf"
shutil.copyfile(Path(__file__).parent / "mosquitto/acl.conf", acl)
os.chown(acl, 1883, 1883)
acl.chmod(0o600)
certs = runtime / "certs"
certs.mkdir(exist_ok=True)
os.chown(certs, 0, 1883)
certs.chmod(0o750)
for source, target in (("voice-code.yangguanxue.top_bundle.pem", "server.crt"),
                       ("voice-code.yangguanxue.top.key", "server.key")):
    destination = certs / target
    shutil.copyfile(Path("/opt/certificate") / source, destination)
    os.chown(destination, 0, 1883)
    destination.chmod(0o640)
print("Runtime configured; secrets retained only in /opt/desk-sprite/runtime.")
