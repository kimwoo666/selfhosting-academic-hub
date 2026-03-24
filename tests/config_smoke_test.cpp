#include "config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <cstdlib>
static void set_env_var(const char* key, const char* value) { _putenv_s(key, value); }
#else
#include <cstdlib>
static void set_env_var(const char* key, const char* value) { setenv(key, value, 1); }
#endif

int main() {
    namespace fs = std::filesystem;

    const fs::path original_cwd = fs::current_path();
    const fs::path temp_dir = fs::temp_directory_path() / "jarvis_config_smoke";
    fs::create_directories(temp_dir);

    const fs::path local_config = temp_dir / "config.local.json";
    const fs::path explicit_config = temp_dir / "explicit.json";

    {
        std::ofstream ofs(local_config);
        ofs << R"({
  "proxmox": {
    "host": "https://127.0.0.1:8006",
    "user": "root@pam",
    "token_name": "jarvis",
    "token_value": "local-token",
    "node": "pve",
    "verify_ssl": false
  },
  "gemini": {
    "api_key": "local-key",
    "model": "gemini-2.0-flash"
  },
  "server": {
    "port": 8080,
    "build_dir": "build",
    "db_path": "jarvis.db"
  }
})";
    }

    {
        std::ofstream ofs(explicit_config);
        ofs << R"({
  "proxmox": {
    "host": "https://10.0.0.1:8006",
    "user": "jarvis@pam",
    "token_name": "explicit",
    "token_value": "explicit-token",
    "node": "pve2",
    "verify_ssl": true
  },
  "gemini": {
    "api_key": "explicit-key",
    "model": "gemini-2.5-flash"
  },
  "server": {
    "port": 9090,
    "build_dir": "dist",
    "db_path": "explicit.db"
  }
})";
    }

    fs::current_path(temp_dir);

    set_env_var("JARVIS_CONFIG", "");
    assert(resolve_config_path("missing.json") == "config.local.json");

    AppConfig local_cfg = load_config("missing.json");
    assert(local_cfg.gemini.api_key == "local-key");
    assert(local_cfg.server.port == 8080);

    set_env_var("JARVIS_CONFIG", explicit_config.string().c_str());
    assert(resolve_config_path("missing.json") == explicit_config.string());

    AppConfig explicit_cfg = load_config("missing.json");
    assert(explicit_cfg.gemini.model == "gemini-2.5-flash");
    assert(explicit_cfg.server.db_path == "explicit.db");
    assert(explicit_cfg.proxmox.verify_ssl);

    set_env_var("JARVIS_CONFIG", "");
    fs::current_path(original_cwd);
    fs::remove_all(temp_dir);
    return 0;
}
