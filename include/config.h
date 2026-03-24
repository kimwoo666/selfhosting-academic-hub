#pragma once
// ============================================================
// config.h — Load config.json at startup
// ============================================================
#include "nlohmann/json.hpp"
#include "google_calendar.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

struct ProxmoxConfig {
    std::string host;        // e.g. "https://192.168.1.1:8006"
    std::string user;        // e.g. "jarvis@pam"
    std::string token_name;  // e.g. "jarvis"
    std::string token_value; // UUID token
    std::string node;        // e.g. "pve"
    bool verify_ssl = false;
};

struct GeminiConfig {
    std::string api_key;
    std::string model; // e.g. "gemini-2.0-flash"
};

struct LmsConfig {
    std::string username;
    std::string password;
    int crawl_interval_minutes = 60;
};

struct ServerConfig {
    int port = 8080;
    std::string build_dir = "build";
    std::string db_path = "jarvis.db";
};

struct UiConfig {
    std::string theme = "dark"; // "dark" or "light"
};

struct AppConfig {
    ProxmoxConfig proxmox;
    GeminiConfig gemini;
    LmsConfig lms;
    GoogleCalendarConfig google_calendar;
    ServerConfig server;
    UiConfig ui;
};

inline std::string resolve_config_path(const std::string& preferred_path = "config.json") {
    namespace fs = std::filesystem;

    if (const char* env_path = std::getenv("JARVIS_CONFIG")) {
        if (*env_path) {
            return env_path;
        }
    }

    if (!preferred_path.empty() && fs::exists(preferred_path)) {
        return preferred_path;
    }

    if (fs::exists("config.local.json")) {
        return "config.local.json";
    }

    if (fs::exists("config.json")) {
        return "config.json";
    }

    throw std::runtime_error(
        "No config file found. Copy config.example.json to config.local.json "
        "or set JARVIS_CONFIG to an explicit path."
    );
}

inline AppConfig load_config(const std::string& path = "config.json") {
    const std::string resolved_path = resolve_config_path(path);
    std::ifstream ifs(resolved_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + resolved_path);
    }
    json j = json::parse(ifs);
    AppConfig cfg;

    // Proxmox
    auto& px = j["proxmox"];
    cfg.proxmox.host        = px.value("host", "https://127.0.0.1:8006");
    cfg.proxmox.user        = px.value("user", "jarvis@pam");
    cfg.proxmox.token_name  = px.value("token_name", "jarvis");
    cfg.proxmox.token_value = px.value("token_value", "");
    cfg.proxmox.node        = px.value("node", "pve");
    cfg.proxmox.verify_ssl  = px.value("verify_ssl", false);

    // Gemini
    auto& gm = j["gemini"];
    cfg.gemini.api_key = gm.value("api_key", "");
    cfg.gemini.model   = gm.value("model", "gemini-2.0-flash");

    // LMS
    if (j.contains("lms")) {
        auto& lm = j["lms"];
        cfg.lms.username               = lm.value("username", "");
        cfg.lms.password               = lm.value("password", "");
        cfg.lms.crawl_interval_minutes = lm.value("crawl_interval_minutes", 60);
    }

    // Google Calendar
    if (j.contains("google_calendar")) {
        auto& gc = j["google_calendar"];
        cfg.google_calendar.enabled       = gc.value("enabled", false);
        cfg.google_calendar.api_key       = gc.value("api_key", "");
        cfg.google_calendar.calendar_id   = gc.value("calendar_id", "primary");
        cfg.google_calendar.client_id     = gc.value("client_id", "");
        cfg.google_calendar.client_secret = gc.value("client_secret", "");
        cfg.google_calendar.refresh_token = gc.value("refresh_token", "");
    }

    // Server
    auto& sv = j["server"];
    cfg.server.port      = sv.value("port", 8080);
    cfg.server.build_dir = sv.value("build_dir", "build");
    cfg.server.db_path   = sv.value("db_path", "jarvis.db");

    // UI
    if (j.contains("ui")) {
        auto& ui = j["ui"];
        cfg.ui.theme = ui.value("theme", "dark");
    }

    return cfg;
}
