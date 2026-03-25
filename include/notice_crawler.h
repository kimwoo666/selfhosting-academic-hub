#pragma once
#include "nlohmann/json.hpp"
#include "database.h"
#include "gemini.h"
#include <curl/curl.h>
#include <string>
#include <vector>
#include <regex>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <array>
#include <memory>
#include <cstdio>
#include <chrono>

using json = nlohmann::json;

struct NoticeItem {
    std::string source;
    std::string title;
    std::string url;
    std::string date;
};

class NoticeCrawler {
    static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::string fetch_text(const std::string& url) {
        std::string response;
        CURL* curl = curl_easy_init();
        if (!curl) return "";

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[NoticeCrawler] Fetch failed for " << url
                      << ": " << curl_easy_strerror(res) << "\n";
            return "";
        }
        return response;
    }

    static void replace_all(std::string& value, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    static std::string decode_html_entities(const std::string& value) {
        std::string result = value;
        replace_all(result, "&amp;", "&");
        replace_all(result, "&lt;", "<");
        replace_all(result, "&gt;", ">");
        replace_all(result, "&quot;", "\"");
        replace_all(result, "&#39;", "'");
        replace_all(result, "&nbsp;", " ");
        return result;
    }

    static std::string strip_tags(const std::string& html) {
        std::string result;
        bool in_tag = false;
        for (char ch : html) {
            if (ch == '<') {
                in_tag = true;
                continue;
            }
            if (ch == '>') {
                in_tag = false;
                continue;
            }
            if (!in_tag) result += ch;
        }
        return result;
    }

    static std::string trim(const std::string& value) {
        auto start = value.find_first_not_of(" \t\r\n");
        auto end = value.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return value.substr(start, end - start + 1);
    }

    static std::string normalize_notice_date(const std::string& value) {
        return value.size() >= 10 ? value.substr(0, 10) : value;
    }

    static std::string parse_notice_title(const json& title_value) {
        if (title_value.is_string()) {
            return trim(decode_html_entities(strip_tags(title_value.get<std::string>())));
        }

        if (title_value.is_object() && title_value.contains("rendered") && title_value["rendered"].is_string()) {
            return trim(decode_html_entities(strip_tags(title_value["rendered"].get<std::string>())));
        }

        return "";
    }

    static bool is_usaint_source(const std::string& source) {
        return source.compare(0, 7, "usaint_") == 0;
    }

    static bool is_custom_source(const std::string& source) {
        return source.compare(0, 7, "custom:") == 0;
    }

    static std::string alert_source_label(const std::string& source) {
        if (source == "aix") return "AIX";
        if (source == "sw") return "SW";
        if (is_usaint_source(source)) return "u-SAINT";
        if (is_custom_source(source)) return source.substr(7);
        return "NOTICE";
    }

    static std::string shell_quote(const std::string& value) {
        std::string escaped = "\"";
        for (char ch : value) {
            if (ch == '"' || ch == '\\') escaped += '\\';
            escaped += ch;
        }
        escaped += '"';
        return escaped;
    }

    json fetch_custom_selector_feed(const std::string& url, const std::string& selector) {
        namespace fs = std::filesystem;

        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        fs::path config_path = fs::temp_directory_path() / ("academic_hub_notice_source_" + std::to_string(timestamp) + ".json");

        {
            std::ofstream config_file(config_path);
            config_file << json({
                {"url", url},
                {"selector", selector},
                {"limit", 20}
            }).dump(2);
        }

        std::string cmd = "python3 scripts/custom_notice_fetch.py --config " + shell_quote(config_path.string());
#ifdef _WIN32
        cmd = "python scripts/custom_notice_fetch.py --config " + shell_quote(config_path.string());
#endif
        cmd += " 2>&1";

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) {
            fs::remove(config_path);
            throw std::runtime_error("Failed to execute custom_notice_fetch.py");
        }

        std::array<char, 256> buffer{};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
            output += buffer.data();
        }

        fs::remove(config_path);

        auto json_start = output.find('{');
        auto json_end = output.rfind('}');
        if (json_start == std::string::npos || json_end == std::string::npos || json_end <= json_start) {
            throw std::runtime_error("Invalid selector crawler output");
        }

        return json::parse(output.substr(json_start, json_end - json_start + 1));
    }

    std::vector<NoticeItem> parse_custom_selector_payload(const json& payload, const std::string& source_name) {
        std::vector<NoticeItem> notices;
        if (!payload.value("success", false) || !payload.contains("notices") || !payload["notices"].is_array()) {
            return notices;
        }

        const std::string source_code = "custom:" + source_name;
        for (const auto& item : payload["notices"]) {
            const std::string title = trim(item.value("title", ""));
            const std::string url = item.value("url", "");
            const std::string date = normalize_notice_date(item.value("date", ""));
            if (title.empty() || url.empty()) continue;
            notices.push_back({source_code, title, url, date});
        }

        std::cout << "[NoticeCrawler] custom source " << source_name << ": parsed " << notices.size() << " notices\n";
        return notices;
    }

    void store_new_notices(Database& db, const std::vector<NoticeItem>& notices, json& result) {
        for (const auto& notice : notices) {
            if (db.add_notice(notice.source, notice.title, notice.url, notice.date)) {
                result["new_notices"] = result["new_notices"].get<int>() + 1;
            }
        }
    }

