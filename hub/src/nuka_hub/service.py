# SPDX-License-Identifier: AGPL-3.0-or-later
import getpass
import os
import plistlib
import subprocess
import sys
import tempfile
from pathlib import Path
from xml.sax.saxutils import escape


def service(args, config_path: Path):
    role, action = args.role, args.action
    name = "nuka-" + role
    executable = Path(sys.executable)
    arguments = ["-m", "nuka_hub.cli", role, "--config", str(config_path.resolve())]
    if sys.platform == "win32":
        python = executable.with_name("pythonw.exe")
        if action == "install":
            level = "HighestAvailable" if args.elevated else "LeastPrivilege"
            xml = f"""<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
<RegistrationInfo>
<Author>nuka-hub</Author>
</RegistrationInfo>
<Triggers>
<LogonTrigger>
<Enabled>true</Enabled>
</LogonTrigger>
</Triggers>
<Principals>
<Principal id="Author">
<UserId>{escape(getpass.getuser())}</UserId>
<LogonType>InteractiveToken</LogonType>
<RunLevel>{level}</RunLevel>
</Principal>
</Principals>
<Settings>
<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
<Hidden>true</Hidden>
<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
<RestartOnFailure>
<Interval>PT1M</Interval>
<Count>3</Count>
</RestartOnFailure>
</Settings>
<Actions Context="Author">
<Exec>
<Command>{escape(str(python))}</Command>
<Arguments>{escape(subprocess.list2cmdline(arguments))}</Arguments>
</Exec>
</Actions>
</Task>"""
            with tempfile.TemporaryDirectory(prefix="nuka-task-") as folder:
                path = Path(folder) / "task.xml"
                path.write_text(xml, encoding="utf-16")
                subprocess.run(
                    ["schtasks", "/Create", "/TN", name, "/XML", str(path), "/F"], check=True
                )
        elif action == "uninstall":
            subprocess.run(
                [
                    str(
                        Path(os.environ["SystemRoot"])
                        / "System32/WindowsPowerShell/v1.0/powershell.exe"
                    ),
                    "-NoProfile",
                    "-NonInteractive",
                    "-Command",
                    f"Stop-ScheduledTask -TaskName '{name}' -ErrorAction Stop",
                ],
                check=True,
                creationflags=0x08000000,
            )
            subprocess.run(["schtasks", "/Delete", "/TN", name, "/F"], check=True)
        else:
            subprocess.run(["schtasks", "/Query", "/TN", name, "/V"], check=True)
    elif sys.platform == "darwin":
        label = "io.nuka." + role
        path = Path.home() / "Library/LaunchAgents" / (label + ".plist")
        target = "gui/" + str(os.getuid())
        if action == "install":
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(
                plistlib.dumps(
                    {
                        "Label": label,
                        "ProgramArguments": [str(executable), *arguments],
                        "RunAtLoad": True,
                        "KeepAlive": True,
                        "ProcessType": "Background",
                    }
                )
            )
            subprocess.run(["launchctl", "bootstrap", target, str(path)], check=True)
        elif action == "uninstall":
            subprocess.run(["launchctl", "bootout", target + "/" + label], check=True)
            path.unlink()
        else:
            subprocess.run(["launchctl", "print", target + "/" + label], check=True)
    else:
        folder = (
            Path("/etc/systemd/system") if args.system else Path.home() / ".config/systemd/user"
        )
        path = folder / (name + ".service")
        command = ["systemctl"] if args.system else ["systemctl", "--user"]
        if action == "install":
            folder.mkdir(parents=True, exist_ok=True)

            def quote(value):
                return (
                    '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'
                )

            unit = (
                "[Unit]\nDescription=nuka companion "
                + role
                + "\nAfter=network-online.target\n[Service]\nType=simple\nExecStart="
                + " ".join(quote(value) for value in [str(executable), *arguments])
                + "\nRestart=on-failure\nRestartSec=5\n[Install]\nWantedBy="
                + ("multi-user.target" if args.system else "default.target")
                + "\n"
            )
            path.write_text(unit, encoding="utf-8")
            subprocess.run([*command, "daemon-reload"], check=True)
            subprocess.run([*command, "enable", "--now", path.name], check=True)
            if not args.system:
                print("For startup before login, run loginctl enable-linger for your user.")
        elif action == "uninstall":
            subprocess.run([*command, "disable", "--now", path.name], check=True)
            path.unlink()
            subprocess.run([*command, "daemon-reload"], check=True)
        else:
            subprocess.run([*command, "status", path.name], check=True)
