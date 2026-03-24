#pragma once
// ============================================================
// google_calendar.h — Google Calendar API client via libcurl
// Supports read (API key) and write (OAuth2) operations
// ============================================================
#include "nlohmann/json.hpp"
#include <curl/curl.h>
#include <string>
#include <iostream>

using json = nlohmann::json;

struct GoogleCalendarConfig {
    std::string api_key;
    std::string calendar_id;  // e.g. "primary" or full calendar ID
    bool enabled = false;
    // OAuth2 for write access (inserting events)
    std::string client_id;
    std::string client_secret;
    std::string refresh_token;
};

class GoogleCalendarClient {
    GoogleCalendarConfig cfg_;
    std::string access_token_;  // Cached OAuth2 access token

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    static std::string url_encode(CURL* curl, const std::string& s) {
        char* out = curl_easy_escape(curl, s.c_str(), (int)s.length());
        std::string result(out);
        curl_free(out);
        return result;
    }

    json api_get(const std::string& url) {
        std::string response_str;
        CURL* curl = curl_easy_init();
        if (!curl) return {{"error", "curl init failed"}};

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[GoogleCal] GET failed: " << curl_easy_strerror(res) << "\n";
            return {{"error", curl_easy_strerror(res)}};
        }

        try {
            return json::parse(response_str);
        } catch (...) {
            return {{"error", "JSON parse failed"}, {"raw", response_str}};
        }
    }

    // POST with OAuth2 Bearer token + JSON body
    json api_post(const std::string& url, const json& body) {
        std::string response_str;
        CURL* curl = curl_easy_init();
        if (!curl) return {{"error", "curl init failed"}};

        std::string body_str = body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token_).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[GoogleCal] POST failed: " << curl_easy_strerror(res) << "\n";
            return {{"error", curl_easy_strerror(res)}};
        }

        try {
            return json::parse(response_str);
        } catch (...) {
            return {{"error", "JSON parse failed"}, {"raw", response_str}};
        }
    }

