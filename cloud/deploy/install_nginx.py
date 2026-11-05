"""Install only the Sprite location in the existing HTTPS vhost; rollback on failure."""

from datetime import datetime, timedelta, timezone
from pathlib import Path
import shutil
import subprocess

site = Path("/etc/nginx/sites-enabled/voice-code.yangguanxue.top")
snippet = Path("/etc/nginx/snippets/desk-sprite.conf")
include = "    include /etc/nginx/snippets/desk-sprite.conf;"
original = site.read_text()
needle = "    listen 443 ssl;\n    listen [::]:443 ssl;\n    server_name voice-code.yangguanxue.top;"
if include not in original and original.count(needle) != 1:
    raise SystemExit("HTTPS vhost layout differs; refusing to edit automatically.")
backup_dir = Path("/home/ubuntu/deploy-backups")
backup_dir.mkdir(exist_ok=True)
stamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d-%H%M%S")
backup = backup_dir / ("voice-code-before-sprite-" + stamp + ".conf")
shutil.copy2(site, backup)
previous_snippet = snippet.read_bytes() if snippet.exists() else None
try:
    shutil.copy2(Path(__file__).with_name("nginx-sprite.conf"), snippet)
    if include not in original:
        site.write_text(original.replace(needle, needle + "\n" + include))
    subprocess.run(["nginx", "-t"], check=True)
    subprocess.run(["systemctl", "reload", "nginx"], check=True)
except BaseException:
    shutil.copy2(backup, site)
    if previous_snippet is None:
        snippet.unlink(missing_ok=True)
    else:
        snippet.write_bytes(previous_snippet)
    subprocess.run(["nginx", "-t"], check=True)
    subprocess.run(["systemctl", "reload", "nginx"], check=True)
    raise
print("Sprite HTTPS location installed. Backup:", backup)
