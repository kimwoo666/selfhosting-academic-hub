// ============================================================
// Self-Hosting Academic Hub: Proxmox-Native AI Orchestrator - C++ Backend
// Target: Proxmox LXC (Ubuntu 24.04), C++20
// Phase 5: SSU Academic Integration ??Notice crawling,
//          LMS sync, Google Calendar write, LLM analysis
// ============================================================

#include "crow.h"
#include "nlohmann/json.hpp"

// Academic hub modules
#include "config.h"
#include "database.h"
#include "gemini.h"
#include "proxmox.h"
#include "google_calendar.h"
#include "notice_crawler.h"
#include "lms_crawler.h"
#include "saint_crawler.h"
#include <curl/curl.h>

#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <set>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================
// Helpers
// ============================================================
static std::string read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::string mime_type(const std::string& path) {
    static const std::unordered_map<std::string, std::string> types = {
        {".html", "text/html"}, {".css", "text/css"},
        {".js", "application/javascript"}, {".json", "application/json"},
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif", "image/gif"}, {".svg", "image/svg+xml"}, {".ico", "image/x-icon"},
        {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".ttf", "font/ttf"},
        {".map", "application/json"},
    };
    auto ext = fs::path(path).extension().string();
    auto it = types.find(ext);
    return (it != types.end()) ? it->second : "application/octet-stream";
}

static crow::response json_response(const json& data) {
    auto resp = crow::response(200, data.dump());
    resp.add_header("Content-Type", "application/json");
    return resp;
}

static crow::response json_error(int code, const std::string& msg) {
    json err = {{"error", msg}};
    auto resp = crow::response(code, err.dump());
    resp.add_header("Content-Type", "application/json");
    return resp;
}

// Get today's date as YYYY-MM-DD
static std::string today_date() {
    time_t now = time(nullptr);
    struct tm* local = localtime(&now);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", local);
    return buf;
}