public:
    explicit GoogleCalendarClient(const GoogleCalendarConfig& cfg) : cfg_(cfg) {}

    bool is_enabled() const { return cfg_.enabled && !cfg_.api_key.empty() && !cfg_.calendar_id.empty(); }

    // Check if OAuth2 write access is configured
    bool has_write_access() const {
        return !cfg_.client_id.empty() && !cfg_.client_secret.empty() && !cfg_.refresh_token.empty();
    }

    // ── OAuth2 token management ──

    bool refresh_access_token() {
        if (!has_write_access()) {
            std::cerr << "[GoogleCal] OAuth2 credentials not configured\n";
            return false;
        }

        CURL* curl = curl_easy_init();
        if (!curl) return false;

        std::string post_data = "client_id=" + cfg_.client_id
                              + "&client_secret=" + cfg_.client_secret
                              + "&refresh_token=" + cfg_.refresh_token
                              + "&grant_type=refresh_token";

        std::string response_str;
        curl_easy_setopt(curl, CURLOPT_URL, "https://oauth2.googleapis.com/token");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[GoogleCal] Token refresh failed: " << curl_easy_strerror(res) << "\n";
            return false;
        }

        try {
            auto resp = json::parse(response_str);
            if (resp.contains("access_token")) {
                access_token_ = resp["access_token"].get<std::string>();
                std::cout << "[GoogleCal] Access token refreshed\n";
                return true;
            }
            std::cerr << "[GoogleCal] Token error: " << resp.dump() << "\n";
            return false;
        } catch (...) {
            std::cerr << "[GoogleCal] Failed to parse token response\n";
            return false;
        }
    }

    // ── Write: Insert event ──

    json insert_event(const std::string& title, const std::string& start_time,
                      const std::string& end_time, const std::string& description = "") {
        if (!has_write_access()) {
            return {{"error", "OAuth2 not configured"}};
        }

        if (access_token_.empty() && !refresh_access_token()) {
            return {{"error", "Failed to get access token"}};
        }

        CURL* curl = curl_easy_init();
        if (!curl) return {{"error", "curl init failed"}};
        std::string cal_id = url_encode(curl, cfg_.calendar_id);
        curl_easy_cleanup(curl);

        std::string url = "https://www.googleapis.com/calendar/v3/calendars/"
                        + cal_id + "/events";

        json event_body = {
            {"summary", title},
            {"description", description},
            {"start", {{"dateTime", start_time}, {"timeZone", "Asia/Seoul"}}},
            {"end", {{"dateTime", end_time}, {"timeZone", "Asia/Seoul"}}}
        };

        auto result = api_post(url, event_body);

        // If 401 unauthorized, refresh token and retry once
        if (result.contains("error") && result["error"].is_object() &&
            result["error"].value("code", 0) == 401) {
            std::cout << "[GoogleCal] Token expired, refreshing...\n";
            if (refresh_access_token()) {
                result = api_post(url, event_body);
            }
        }

        if (result.contains("id")) {
            std::cout << "[GoogleCal] Event created: " << result["id"] << "\n";
        }

        return result;
    }

    // ── Read: Fetch events ──

    json get_events(const std::string& time_min, const std::string& time_max) {
        if (!is_enabled()) return json::array();

        CURL* curl = curl_easy_init();
        if (!curl) return json::array();

        std::string cal_id = url_encode(curl, cfg_.calendar_id);
        std::string t_min = url_encode(curl, time_min);
        std::string t_max = url_encode(curl, time_max);

        std::string url = "https://www.googleapis.com/calendar/v3/calendars/" + cal_id
                        + "/events?key=" + cfg_.api_key
                        + "&timeMin=" + t_min + "&timeMax=" + t_max
                        + "&singleEvents=true&orderBy=startTime&maxResults=100";

        curl_easy_cleanup(curl);

        auto resp = api_get(url);

        json result = json::array();
        if (resp.contains("items") && resp["items"].is_array()) {
            for (auto& item : resp["items"]) {
                std::string start_str, end_str;
                bool all_day = false;

                if (item.contains("start")) {
                    if (item["start"].contains("dateTime"))
                        start_str = item["start"]["dateTime"].get<std::string>();
                    else if (item["start"].contains("date")) {
                        start_str = item["start"]["date"].get<std::string>();
                        all_day = true;
                    }
                }
                if (item.contains("end")) {
                    if (item["end"].contains("dateTime"))
                        end_str = item["end"]["dateTime"].get<std::string>();
                    else if (item["end"].contains("date"))
                        end_str = item["end"]["date"].get<std::string>();
                }

                std::string date = start_str.substr(0, 10);
                std::string time_display = all_day ? "All Day" : "";
                if (!all_day && start_str.length() > 16) {
                    std::string hh = start_str.substr(11, 2);
                    std::string mm = start_str.substr(14, 2);
                    int hour = std::stoi(hh);
                    std::string ampm = hour >= 12 ? "PM" : "AM";
                    if (hour > 12) hour -= 12;
                    if (hour == 0) hour = 12;
                    time_display = std::to_string(hour) + ":" + mm + " " + ampm;
                }

                result.push_back({
                    {"id", "gcal_" + item.value("id", "")},
                    {"date", date},
                    {"time", time_display},
                    {"title", item.value("summary", "Untitled")},
                    {"location", item.value("location", "")},
                    {"description", item.value("description", "")},
                    {"type", "Google"},
                    {"duration", ""},
                    {"done", false},
                    {"source", "google"},
                    {"google_link", item.value("htmlLink", "")}
                });
            }
        }

        if (resp.contains("error")) {
            std::cerr << "[GoogleCal] API error: " << resp["error"].dump() << "\n";
        }

        return result;
    }

    json get_month_events(int year, int month) {
        char time_min[32], time_max[32];
        snprintf(time_min, sizeof(time_min), "%04d-%02d-01T00:00:00Z", year, month);

        int next_month = month + 1;
        int next_year = year;
        if (next_month > 12) { next_month = 1; next_year++; }
        snprintf(time_max, sizeof(time_max), "%04d-%02d-01T00:00:00Z", next_year, next_month);

        return get_events(time_min, time_max);
    }
};