public:
    std::vector<NoticeItem> parse_aix(const std::string& html) {
        std::vector<NoticeItem> notices;
        std::regex link_re(
            R"(<a\s+[^>]*href\s*=\s*["']([^"']*notice_view\.html\?[^"']*)["'][^>]*>(.*?)</a>)",
            std::regex::icase
        );

        auto begin = std::sregex_iterator(html.begin(), html.end(), link_re);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::smatch match = *it;
            std::string href = match[1].str();
            std::string title = trim(decode_html_entities(strip_tags(match[2].str())));
            if (title.empty()) continue;

            std::string full_url = href;
            if (full_url.find("http") != 0) {
                full_url = "https://aix.ssu.ac.kr/" + href;
            }

            notices.push_back({"aix", title, full_url, ""});
        }

        std::cout << "[NoticeCrawler] AIX: parsed " << notices.size() << " notices\n";
        return notices;
    }

    std::vector<NoticeItem> parse_sw(const std::string& html) {
        std::vector<NoticeItem> notices;
        std::regex link_re(
            R"(<a\s+[^>]*href\s*=\s*["']((?:https?://sw\.ssu\.ac\.kr)?/bbs/board\.php\?bo_table=notice[^"']*)["'][^>]*>(.*?)</a>)",
            std::regex::icase
        );

        auto begin = std::sregex_iterator(html.begin(), html.end(), link_re);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::smatch match = *it;
            std::string href = match[1].str();
            if (href.find("wr_id=") == std::string::npos) continue;
            if (href.find("page=") != std::string::npos && href.find("wr_id=") == std::string::npos) continue;

            std::string title = trim(decode_html_entities(strip_tags(match[2].str())));
            if (title.empty() || title.size() < 3) continue;

            std::string full_url = href;
            if (full_url.find("http") != 0) {
                full_url = "https://sw.ssu.ac.kr" + full_url;
            }

            notices.push_back({"sw", title, full_url, ""});
        }

        std::cout << "[NoticeCrawler] SW: parsed " << notices.size() << " notices\n";
        return notices;
    }

    std::vector<NoticeItem> parse_usaint_jsonp(const std::string& body, const std::string& source) {
        std::vector<NoticeItem> notices;
        auto open = body.find('(');
        auto close = body.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) {
            std::cerr << "[NoticeCrawler] u-SAINT JSONP payload not found for source " << source << "\n";
            return notices;
        }

        try {
            std::string payload = trim(body.substr(open + 1, close - open - 1));
            json parsed = json::parse(payload);
            if (!parsed.contains("Data") || !parsed["Data"].is_array()) {
                return notices;
            }

            for (const auto& item : parsed["Data"]) {
                std::string title = item.contains("title") ? parse_notice_title(item["title"]) : "";
                std::string url = item.value("link", "");
                std::string date = normalize_notice_date(item.value("date", ""));
                if (title.empty() || url.empty()) continue;
                notices.push_back({source, title, url, date});
            }
        } catch (const std::exception& e) {
            std::cerr << "[NoticeCrawler] Failed to parse u-SAINT JSONP for " << source
                      << ": " << e.what() << "\n";
        }

        std::cout << "[NoticeCrawler] " << source << ": parsed " << notices.size() << " notices\n";
        return notices;
    }

    std::vector<NoticeItem> parse_usaint_wp(const std::string& body, const std::string& source) {
        std::vector<NoticeItem> notices;

        try {
            json parsed = json::parse(body);
            if (!parsed.is_array()) {
                return notices;
            }

            for (const auto& item : parsed) {
                std::string title = item.contains("title") ? parse_notice_title(item["title"]) : "";
                std::string url = item.value("link", "");
                std::string date = normalize_notice_date(item.value("date", ""));
                if (title.empty() || url.empty()) continue;
                notices.push_back({source, title, url, date});
            }
        } catch (const std::exception& e) {
            std::cerr << "[NoticeCrawler] Failed to parse u-SAINT WP feed for " << source
                      << ": " << e.what() << "\n";
        }

        std::cout << "[NoticeCrawler] " << source << ": parsed " << notices.size() << " notices\n";
        return notices;
    }

    json crawl_and_process(Database& db, GeminiClient& gemini) {
        json result = {{"new_notices", 0}, {"analyzed", 0}, {"alerts_created", 0}};

        std::string aix_html = fetch_text("https://aix.ssu.ac.kr/notice.html");
        if (!aix_html.empty()) {
            store_new_notices(db, parse_aix(aix_html), result);
        }

        std::string sw_html = fetch_text("https://sw.ssu.ac.kr/bbs/board.php?bo_table=notice");
        if (!sw_html.empty()) {
            store_new_notices(db, parse_sw(sw_html), result);
        }

        struct FeedConfig {
            std::string source;
            std::string url;
            bool is_jsonp;
        };

        const std::vector<FeedConfig> usaint_feeds = {
            {"usaint_academic", "https://scatch.ssu.ac.kr/usaint/notice_json.php", true},
            {"usaint_scholarship", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=6&per_page=10", false},
            {"usaint_exchange", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=7&per_page=10", false},
            {"usaint_foreign", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=8&per_page=10", false},
            {"usaint_hiring", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=9&per_page=10", false},
            {"usaint_event", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=10&per_page=10", false},
            {"usaint_volunteer", "https://scatch.ssu.ac.kr/wp-json/wp/v2/notice?notice-category=11&per_page=10", false}
        };

        for (const auto& feed : usaint_feeds) {
            std::string body = fetch_text(feed.url);
            if (body.empty()) continue;

            auto notices = feed.is_jsonp
                ? parse_usaint_jsonp(body, feed.source)
                : parse_usaint_wp(body, feed.source);
            store_new_notices(db, notices, result);
        }

        auto custom_sources = db.get_notice_sources(true);
        if (custom_sources.is_array()) {
            for (const auto& source : custom_sources) {
                try {
                    const std::string source_name = trim(source.value("name", ""));
                    const std::string source_url = source.value("url", "");
                    const std::string selector = trim(source.value("title_selector", ""));
                    if (source_name.empty() || source_url.empty() || selector.empty()) continue;

                    auto payload = fetch_custom_selector_feed(source_url, selector);
                    auto notices = parse_custom_selector_payload(payload, source_name);
                    store_new_notices(db, notices, result);
                } catch (const std::exception& e) {
                    std::cerr << "[NoticeCrawler] Custom source failed: " << e.what() << "\n";
                }
            }
        }

        auto unprocessed = db.get_unprocessed_notices();
        for (auto& notice : unprocessed) {
            std::string title = notice["title"].get<std::string>();
            std::string source = notice["source"].get<std::string>();
            int notice_id = notice["id"].get<int>();

            try {
                json analysis = gemini.analyze_notice(title, source);
                int score = analysis.value("score", 0);
                std::string summary = analysis.value("summary", "");
                std::string category = analysis.value("category", "Academic");

                db.mark_notice_processed(notice_id, score, summary);
                result["analyzed"] = result["analyzed"].get<int>() + 1;

                if (score >= 80) {
                    std::string severity = score >= 95 ? "critical" : "info";
                    std::string color = score >= 95 ? "red" : (score >= 90 ? "amber" : "primary");
                    std::string source_label = alert_source_label(source);
                    std::string alert_msg = "[" + source_label + "] " + (summary.empty() ? title : summary);

                    db.add_alert(severity, category, title, alert_msg, color);
                    result["alerts_created"] = result["alerts_created"].get<int>() + 1;
                }
            } catch (const std::exception& e) {
                std::cerr << "[NoticeCrawler] Analysis failed for: " << title
                          << " -- " << e.what() << "\n";
                db.mark_notice_processed(notice_id, 0, "Analysis failed");
            }
        }

        std::cout << "[NoticeCrawler] Done: " << result["new_notices"] << " new, "
                  << result["analyzed"] << " analyzed, "
                  << result["alerts_created"] << " alerts\n";
        return result;
    }
};