// ============================================================
// MAIN
// ============================================================
int main() {
    // ???? Load config ????
    AppConfig cfg;
    std::string config_path;
    try {
        config_path = resolve_config_path("config.json");
        cfg = load_config(config_path);
        CROW_LOG_INFO << "Config loaded from " << config_path;
    } catch (const std::exception& e) {
        CROW_LOG_ERROR << "Failed to load config: " << e.what();
        return 1;
    }

    // libcurl global init MUST happen before any curl_easy_init() usage.
    CURLcode curl_init_rc = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_init_rc != CURLE_OK) {
        CROW_LOG_ERROR << "curl_global_init failed: " << curl_easy_strerror(curl_init_rc);
        return 1;
    }

    // ???? Initialize services ????
    Database db(cfg.server.db_path);
    CROW_LOG_INFO << "SQLite database: " << cfg.server.db_path;

    GeminiClient gemini(cfg.gemini);
    CROW_LOG_INFO << "Gemini API: model=" << cfg.gemini.model
                  << " configured=" << (!cfg.gemini.api_key.empty() ? "yes" : "no");

    ProxmoxClient proxmox(cfg.proxmox);
    CROW_LOG_INFO << "Proxmox API: host=" << cfg.proxmox.host
                  << " user=" << cfg.proxmox.user
                  << " token=" << (!cfg.proxmox.token_value.empty() ? "set" : "unset");

    GoogleCalendarClient gcal(cfg.google_calendar);
    if (gcal.is_enabled()) {
        CROW_LOG_INFO << "Google Calendar: enabled, calendar=" << cfg.google_calendar.calendar_id;
    } else {
        CROW_LOG_INFO << "Google Calendar: disabled";
    }

    // SSU Academic modules
    NoticeCrawler notice_crawler;
    LmsCrawler lms_crawler(cfg.lms);
    SaintCrawler saint_crawler(cfg.lms);
    CROW_LOG_INFO << "SSU Notice Crawler: initialized";
    CROW_LOG_INFO << "SSU LMS Crawler: " << (cfg.lms.username.empty() ? "not configured" : "configured");
    
    // UI Config
    std::string current_theme = cfg.ui.theme;
    CROW_LOG_INFO << "UI Theme: " << current_theme;

    // Thread safety for DB access
    std::mutex db_mutex;
    std::mutex lms_mutex;
    std::mutex saint_mutex;

    crow::SimpleApp app;
    const std::string& build_dir = cfg.server.build_dir;

    // ----------------------------------------------------------
    // REST API: Dashboard Summary
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/summary")
    ([&]() {
        json result;

        try {
            result = proxmox.get_summary_system();
        } catch (...) {
            result = {
                {"system", {{"cpu", 0}, {"ram", 0}, {"disk", 0}, {"temp", 0}}},
                {"uptime", "N/A (Proxmox unreachable)"},
                {"active_nodes", 0}, {"total_nodes", 0},
                {"containers", json::array()},
                {"network", {{"download_mbps", 0}, {"upload_mbps", 0}}}
            };
        }

        result["kernel"] = "6.5.0-generic";
        result["ai_status"] = "idle";

        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto events = db.get_events();
            if (!events["events"].empty()) {
                auto& first = events["events"][0];
                result["next_task"] = {
                    {"title", first.value("title", "")},
                    {"d_day", "D-1"},
                    {"course", ""},
                    {"due", first.value("time", "")}
                };
            }
        }

        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto alerts = db.get_alerts();
            result["active_alerts"] = (int)alerts.size();
        }

        // Notices from database (crawled from SSU)
        result["notices"] = json::array();
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto all_notices = db.get_notices(5);
            if (all_notices.is_array()) {
                for (auto& n : all_notices) {
                    int score = n.value("relevance_score", 50);
                    result["notices"].push_back({
                        {"title", n.value("title", "")},
                        {"description", n.value("ai_summary", n.value("title", ""))},
                        {"match", score},
                        {"date", n.value("date", "")}
                    });
                }
            }
        }

        {
            auto sys = result.value("system", json::object());
            double cpu = sys.value("cpu", 0.0);
            double ram = sys.value("ram", 0.0);
            double disk = sys.value("disk", 0.0);
            int alert_count = result.value("active_alerts", 0);
            int active_nodes = result.value("active_nodes", 0);
            int total_nodes = result.value("total_nodes", 0);

            json insights = json::array();
            insights.push_back({
                {"level", cpu >= 85.0 ? "warning" : "success"},
                {"message", cpu >= 85.0 ? "CPU usage is high. Investigate heavy workloads." : "CPU usage is stable."}
            });
            insights.push_back({
                {"level", ram >= 85.0 ? "warning" : "info"},
                {"message", ram >= 85.0 ? "RAM pressure detected. Consider scaling memory." : "Memory headroom is healthy."}
            });
            insights.push_back({
                {"level", disk >= 90.0 ? "warning" : "info"},
                {"message", disk >= 90.0 ? "Disk usage is near capacity. Cleanup recommended." : "Disk usage is within safe range."}
            });
            if (alert_count > 0) {
                insights.push_back({{"level", "warning"}, {"message", "There are active alerts requiring attention."}});
            }
            if (total_nodes > 0 && active_nodes == 0) {
                insights.push_back({{"level", "warning"}, {"message", "No active nodes detected in the cluster."}});
            }
            result["ai_insights"] = insights;
        }
        result["priorities"] = json::array();
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto events = db.get_events();
            for (auto& ev : events["events"]) {
                result["priorities"].push_back({
                    {"id", ev.value("id", 0)},
                    {"title", ev.value("title", "")},
                    {"tag", ev.value("done", false) ? "DONE" : (ev.value("type", "") == "Exam" ? "URGENT" : "STUDY")},
                    {"due", ev.value("time", "")},
                    {"course", ""},
                    {"done", ev.value("done", false)},
                    {"ai_action", ev.value("done", false) ? "" : "Summarize Requirements"}
                });
            }
        }

        return json_response(result);
    });

    // ----------------------------------------------------------
    // REST API: Nodes (from Proxmox)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/nodes")
    ([&]() {
        try {
            auto nodes = proxmox.get_nodes_for_frontend();
            return json_response(nodes);
        } catch (const std::exception& e) {
            return json_error(502, std::string("Proxmox error: ") + e.what());
        }
    });

    // ----------------------------------------------------------
    // REST API: Node control (via Proxmox)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/nodes/<int>/control").methods("POST"_method)
    ([&](const crow::request& req, int id) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string action = body.value("action", "");
        if (action != "start" && action != "stop" && action != "restart"
            && action != "reboot" && action != "shutdown") {
            return json_error(400, "Invalid action. Use: start, stop, restart");
        }

        try {
            auto result = proxmox.control_container(id, action);
            return json_response(result);
        } catch (const std::exception& e) {
            return json_error(502, std::string("Proxmox error: ") + e.what());
        }
    });

    // ----------------------------------------------------------
    // REST API: Node deploy (clone from LXC template)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/nodes/deploy").methods("POST"_method)
    ([&](const crow::request& req) {
        std::string base_name = "academic-hub-node";
        if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body);
                if (body.contains("name") && body["name"].is_string()) {
                    std::string n = body["name"].get<std::string>();
                    if (!n.empty()) base_name = n;
                }
            } catch (...) {
                // Use default name if body parse fails.
            }
        }

        try {
            auto result = proxmox.deploy_container(base_name);
            int code = result.value("success", false) ? 200 : 502;
            auto resp = crow::response(code, result.dump());
            resp.add_header("Content-Type", "application/json");
            return resp;
        } catch (const std::exception& e) {
            return json_error(502, std::string("Proxmox deploy error: ") + e.what());
        }
    });

    // ----------------------------------------------------------
    // REST API: Calendar Events
    // ----------------------------------------------------------
    // GET /api/calendar/events?date=YYYY-MM-DD
    CROW_ROUTE(app, "/api/calendar/events")
    ([&](const crow::request& req) {
        std::string date = today_date();
        auto date_param = req.url_params.get("date");
        if (date_param) date = date_param;

        std::lock_guard<std::mutex> lock(db_mutex);
        auto result = db.get_events_for_date(date);

        // Merge Google Calendar events if enabled
        if (gcal.is_enabled()) {
            try {
                // Get Google events for this specific date
                int year, month, day;
                sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);
                char time_min[32], time_max[32];
                snprintf(time_min, sizeof(time_min), "%04d-%02d-%02dT00:00:00Z", year, month, day);
                snprintf(time_max, sizeof(time_max), "%04d-%02d-%02dT23:59:59Z", year, month, day);
                auto google_events = gcal.get_events(time_min, time_max);
                for (auto& gev : google_events) {
                    result["events"].push_back(gev);
                }
            } catch (...) {
                CROW_LOG_WARNING << "Google Calendar fetch failed";
            }
        }

        return json_response(result);
    });

    // POST /api/calendar/events ??add new event
    CROW_ROUTE(app, "/api/calendar/events").methods("POST"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string date = body.value("date", today_date());
        std::string time = body.value("time", "");
        std::string type = body.value("type", "Task");
        std::string title = body.value("title", "");
        std::string location = body.value("location", "");
        std::string duration = body.value("duration", "");
        std::string description = body.value("description", "");

        if (title.empty()) {
            return json_error(400, "Title is required");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        int id = db.add_event(date, time, type, title, location, duration, description);
        return json_response({{"success", true}, {"id", id}, {"message", "Event created"}});
    });

    // PUT /api/calendar/events/<id> ??update existing event
    CROW_ROUTE(app, "/api/calendar/events/<int>").methods("PUT"_method)
    ([&](const crow::request& req, int id) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string date = body.value("date", today_date());
        std::string time = body.value("time", "");
        std::string type = body.value("type", "Task");
        std::string title = body.value("title", "");
        std::string location = body.value("location", "");
        std::string duration = body.value("duration", "");
        std::string description = body.value("description", "");

        if (title.empty()) {
            return json_error(400, "Title is required");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        bool ok = db.update_event(id, date, time, type, title, location, duration, description);
        return json_response({{"success", ok}, {"message", ok ? "Event updated" : "Event not found"}});
    });

    // DELETE /api/calendar/events/<id>
    CROW_ROUTE(app, "/api/calendar/events/<int>").methods("DELETE"_method)
    ([&](int id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        bool ok = db.delete_event(id);
        return json_response({{"success", ok}, {"message", ok ? "Event deleted" : "Event not found"}});
    });

    // POST /api/calendar/events/<id>/toggle ??toggle done
    CROW_ROUTE(app, "/api/calendar/events/<int>/toggle").methods("POST"_method)
    ([&](int id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        bool ok = db.toggle_event(id);
        return json_response({{"success", ok}});
    });

    // POST /api/calendar/events/<id>/summarize ??AI summarize requirements
    CROW_ROUTE(app, "/api/calendar/events/<int>/summarize").methods("POST"_method)
    ([&](int id) {
        json event;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto events = db.get_events();
            for (auto& ev : events["events"]) {
                if (ev.value("id", 0) == id) {
                    event = ev;
                    break;
                }
            }
        }
        if (event.empty()) {
            return json_error(404, "Event not found");
        }

        std::string title = event.value("title", "Unknown");
        std::string type = event.value("type", "Task");
        std::string desc = event.value("description", "");

        std::string prompt = "Summarize the requirements for this academic task in 2-3 concise bullet points.\n"
                            "Task: " + title + "\nType: " + type;
        if (!desc.empty()) prompt += "\nDescription: " + desc;
        
        try {
            std::string summary = gemini.chat(
                "You are a concise academic assistant. Provide brief, actionable bullet points.",
                prompt
            );
            return json_response({{"success", true}, {"summary", summary}});
        } catch (const std::exception& e) {
            return json_error(500, std::string("AI error: ") + e.what());
        }
    });

    // GET /api/calendar/month?year=YYYY&month=MM ??event dates in month
    CROW_ROUTE(app, "/api/calendar/month")
    ([&](const crow::request& req) {
        auto y_param = req.url_params.get("year");
        auto m_param = req.url_params.get("month");

        time_t now = time(nullptr);
        struct tm* local = localtime(&now);
        int year = y_param ? std::stoi(y_param) : (local->tm_year + 1900);
        int month = m_param ? std::stoi(m_param) : (local->tm_mon + 1);

        std::lock_guard<std::mutex> lock(db_mutex);
        auto dates = db.get_event_dates_in_month(year, month);

        // Also get Google Calendar events for the month
        json google_dates = json::array();
        if (gcal.is_enabled()) {
            try {
                auto gevents = gcal.get_month_events(year, month);
                for (auto& gev : gevents) {
                    google_dates.push_back(gev["date"]);
                }
            } catch (...) {}
        }

        return json_response({
            {"dates", dates},
            {"google_dates", google_dates},
            {"year", year},
            {"month", month}
        });
    });

    // ----------------------------------------------------------
    // REST API: Academic Grades (from SQLite)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/academic/grades")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_grades());
    });

    // POST /api/academic/grades/sync -- sync GPA/rank from SAINT (BLOCKING LEGACY: Kept for compat, but triggers async now?)
    // Actually, let's keep this as blocking if we want immediate result,
    // or modify it. The user flow in Grades.tsx was modified to use a new async flow.
    // Let's add the NEW endpoints.

    // POST /api/academic/crawl/start
    CROW_ROUTE(app, "/api/academic/crawl/start").methods("POST"_method)
    ([&](const crow::request& req) {
         if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body);
                std::string student_id = body.value("student_id", "");
                std::string password = body.value("password", "");
                if (!student_id.empty() && !password.empty()) {
                    std::lock_guard<std::mutex> saint_lock(saint_mutex);
                    saint_crawler.set_credentials(student_id, password);
                }
            } catch (...) {}
        }
        
        {
            std::lock_guard<std::mutex> saint_lock(saint_mutex);
            saint_crawler.start_crawl();
        }
        
        return json_response({{"success", true}, {"message", "Crawler started"}});
    });
    
    // GET /api/academic/crawl/status
    CROW_ROUTE(app, "/api/academic/crawl/status")
    ([&]() {
        // No lock needed on generic mutex, saint_crawler handles its own locking
        std::string status = saint_crawler.get_status();
        bool active = saint_crawler.is_crawling();
        json result = saint_crawler.get_last_result();
        
        return json_response({
            {"status", status},
            {"active", active},
            {"last_result", result}
        });
    });

    // POST /api/academic/grades/sync -- sync GPA/rank from SAINT (Blocking Legacy)
    CROW_ROUTE(app, "/api/academic/grades/sync").methods("POST"_method)
    ([&](const crow::request& req) {
        if (!req.body.empty()) {
            try {
                auto body = json::parse(req.body);
                std::string student_id = body.value("student_id", "");
                std::string password = body.value("password", "");
                if (!student_id.empty() && !password.empty()) {
                    std::lock_guard<std::mutex> saint_lock(saint_mutex);
                    saint_crawler.set_credentials(student_id, password);
                }
            } catch (...) {
                // Ignore parse error and use existing in-memory credentials
            }
        }

        json result;
        try {
            {
                // blocking call
                std::lock_guard<std::mutex> saint_lock(saint_mutex);
                result = saint_crawler.fetch_latest_grade_summary();
            }
            if (result.value("success", false)) {
                double gpa = result.value("gpa", 0.0);
                double credits = result.value("credits", 0.0);
                std::string rank = result.value("rank", "--");
                std::string semester = result.value("semester", "");
                
                std::lock_guard<std::mutex> lock(db_mutex);
                
                // Update Summary
                bool ok_summary = db.update_academic_summary(gpa, credits, rank, semester);
                
                // Update Detailed Grades
                bool ok_details = true; // Default true if no details provided (legacy)
                if (result.contains("semesters_data")) {
                    ok_details = db.save_semester_grades(result["semesters_data"]);
                }
                
                result["db_updated"] = ok_summary && ok_details;
                result["message"] = (ok_summary && ok_details)
                    ? "SAINT grade summary and details synced"
                    : "SAINT grade summary synced, but details failed";
                result["success"] = ok_summary;
            }
        } catch (const std::exception& e) {
            return json_error(500, std::string("SAINT grade sync error: ") + e.what());
        }

        int code = result.value("success", false) ? 200 : 502;
        auto resp = crow::response(code, result.dump());
        resp.add_header("Content-Type", "application/json");
        return resp;
    });

    // ----------------------------------------------------------
    // REST API: Debug - Session isolation verification
    // ----------------------------------------------------------
    // Verifies whether LMS and SAINT sessions invalidate each other.
    CROW_ROUTE(app, "/api/debug/session-isolation").methods("POST"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string student_id = body.value("student_id", "");
        std::string password = body.value("password", "");
        if (student_id.empty() || password.empty()) {
            return json_error(400, "student_id and password are required");
        }

        json result = {
            {"success", true},
            {"student_id", student_id},
            {"steps", json::object()},
            {"conclusion", json::object()}
        };

        // 1) LMS login + probe
        {
            std::lock_guard<std::mutex> lms_lock(lms_mutex);
            lms_crawler.set_credentials(student_id, password);
            bool ok = lms_crawler.sso_login();
            result["steps"]["lms_login_ok"] = ok;
            result["steps"]["lms_after_login"] = lms_crawler.probe_session();
        }

        // 2) SAINT pre-login probe (baseline)
        {
            std::lock_guard<std::mutex> saint_lock(saint_mutex);
            result["steps"]["saint_before_login"] = saint_crawler.probe_session();
        }

        // 3) SAINT login + probe
        {
            std::lock_guard<std::mutex> saint_lock(saint_mutex);
            saint_crawler.set_credentials(student_id, password);
            bool ok = saint_crawler.login();
            result["steps"]["saint_login_ok"] = ok;
            result["steps"]["saint_after_login"] = saint_crawler.probe_session();
        }

        // 4) LMS probe after SAINT login
        {
            std::lock_guard<std::mutex> lms_lock(lms_mutex);
            result["steps"]["lms_after_saint_login"] = lms_crawler.probe_session();
        }

        // 5) LMS re-login + probe
        {
            std::lock_guard<std::mutex> lms_lock(lms_mutex);
            bool ok = lms_crawler.sso_login();
            result["steps"]["lms_relogin_ok"] = ok;
            result["steps"]["lms_after_relogin"] = lms_crawler.probe_session();
        }

        // 6) SAINT probe after LMS re-login
        {
            std::lock_guard<std::mutex> saint_lock(saint_mutex);
            result["steps"]["saint_after_lms_relogin"] = saint_crawler.probe_session();
        }

        auto alive_of = [](const json& j) {
            return j.is_object() && j.value("alive", false);
        };
        bool lms_alive_after_login = alive_of(result["steps"]["lms_after_login"]);
        bool saint_alive_after_login = alive_of(result["steps"]["saint_after_login"]);
        bool lms_alive_after_saint = alive_of(result["steps"]["lms_after_saint_login"]);
        bool saint_alive_after_lms = alive_of(result["steps"]["saint_after_lms_relogin"]);

        result["conclusion"]["lms_invalidated_by_saint_login"] =
            lms_alive_after_login && !lms_alive_after_saint;
        result["conclusion"]["saint_invalidated_by_lms_login"] =
            saint_alive_after_login && !saint_alive_after_lms;
        result["conclusion"]["both_can_stay_alive_simultaneously"] =
            lms_alive_after_saint && saint_alive_after_lms;

        return json_response(result);
        return json_response(result);
    });

    // ----------------------------------------------------------
    // REST API: Settings (Theme & Config)
    // ----------------------------------------------------------
    // GET /api/settings ??get current UI settings
    CROW_ROUTE(app, "/api/settings")
    ([&]() {
        // Reload config to get latest (in case changed by another process, though unlikely here)
        // For simplicity, we stick to in-memory 'current_theme' but usually we'd want a unified config store.
        // We'll just return what we have in memory for now, updated by the POST.
        
        json j = {
            {"theme", current_theme},
            {"notifications", true} // placeholder
        };
        return json_response(j);
    });

    // POST /api/settings/theme ??update theme
    CROW_ROUTE(app, "/api/settings/theme").methods("POST"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string new_theme = body.value("theme", "");
        if (new_theme != "light" && new_theme != "dark") {
            return json_error(400, "Theme must be 'light' or 'dark'");
        }

        // Update in-memory
        current_theme = new_theme;

        // Persist to config.json
        // We read the existing file, update the field, and write back to preserve other comments/structure if possible
        // (nlohmann/json dumps will remove comments, but that's the trade-off)
        try {
            std::ifstream ifs("config.json");
            json j = json::parse(ifs);
            ifs.close();

            j["ui"]["theme"] = new_theme;

            std::ofstream ofs("config.json");
            ofs << j.dump(4); // pretty print with 4 spaces
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Failed to save config: " << e.what();
            return json_error(500, "Failed to persist setting");
        }

        return json_response({{"success", true}, {"theme", current_theme}});
    });

    // ----------------------------------------------------------
    // REST API: Alerts (from SQLite)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/alerts")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        json merged = db.get_alerts();
        if (!merged.is_array()) merged = json::array();

        std::set<std::string> seen_titles;
        for (auto& a : merged) {
            std::string title = a.value("title", "");
            if (!title.empty()) seen_titles.insert(title);
        }

        auto notices = db.get_notices(30);
        if (notices.is_array()) {
            for (auto& n : notices) {
                std::string title = n.value("title", "");
                std::string source = n.value("source", "");
                if (title.empty()) continue;

                std::string category = "Academic";
                if (seen_titles.find(title) != seen_titles.end()) continue;
                seen_titles.insert(title);

                int score = n.value("relevance_score", 0);
                std::string severity = score >= 95 ? "critical" : "info";
                std::string color = score >= 95 ? "red" : (score >= 90 ? "amber" : "primary");

                std::string source_label = "NOTICE";
                if (source == "aix") source_label = "AIX";
                else if (source == "sw") source_label = "SW";
                else if (source.compare(0, 7, "usaint_") == 0) source_label = "u-SAINT";
                else if (source.compare(0, 7, "custom:") == 0) source_label = source.substr(7);

                std::string ai_summary = n.value("ai_summary", "");
                std::string message = ai_summary.empty()
                    ? ("[" + source_label + "] New notice")
                    : ("[" + source_label + "] " + ai_summary);

                std::string time = n.value("date", "");
                if (time.empty()) {
                    std::string created_at = n.value("created_at", "");
                    if (created_at.size() >= 16) time = created_at.substr(5, 11); // MM-DD HH:MM
                    else if (!created_at.empty()) time = created_at;
                    else time = "now";
                }

                merged.push_back({
                    {"severity", severity},
                    {"category", category},
                    {"title", title},
                    {"message", message},
                    {"time", time},
                    {"color", color}
                });
            }
        }

        return json_response(merged);
    });

    // ----------------------------------------------------------
    // REST API: Notice Board Crawling
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/notice-sources")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_notice_sources());
    });

    CROW_ROUTE(app, "/api/notice-sources").methods("POST"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        const std::string name = body.value("name", "");
        const std::string url = body.value("url", "");
        const std::string title_selector = body.value("title_selector", "");
        const bool enabled = body.value("enabled", true);

        if (name.empty() || url.empty() || title_selector.empty()) {
            return json_error(400, "name, url, and title_selector are required");
        }

        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            const int id = db.add_notice_source(name, url, title_selector, enabled);
            return json_response({{"success", true}, {"id", id}});
        } catch (const std::exception& e) {
            return json_error(500, std::string("Failed to add notice source: ") + e.what());
        }
    });

    CROW_ROUTE(app, "/api/notice-sources/<int>").methods("PUT"_method)
    ([&](const crow::request& req, int id) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        const std::string name = body.value("name", "");
        const std::string url = body.value("url", "");
        const std::string title_selector = body.value("title_selector", "");
        const bool enabled = body.value("enabled", true);

        if (name.empty() || url.empty() || title_selector.empty()) {
            return json_error(400, "name, url, and title_selector are required");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        const bool updated = db.update_notice_source(id, name, url, title_selector, enabled);
        if (!updated) return json_error(404, "Notice source not found");
        return json_response({{"success", true}});
    });

    CROW_ROUTE(app, "/api/notice-sources/<int>").methods("DELETE"_method)
    ([&](int id) {
        std::lock_guard<std::mutex> lock(db_mutex);
        const bool removed = db.delete_notice_source(id);
        if (!removed) return json_error(404, "Notice source not found");
        return json_response({{"success", true}});
    });

    // POST /api/notices/crawl ??trigger notice crawl + LLM analysis
    CROW_ROUTE(app, "/api/notices/crawl").methods("POST"_method)
    ([&](const crow::request&) {
        try {
            std::lock_guard<std::mutex> lock(db_mutex);
            json result = notice_crawler.crawl_and_process(db, gemini);
            result["success"] = true;
            return json_response(result);
        } catch (const std::exception& e) {
            return json_error(500, std::string("Crawl failed: ") + e.what());
        }
    });

    // GET /api/notices ??list all notices with scores
    CROW_ROUTE(app, "/api/notices")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_notices());
    });

    // ----------------------------------------------------------
    // REST API: LMS Integration
    // ----------------------------------------------------------
    // POST /api/lms/login ??set credentials and attempt SSO login
    CROW_ROUTE(app, "/api/lms/login").methods("POST"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::string student_id = body.value("student_id", "");
        std::string password = body.value("password", "");

        if (student_id.empty() || password.empty()) {
            return json_error(400, "student_id and password are required");
        }

        try {
            bool ok = false;
            {
                std::lock_guard<std::mutex> lms_lock(lms_mutex);
                lms_crawler.set_credentials(student_id, password);
                ok = lms_crawler.sso_login();
            }
            if (ok) {
                // Trigger background sync after successful login
                try {
                    std::lock_guard<std::mutex> lms_lock(lms_mutex);
                    std::lock_guard<std::mutex> lock(db_mutex);
                    lms_crawler.sync(db);
                } catch (...) {
                    // Sync failure doesn't affect login success
                }
                json grade_sync = {
                    {"success", false},
                    {"message", "SAINT grades sync not attempted"}
                };
                try {
                    {
                        std::lock_guard<std::mutex> saint_lock(saint_mutex);
                        saint_crawler.set_credentials(student_id, password);
                        grade_sync = saint_crawler.fetch_latest_grade_summary();
                    }
                    if (grade_sync.value("success", false)) {
                        double gpa = grade_sync.value("gpa", 0.0);
                        double credits = grade_sync.value("credits", 0.0);
                        std::string rank = grade_sync.value("rank", "--");
                        std::string semester = grade_sync.value("semester", "");
                        std::lock_guard<std::mutex> lock(db_mutex);
                        bool ok_summary = db.update_academic_summary(gpa, credits, rank, semester);
                        bool ok_details = true;
                        if (grade_sync.contains("semesters_data")) {
                            ok_details = db.save_semester_grades(grade_sync["semesters_data"]);
                        }
                        bool db_ok = ok_summary && ok_details;
                        grade_sync["db_updated"] = db_ok;
                        grade_sync["message"] = db_ok
                            ? "SAINT grade summary and semester history synced to Grades dashboard"
                            : "SAINT grade summary synced, but semester history update failed";
                        grade_sync["success"] = db_ok;
                    }
                } catch (const std::exception& e) {
                    grade_sync = {
                        {"success", false},
                        {"message", std::string("SAINT grades sync error: ") + e.what()}
                    };
                }

                std::string msg = "LMS 濡쒓렇???깃났. 怨쇰ぉ 諛??쇱젙???숆린?붾릺?덉뒿?덈떎.";
                if (grade_sync.value("success", false)) {
                    msg += " SAINT grades (Current GPA, Class Rank) were synced.";
                } else {
                    std::string grade_msg = grade_sync.value("message", "");
                    if (!grade_msg.empty()) {
                        msg += " SAINT grade sync incomplete: " + grade_msg;
                    } else {
                        msg += " SAINT grade sync incomplete.";
                    }
                }
                return json_response({
                    {"success", true},
                    {"message", msg},
                    {"student_id", student_id},
                    {"grades_sync", grade_sync}
                });
            } else {
                return json_response({
                    {"success", false},
                    {"message", "SSO 濡쒓렇???ㅽ뙣. ?숇쾲怨?鍮꾨?踰덊샇瑜??뺤씤?댁＜?몄슂."}
                });
            }
        } catch (const std::exception& e) {
            return json_error(500, std::string("LMS login error: ") + e.what());
        }
    });

    // GET /api/lms/status - current in-memory LMS session state
    CROW_ROUTE(app, "/api/lms/status")
    ([&]() {
        std::lock_guard<std::mutex> lms_lock(lms_mutex);
        bool logged = lms_crawler.is_logged_in();
        return json_response({
            {"logged_in", logged},
            {"student_id", logged ? lms_crawler.current_username() : ""}
        });
    });

    // POST /api/lms/sync ??login to SSU LMS + scrape courses/deadlines
    CROW_ROUTE(app, "/api/lms/sync").methods("POST"_method)
    ([&](const crow::request&) {
        try {
            std::lock_guard<std::mutex> lms_lock(lms_mutex);
            std::lock_guard<std::mutex> lock(db_mutex);
            json result = lms_crawler.sync(db);
            return json_response(result);
        } catch (const std::exception& e) {
            return json_error(500, std::string("LMS sync failed: ") + e.what());
        }
    });

    // GET /api/lms/courses ??list enrolled courses
    CROW_ROUTE(app, "/api/lms/courses")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_lms_courses());
    });

    // GET /api/lms/deadlines ??list all deadlines
    CROW_ROUTE(app, "/api/lms/deadlines")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_lms_deadlines());
    });

    // POST /api/lms/sync-to-calendar ??push unsynced deadlines to Google Calendar
    CROW_ROUTE(app, "/api/lms/sync-to-calendar").methods("POST"_method)
    ([&](const crow::request&) {
        if (!gcal.has_write_access()) {
            return json_error(400, "Google Calendar OAuth2 not configured");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        auto unsynced = db.get_unsynced_deadlines();
        int synced_count = 0;

        for (auto& dl : unsynced) {
            std::string title = "[" + dl["course"].get<std::string>() + "] "
                              + dl["title"].get<std::string>();
            std::string start = dl["start_time"].get<std::string>();
            std::string end = dl["end_time"].get<std::string>();
            int dl_id = dl["id"].get<int>();

            // Format datetime for Google Calendar (RFC3339)
            // Input: "2026-03-01 09:00" ??"2026-03-01T09:00:00+09:00"
            auto format_dt = [](const std::string& dt) -> std::string {
                std::string result = dt;
                auto space_pos = result.find(' ');
                if (space_pos != std::string::npos) {
                    result[space_pos] = 'T';
                }
                if (result.length() == 16) result += ":00";
                result += "+09:00";
                return result;
            };

            if (start.empty() || end.empty()) continue;

            json gcal_result = gcal.insert_event(title, format_dt(start),
                                                  format_dt(end), dl["type"].get<std::string>());
            if (gcal_result.contains("id")) {
                db.mark_deadline_synced(dl_id, gcal_result["id"].get<std::string>());
                synced_count++;
            }
        }

        return json_response({
            {"success", true},
            {"synced", synced_count},
            {"total_unsynced", (int)unsynced.size()}
        });
    });

    // ----------------------------------------------------------
    // REST API: Profile (from SQLite)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/api/profile")
    ([&]() {
        std::lock_guard<std::mutex> lock(db_mutex);
        return json_response(db.get_profile());
    });

    // PUT /api/profile ??update profile
    CROW_ROUTE(app, "/api/profile").methods("PUT"_method)
    ([&](const crow::request& req) {
        json body;
        try {
            body = json::parse(req.body);
        } catch (...) {
            return json_error(400, "Invalid JSON body");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        bool ok = db.update_profile(body);
        if (ok) {
            return json_response({{"success", true}, {"message", "Profile updated"}});
        }
        return json_error(400, "No valid fields to update");
    });

    // ----------------------------------------------------------
    // WebSocket: Terminal / AI Chat (via Gemini)
    // ----------------------------------------------------------
    CROW_WEBSOCKET_ROUTE(app, "/ws/shell")
        .onopen([&](crow::websocket::connection& conn) {
            CROW_LOG_INFO << "WebSocket connected";
            json welcome = {
                {"sender", "academic-hub"},
                {"time", "now"},
                {"message", "System initialized. Neural interface online.\n"
                            "Connected to Proxmox node: " + cfg.proxmox.node + "\n"
                            "Gemini AI model: " + cfg.gemini.model + "\n"
                            "Awaiting command sequence..."}
            };
            conn.send_text(welcome.dump());
        })
        .onclose([](crow::websocket::connection&, const std::string& reason) {
            CROW_LOG_INFO << "WebSocket closed: " << reason;
        })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool) {
            std::string user_msg = data;
            try {
                auto j = json::parse(data);
                if (j.contains("message")) {
                    user_msg = j["message"].get<std::string>();
                }
            } catch (...) {}

            CROW_LOG_INFO << "Terminal: " << user_msg;

            auto start = std::chrono::high_resolution_clock::now();
            std::string ai_response = gemini.terminal_chat(user_msg);
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            json response = {
                {"sender", "academic-hub"},
                {"time", "now"},
                {"message", ai_response},
                {"processing_time_ms", ms},
                {"tokens", (int)(ai_response.size() / 4)}
            };
            conn.send_text(response.dump());
        });

    // ----------------------------------------------------------
    // Static File Serving (React build)
    // ----------------------------------------------------------
    CROW_ROUTE(app, "/assets/<path>")
    ([&build_dir](const std::string& path) {
        std::string file_path = build_dir + "/assets/" + path;
        if (!fs::exists(file_path)) return crow::response(404);
        auto content = read_file(file_path);
        auto resp = crow::response(200, content);
        resp.add_header("Content-Type", mime_type(file_path));
        return resp;
    });

    // ----------------------------------------------------------
    // SPA Catch-All
    // ----------------------------------------------------------
    CROW_CATCHALL_ROUTE(app)
    ([&build_dir](const crow::request& req) {
        std::string url_path = req.url;
        if (url_path == "/") url_path = "/index.html";

        std::string file_path = build_dir + url_path;
        if (fs::exists(file_path) && fs::is_regular_file(file_path)) {
            auto content = read_file(file_path);
            auto resp = crow::response(200, content);
            resp.add_header("Content-Type", mime_type(file_path));
            return resp;
        }

        std::string index_path = build_dir + "/index.html";
        if (fs::exists(index_path)) {
            auto content = read_file(index_path);
            auto resp = crow::response(200, content);
            resp.add_header("Content-Type", "text/html");
            return resp;
        }

        return crow::response(404, "Build directory not found. Run 'npm run build' first.");
    });

    // ----------------------------------------------------------
    // Start
    // ----------------------------------------------------------
    CROW_LOG_INFO << "============================================";
    CROW_LOG_INFO << " Self-Hosting Academic Hub Server ??Phase 5 (SSU Academic)";
    CROW_LOG_INFO << " Target: Proxmox LXC (Ubuntu 24.04)";
    CROW_LOG_INFO << " Port: " << cfg.server.port;
    CROW_LOG_INFO << " SQLite: " << cfg.server.db_path;
    CROW_LOG_INFO << " Proxmox: " << cfg.proxmox.host;
    CROW_LOG_INFO << " Gemini: " << cfg.gemini.model;
    CROW_LOG_INFO << " Google Calendar: " << (gcal.is_enabled() ? "enabled" : "disabled");
    CROW_LOG_INFO << " GCal Write: " << (gcal.has_write_access() ? "OAuth2 ready" : "not configured");
    CROW_LOG_INFO << " LMS: " << (cfg.lms.username.empty() ? "not configured" : "configured");
    CROW_LOG_INFO << " Notice Crawl Interval: " << cfg.lms.crawl_interval_minutes << " min";
    CROW_LOG_INFO << " Static: ./" << build_dir << "/";
    CROW_LOG_INFO << "============================================";

    // Background thread: periodic notice crawling
    std::atomic<bool> crawler_running{true};
    std::thread crawler_thread([&]() {
        // Wait a bit before first crawl so server can start
        std::this_thread::sleep_for(std::chrono::seconds(10));
        while (crawler_running) {
            try {
                CROW_LOG_INFO << "[Crawler] Running periodic notice crawl...";
                std::lock_guard<std::mutex> lock(db_mutex);
                notice_crawler.crawl_and_process(db, gemini);
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "[Crawler] Error: " << e.what();
            }
            // Sleep for configured interval
            for (int i = 0; i < cfg.lms.crawl_interval_minutes * 60 && crawler_running; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    });
    crawler_thread.detach();

    app.port(cfg.server.port)
       .multithreaded()
       .run();

    crawler_running = false;
    curl_global_cleanup();
    return 0;
}

