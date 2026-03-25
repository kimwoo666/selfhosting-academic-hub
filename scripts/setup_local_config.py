import argparse
import copy
import getpass
import json
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT_DIR / "config.local.json"
EXAMPLE_CONFIG = ROOT_DIR / "config.example.json"
PLACEHOLDER_VALUES = {"", "CHANGE_ME"}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def prompt_text(label: str, default: str = "", secret: bool = False) -> str:
    rendered_default = f" [{default}]" if default else ""
    prompt = f"{label}{rendered_default}: "
    if secret:
        value = getpass.getpass(prompt)
        return default if value == "" else value
    value = input(prompt).strip()
    return default if value == "" else value


def is_configured(value: str) -> bool:
    return str(value).strip() not in PLACEHOLDER_VALUES


def secret_default(value: str) -> str:
    return value if is_configured(value) else ""


def prompt_bool(label: str, default: bool) -> bool:
    suffix = "Y/n" if default else "y/N"
    value = input(f"{label} [{suffix}]: ").strip().lower()
    if not value:
        return default
    return value in {"y", "yes", "true", "1"}


def prompt_int(label: str, default: int) -> int:
    while True:
        value = input(f"{label} [{default}]: ").strip()
        if not value:
            return default
        try:
            return int(value)
        except ValueError:
            print("Enter a valid integer.")


def build_config(base: dict) -> dict:
    config = copy.deepcopy(base)

    print("== Proxmox ==")
    proxmox = config["proxmox"]
    proxmox["host"] = prompt_text("Proxmox host", proxmox.get("host", ""))
    proxmox["user"] = prompt_text("Proxmox user", proxmox.get("user", ""))
    proxmox["token_name"] = prompt_text("Proxmox token name", proxmox.get("token_name", ""))
    proxmox["token_value"] = prompt_text(
        "Proxmox token value",
        secret_default(proxmox.get("token_value", "")),
        secret=True,
    )
    proxmox["node"] = prompt_text("Default Proxmox node", proxmox.get("node", ""))
    proxmox["verify_ssl"] = prompt_bool("Verify Proxmox SSL", bool(proxmox.get("verify_ssl", False)))
    print()

    print("== Gemini ==")
    gemini = config["gemini"]
    gemini["api_key"] = prompt_text("Gemini API key", secret_default(gemini.get("api_key", "")), secret=True)
    gemini["model"] = prompt_text("Gemini model", gemini.get("model", "gemini-2.0-flash"))
    print()

    print("== LMS (optional) ==")
    lms = config["lms"]
    lms["username"] = prompt_text("LMS username", lms.get("username", ""))
    lms["password"] = prompt_text("LMS password", lms.get("password", ""), secret=True)
    lms["crawl_interval_minutes"] = prompt_int(
        "LMS crawl interval minutes",
        int(lms.get("crawl_interval_minutes", 60)),
    )
    print()

    print("== Google Calendar (optional) ==")
    calendar = config["google_calendar"]
    calendar["enabled"] = prompt_bool("Enable Google Calendar sync", bool(calendar.get("enabled", False)))
    if calendar["enabled"]:
        calendar["api_key"] = prompt_text(
            "Google Calendar API key",
            secret_default(calendar.get("api_key", "")),
            secret=True,
        )
        calendar["calendar_id"] = prompt_text("Google Calendar ID", calendar.get("calendar_id", "primary"))
        calendar["client_id"] = prompt_text("Google OAuth client ID", calendar.get("client_id", ""))
        calendar["client_secret"] = prompt_text(
            "Google OAuth client secret",
            secret_default(calendar.get("client_secret", "")),
            secret=True,
        )
        calendar["refresh_token"] = prompt_text(
            "Google OAuth refresh token",
            secret_default(calendar.get("refresh_token", "")),
            secret=True,
        )
    else:
        calendar["api_key"] = ""
        calendar["calendar_id"] = "primary"
        calendar["client_id"] = ""
        calendar["client_secret"] = ""
        calendar["refresh_token"] = ""
    print()

    print("== Server ==")
    server = config["server"]
    server["port"] = prompt_int("Backend port", int(server.get("port", 8080)))
    server["build_dir"] = prompt_text("Frontend build directory", server.get("build_dir", "build"))
    server["db_path"] = prompt_text("SQLite DB path", server.get("db_path", "academic_hub.db"))
    print()

    print("== UI ==")
    ui = config["ui"]
    ui["theme"] = prompt_text("UI theme", ui.get("theme", "dark"))
    print()

    return config


def print_summary(config_path: Path, config: dict) -> None:
    print("Config written.")
    print(f"- File: {config_path}")
    print(f"- Proxmox host: {config['proxmox']['host']}")
    print(f"- Proxmox user: {config['proxmox']['user']}")
    print(f"- Proxmox node: {config['proxmox']['node']}")
    print(f"- Proxmox token configured: {'yes' if is_configured(config['proxmox']['token_value']) else 'no'}")
    print(f"- Gemini configured: {'yes' if is_configured(config['gemini']['api_key']) else 'no'}")
    print(f"- LMS configured: {'yes' if bool(config['lms']['username'] or config['lms']['password']) else 'no'}")
    print(f"- Google Calendar enabled: {'yes' if bool(config['google_calendar']['enabled']) else 'no'}")
    print(f"- Backend port: {config['server']['port']}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create or update config.local.json in one guided flow.")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG), help="Path to the local config file to write.")
    parser.add_argument(
        "--write-defaults",
        action="store_true",
        help="Write the template values without interactive prompts.",
    )
    args = parser.parse_args()

    config_path = Path(args.config).expanduser().resolve()
    base = load_json(config_path) if config_path.exists() else load_json(EXAMPLE_CONFIG)

    if args.write_defaults or not sys.stdin.isatty():
        config = base
    else:
        print(f"Local setup wizard for {config_path.name}")
        print("Press Enter to keep the current/default value.")
        print()
        config = build_config(base)

    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print_summary(config_path, config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
