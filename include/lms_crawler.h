#pragma once
// ============================================================
// lms_crawler.h ??SSU LMS crawler via C++ libcurl
// Handles SSO login, course extraction, deadline discovery
// ============================================================
#include "nlohmann/json.hpp"
#include "database.h"
#include "config.h"
#include <curl/curl.h>
#include <string>
#include <vector>
#include <regex>
#include <iostream>
#include <set>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

struct LmsCourse {
    std::string lms_id;
    std::string name;
    std::string professor;
    std::string url;
};

struct LmsDeadline {
    std::string course_name;
    std::string type;   // "exam", "lecture", "assignment"
    std::string title;
    std::string start_time;
    std::string end_time;
};

class LmsCrawler {
    LmsConfig cfg_;
    CURL* curl_ = nullptr;
    std::string cookie_file_;
    bool logged_in_ = false;

    static size_t write_cb(char* ptr, size_t sz, size_t nm, std::string* data) {
        data->append(ptr, sz * nm);
        return sz * nm;
    }

    // Trim whitespace
    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        auto end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        return s.substr(start, end - start + 1);
    }

    // Strip HTML tags
    static std::string strip_tags(const std::string& html) {
        std::string result;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; continue; }
            if (!in_tag) result += c;
        }
        return result;
    }

    // Decode HTML entities
    static std::string decode_html(const std::string& s) {
        std::string r = s;
        auto rep = [](std::string& str, const std::string& f, const std::string& t) {
            size_t pos = 0;
            while ((pos = str.find(f, pos)) != std::string::npos) {
                str.replace(pos, f.length(), t);
                pos += t.length();
            }
        };
        rep(r, "&amp;", "&"); rep(r, "&lt;", "<"); rep(r, "&gt;", ">");
        rep(r, "&quot;", "\""); rep(r, "&#39;", "'"); rep(r, "&nbsp;", " ");
        return r;
    }

    // Perform HTTP GET request (reuses curl session)
    std::string http_get(const std::string& url) {
        if (!curl_) {
            std::cerr << "[LMS] GET failed: curl handle is null\n";
            return "";
        }
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            std::cerr << "[LMS] GET failed: " << url << " -> " << curl_easy_strerror(res) << "\n";
            return "";
        }
        return response;
    }

    // Perform HTTP POST (form-encoded)
    std::string http_post(const std::string& url, const std::string& post_data) {
        if (!curl_) {
            std::cerr << "[LMS] POST failed: curl handle is null\n";
            return "";
        }
        std::string response;
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, post_data.c_str());

        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            std::cerr << "[LMS] POST failed: " << url << " -> " << curl_easy_strerror(res) << "\n";
            return "";
        }

        // Reset to GET for subsequent requests
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        return response;
    }

    // URL-encode a string
    std::string url_encode(const std::string& s) {
        if (!curl_) return s;
        char* encoded = curl_easy_escape(curl_, s.c_str(), (int)s.length());
        if (!encoded) return s;
        std::string result(encoded);
        curl_free(encoded);
        return result;
    }

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static std::string attr_value(const std::string& tag, const std::string& attr) {
        std::regex re(attr + R"(\s*=\s*["']([^"']*)["'])", std::regex::icase);
        std::smatch m;
        if (std::regex_search(tag, m, re)) return m[1].str();
        return "";
    }

    static std::string resolve_url(const std::string& base, const std::string& raw) {
        if (raw.empty()) return "";
        if (raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0) return raw;
        if (raw.rfind("//", 0) == 0) return "https:" + raw;

        std::regex host_re(R"(^(https?://[^/]+))", std::regex::icase);
        std::smatch host_m;
        std::string host = "";
        if (std::regex_search(base, host_m, host_re)) host = host_m[1].str();
        if (host.empty()) return raw;

        if (raw[0] == '/') return host + raw;

        auto slash = base.find_last_of('/');
        if (slash == std::string::npos) return host + "/" + raw;
        return base.substr(0, slash + 1) + raw;
    }

    std::string extract_hidden_fields(const std::string& html) {
        std::string fields;
        std::set<std::string> seen;
        std::regex input_re(R"(<input\b[^>]*>)", std::regex::icase);
        auto begin = std::sregex_iterator(html.begin(), html.end(), input_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string tag = it->str();
            std::string type = to_lower(attr_value(tag, "type"));
            std::string name = attr_value(tag, "name");
            std::string value = attr_value(tag, "value");
            if (name.empty()) continue;
            if (!type.empty() && type != "hidden") continue;
            if (seen.insert(name).second) {
                if (!fields.empty()) fields += "&";
                fields += url_encode(name) + "=" + url_encode(value);
            }
        }
        return fields;
    }

    static std::string extract_form_scope(const std::string& html, bool prefer_password_form = false) {
        std::regex form_re(R"(<form\b[^>]*>[\s\S]*?</form>)", std::regex::icase);
        auto begin = std::sregex_iterator(html.begin(), html.end(), form_re);
        auto end = std::sregex_iterator();
        std::string fallback;

        for (auto it = begin; it != end; ++it) {
            std::string form_html = it->str();
            if (fallback.empty()) fallback = form_html;
            if (!prefer_password_form) return form_html;
            if (!detect_input_field(form_html, true).empty()) return form_html;
        }

        return fallback.empty() ? html : fallback;
    }

    static std::string extract_form_action(const std::string& html) {
        std::string scope = extract_form_scope(html, false);
        std::regex form_re(R"(<form\b[^>]*>)", std::regex::icase);
        std::smatch m;
        if (!std::regex_search(scope, m, form_re)) return "";
        return attr_value(m[0].str(), "action");
    }

    static std::string detect_input_field(const std::string& html, bool want_password) {
        std::regex input_re(R"(<input\b[^>]*>)", std::regex::icase);
        auto begin = std::sregex_iterator(html.begin(), html.end(), input_re);
        auto end = std::sregex_iterator();
        std::string fallback;
        for (auto it = begin; it != end; ++it) {
            std::string tag = it->str();
            std::string type = to_lower(attr_value(tag, "type"));
            std::string name = attr_value(tag, "name");
            std::string id = to_lower(attr_value(tag, "id"));
            std::string low_name = to_lower(name);
            if (name.empty()) continue;

            if (want_password) {
                if (type == "password" || low_name.find("pwd") != std::string::npos ||
                    low_name.find("pass") != std::string::npos || id.find("pwd") != std::string::npos) {
                    return name;
                }
            } else {
                if (type == "text" || type == "email" || type.empty()) {
                    if (low_name.find("user") != std::string::npos || low_name.find("id") != std::string::npos ||
                        id.find("user") != std::string::npos || id.find("id") != std::string::npos) {
                        return name;
                    }
                    if (fallback.empty()) fallback = name;
                }
            }
        }
        return want_password ? "" : fallback;
    }

    static std::string extract_js_redirect(const std::string& html) {
        std::vector<std::regex> patterns = {
            std::regex(R"((?:window\.|document\.|top\.)?location\.(?:href|replace)\s*[=(]\s*['"]([^'"]+)['"])", std::regex::icase),
            std::regex(R"((?:window\.|document\.|top\.)?location\s*=\s*['"]([^'"]+)['"])", std::regex::icase)
        };

        for (auto& re : patterns) {
            std::smatch m;
            if (std::regex_search(html, m, re)) return m[1].str();
        }
        return "";
    }

    static std::string extract_meta_refresh_url(const std::string& html) {
        std::regex re(R"(<meta[^>]*http-equiv\s*=\s*["']?refresh["']?[^>]*content\s*=\s*["'][^"']*url=([^"'>]+))", std::regex::icase);
        std::smatch m;
        if (std::regex_search(html, m, re)) return trim(m[1].str());
        return "";
    }

    static bool is_sso_login_page(const std::string& html) {
        std::string low = to_lower(html);
        return low.find("smartid.ssu.ac.kr") != std::string::npos ||
               low.find("symtra_sso") != std::string::npos ||
               low.find("userid") != std::string::npos && low.find("pwd") != std::string::npos;
    }

    static bool contains_credential_error(const std::string& html) {
        std::string low = to_lower(html);
        return low.find("password") != std::string::npos && low.find("error") != std::string::npos ||
               low.find("alert(") != std::string::npos && (low.find("鍮꾨?踰덊샇") != std::string::npos || low.find("password") != std::string::npos);
    }

    static bool contains_credential_error_robust(const std::string& html) {
        std::string low = to_lower(html);
        if (low.find("password") != std::string::npos &&
            (low.find("error") != std::string::npos ||
             low.find("invalid") != std::string::npos ||
             low.find("incorrect") != std::string::npos)) {
            return true;
        }
        if (low.find("userid") != std::string::npos &&
            (low.find("error") != std::string::npos ||
             low.find("invalid") != std::string::npos)) {
            return true;
        }
        if (low.find("alert(") != std::string::npos &&
            (low.find("password") != std::string::npos || low.find("userid") != std::string::npos)) {
            return true;
        }
        if (html.find("鍮꾨?踰덊샇") != std::string::npos && html.find("?ㅻ쪟") != std::string::npos) return true;
        if (html.find("?숇쾲") != std::string::npos && html.find("?ㅻ쪟") != std::string::npos) return true;
        return false;
    }

public:
    explicit LmsCrawler(const LmsConfig& cfg) : cfg_(cfg) {
        curl_ = curl_easy_init();
        if (!curl_) {
            std::cerr << "[LMS] Failed to initialize libcurl\n";
            return;
        }

        // Configure persistent session
        cookie_file_ = "/tmp/jarvis_lms_cookies.txt";
        curl_easy_setopt(curl_, CURLOPT_COOKIEJAR, cookie_file_.c_str());
        curl_easy_setopt(curl_, CURLOPT_COOKIEFILE, cookie_file_.c_str());
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl_, CURLOPT_USERAGENT,
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING, "");
    }

    ~LmsCrawler() {
        if (curl_) curl_easy_cleanup(curl_);
    }

    // ?? Set credentials at runtime (for login endpoint) ??
    void set_credentials(const std::string& username, const std::string& password) {
        cfg_.username = username;
        cfg_.password = password;
        logged_in_ = false;  // Force re-login with new credentials
    }

    bool is_logged_in() const { return logged_in_; }
    std::string current_username() const { return cfg_.username; }

    // Lightweight session probe without re-login.
    json probe_session() {
        json out = {
            {"alive", false},
            {"reason", ""},
            {"mypage_bytes", 0},
            {"dashboard_bytes", 0},
            {"has_logout", false},
            {"has_course", false},
            {"is_login_page", true}
        };
        if (!curl_) {
            out["reason"] = "curl_handle_null";
            return out;
        }

        std::string mypage = http_get("https://lms.ssu.ac.kr/mypage");
        std::string dashboard = http_get("https://lms.ssu.ac.kr/dashboard");
        out["mypage_bytes"] = (int)mypage.size();
        out["dashboard_bytes"] = (int)dashboard.size();

        std::string low_my = to_lower(mypage);
        std::string low_dash = to_lower(dashboard);
        bool is_login_page = is_sso_login_page(mypage) || is_sso_login_page(dashboard) ||
                             low_my.find("login/index.php") != std::string::npos ||
                             low_dash.find("login/index.php") != std::string::npos;
        bool has_logout = low_my.find("logout") != std::string::npos ||
                          low_dash.find("logout") != std::string::npos;
        bool has_course = low_my.find("course/view.php") != std::string::npos ||
                          low_my.find("my courses") != std::string::npos ||
                          low_dash.find("dashboard") != std::string::npos ||
                          low_dash.find("course") != std::string::npos;
        bool alive = (has_logout || has_course) && !is_login_page;

        out["has_logout"] = has_logout;
        out["has_course"] = has_course;
        out["is_login_page"] = is_login_page;
        out["alive"] = alive;
        if (!alive) out["reason"] = "login_markers_missing_or_login_page_detected";
        return out;
    }

    // ?? SSO Login Flow ??
    // Step 1: GET lms.ssu.ac.kr ??triggers SSO redirect chain
    // Step 2: POST credentials to SSO form
    // Step 3: Parse SSO response for JS/HTML redirect and follow it
    // Step 4: Verify session is established
    bool sso_login() {
        if (!curl_) {
            std::cerr << "[LMS] curl handle not initialized\n";
            return false;
        }
        if (cfg_.username.empty() || cfg_.password.empty()) {
            std::cerr << "[LMS] Username or password not configured\n";
            return false;
        }

        std::cout << "[LMS] Starting SSO login for user: " << cfg_.username << "\n";
        curl_easy_setopt(curl_, CURLOPT_COOKIELIST, "ALL");

        const std::string lms_url = "https://lms.ssu.ac.kr/";
        const std::string sso_entry = "https://smartid.ssu.ac.kr/Symtra_sso/smln.asp?apiReturnUrl=https%3A%2F%2Flms.ssu.ac.kr%2Fxn-sso%2Fgw-cb.php";

        std::string lms_page = http_get(lms_url);
        std::cout << "[LMS] Step 1: LMS homepage fetched (" << lms_page.size() << " bytes)\n";
        if (lms_page.empty()) {
            std::cerr << "[LMS] Failed to access LMS homepage\n";
            return false;
        }

        std::string sso_page = http_get(sso_entry);
        std::cout << "[LMS] Step 1b: SSO login page fetched (" << sso_page.size() << " bytes)\n";
        if (sso_page.empty()) {
            std::cerr << "[LMS] Failed to access SSO login page\n";
            return false;
        }

        std::string login_form_html = extract_form_scope(sso_page, true);
        std::string form_action = extract_form_action(login_form_html);
        std::string login_post_url = resolve_url(sso_entry, form_action.empty() ? sso_entry : form_action);
        std::string user_field = detect_input_field(login_form_html, false);
        std::string pass_field = detect_input_field(login_form_html, true);
        if (user_field.empty()) user_field = "userid";
        if (pass_field.empty()) pass_field = "pwd";

        std::string post_data = extract_hidden_fields(login_form_html);
        if (!post_data.empty()) post_data += "&";
        post_data += url_encode(user_field) + "=" + url_encode(cfg_.username);
        post_data += "&" + url_encode(pass_field) + "=" + url_encode(cfg_.password);
        if (post_data.find("in_tp_bit=") == std::string::npos) post_data += "&in_tp_bit=0";
        if (post_data.find("rqst_caus_cd=") == std::string::npos) post_data += "&rqst_caus_cd=03";

        std::cout << "[LMS] Step 2: POSTing to SSO form: " << login_post_url
                  << " (user_field=" << user_field << ", pass_field=" << pass_field << ")\n";
        std::string current_html = http_post(login_post_url, post_data);
        std::string current_url = login_post_url;
        std::cout << "[LMS] Step 2: SSO response (" << current_html.size() << " bytes)\n";
        if (current_html.empty()) {
            std::cerr << "[LMS] SSO login POST failed (empty response)\n";
            return false;
        }

        std::cout << "[LMS] Step 2: SSO response preview:\n"
                  << current_html.substr(0, std::min((size_t)500, current_html.size())) << "\n";

        if (contains_credential_error_robust(current_html)) {
            std::cerr << "[LMS] SSO reports credential error\n";
            std::regex alert_re(R"(alert\s*\(\s*["']([^"']+)["'])", std::regex::icase);
            std::smatch alert_match;
            if (std::regex_search(current_html, alert_match, alert_re)) {
                std::cerr << "[LMS] SSO alert: " << alert_match[1].str() << "\n";
            }
            return false;
        }

        bool followed_redirect = false;
        const int max_hops = 8;
        for (int i = 0; i < max_hops; ++i) {
            std::string next_js = extract_js_redirect(current_html);
            if (!next_js.empty()) {
                std::string next_url = resolve_url(current_url, next_js);
                std::cout << "[LMS] Step 3." << i + 1 << ": JS redirect -> " << next_url << "\n";
                current_html = http_get(next_url);
                current_url = next_url;
                followed_redirect = true;
                continue;
            }

            std::string next_meta = extract_meta_refresh_url(current_html);
            if (!next_meta.empty()) {
                std::string next_url = resolve_url(current_url, next_meta);
                std::cout << "[LMS] Step 3." << i + 1 << ": Meta refresh -> " << next_url << "\n";
                current_html = http_get(next_url);
                current_url = next_url;
                followed_redirect = true;
                continue;
            }

            std::string form_scope = extract_form_scope(current_html, false);
            std::string next_action = extract_form_action(form_scope);
            std::string next_hidden = extract_hidden_fields(form_scope);
            if (!next_action.empty() && !next_hidden.empty()) {
                std::string next_url = resolve_url(current_url, next_action);
                std::string next_url_low = to_lower(next_url);
                if (next_url_low.find("logout") != std::string::npos ||
                    next_url_low.find("logoff") != std::string::npos ||
                    next_url_low.find("signout") != std::string::npos) {
                    std::cout << "[LMS] Step 3." << i + 1 << ": Skip logout form -> " << next_url << "\n";
                    break;
                }
                std::cout << "[LMS] Step 3." << i + 1 << ": Form submit -> " << next_url << "\n";
                current_html = http_post(next_url, next_hidden);
                current_url = next_url;
                followed_redirect = true;
                continue;
            }

            break;
        }

        if (!followed_redirect) {
            std::cout << "[LMS] Step 3: No redirect/form chain found, forcing LMS callback\n";
        }
        std::string cb_resp = http_get("https://lms.ssu.ac.kr/xn-sso/gw-cb.php");
        std::cout << "[LMS] Step 3b: Callback response (" << cb_resp.size() << " bytes)\n";

        std::string mypage = http_get("https://lms.ssu.ac.kr/mypage");
        std::string dashboard = http_get("https://lms.ssu.ac.kr/dashboard");
        std::cout << "[LMS] Step 4: mypage(" << mypage.size() << " bytes), dashboard(" << dashboard.size() << " bytes)\n";

        std::string low_my = to_lower(mypage);
        std::string low_dash = to_lower(dashboard);
        bool is_login_page = is_sso_login_page(mypage) || is_sso_login_page(dashboard) ||
                             low_my.find("login/index.php") != std::string::npos ||
                             low_dash.find("login/index.php") != std::string::npos;
        bool has_logout = low_my.find("logout") != std::string::npos ||
                          low_dash.find("logout") != std::string::npos;
        bool has_course = low_my.find("course/view.php") != std::string::npos ||
                          low_my.find("my courses") != std::string::npos ||
                          low_dash.find("dashboard") != std::string::npos ||
                          low_dash.find("course") != std::string::npos;

        if ((has_logout || has_course) && !is_login_page) {
            std::cout << "[LMS] SSO login successful\n";
            logged_in_ = true;
            return true;
        }

        std::cerr << "[LMS] SSO login verification failed.\n";
        std::cerr << "[LMS] has_logout=" << has_logout
                  << " has_course=" << has_course
                  << " is_login_page=" << is_login_page << "\n";
        return false;
    }

    // ?? Extract enrolled courses from /mypage ??
    std::vector<LmsCourse> parse_courses(const std::string& html) {
        std::vector<LmsCourse> courses;
        if (html.empty()) return courses;

        std::set<std::string> seen_ids;
        std::regex id_re(R"((?:\?|&)id=(\d+))", std::regex::icase);

        auto add_course = [&](const std::string& href_raw,
                              const std::string& text_raw,
                              const std::string& title_raw) {
            std::string href = trim(decode_html(href_raw));
            if (href.empty()) return;
            if (href.find("/course/view.php") == std::string::npos) return;

            std::smatch id_m;
            if (!std::regex_search(href, id_m, id_re)) return;
            std::string id = id_m[1].str();
            if (!seen_ids.insert(id).second) return;

            std::string name = trim(decode_html(strip_tags(text_raw)));
            if (name.empty()) name = trim(decode_html(title_raw));
            if (name.empty()) name = "Course " + id;

            std::string full_url = href;
            if (full_url.rfind("http://", 0) != 0 && full_url.rfind("https://", 0) != 0) {
                if (!full_url.empty() && full_url[0] == '/') full_url = "https://lms.ssu.ac.kr" + full_url;
                else full_url = "https://lms.ssu.ac.kr/" + full_url;
            }
            auto hash_pos = full_url.find('#');
            if (hash_pos != std::string::npos) full_url = full_url.substr(0, hash_pos);

            courses.push_back({id, name, "", full_url});
        };

        // Primary: parse all anchors and filter course URLs.
        std::regex anchor_re(R"(<a\b([^>]*)>([\s\S]*?)</a>)", std::regex::icase);
        auto begin = std::sregex_iterator(html.begin(), html.end(), anchor_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string attrs = (*it)[1].str();
            std::string inner = (*it)[2].str();
            add_course(attr_value(attrs, "href"), inner, attr_value(attrs, "title"));
        }

        // Fallback: detect raw course URLs that might not be in simple anchor text blocks.
        if (courses.empty()) {
            std::regex url_re(R"((?:https?://lms\.ssu\.ac\.kr)?/course/view\.php\?[^"'\s<>]*id=\d+[^"'\s<>]*)", std::regex::icase);
            auto ub = std::sregex_iterator(html.begin(), html.end(), url_re);
            auto ue = std::sregex_iterator();
            for (auto it = ub; it != ue; ++it) add_course(it->str(), "", "");
        }

        std::cout << "[LMS] Found " << courses.size() << " courses\n";
        return courses;
    }

    // ?? Extract deadlines from a course page ??
    // Looks for: assignment due dates, quiz dates, lecture periods
    std::vector<LmsDeadline> parse_deadlines(const std::string& course_html,
                                              const std::string& course_name) {
        std::vector<LmsDeadline> deadlines;

        // Pattern 1: Date ranges like "2026-03-01 ~ 2026-06-30" or "2026.03.01 ~ 2026.06.30"
        std::regex date_range_re(
            R"((\d{4}[-./]\d{2}[-./]\d{2}\s+\d{2}:\d{2})\s*~\s*(\d{4}[-./]\d{2}[-./]\d{2}\s+\d{2}:\d{2}))"
        );

        // Pattern 2: Assignment/quiz sections with titles and dates
        std::regex activity_re(
            R"(<span\s+class\s*=\s*["']instancename["'][^>]*>(.*?)</span>)",
            std::regex::icase
        );

        // Extract activity names
        std::vector<std::string> activities;
        {
            auto begin = std::sregex_iterator(course_html.begin(), course_html.end(), activity_re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                activities.push_back(trim(decode_html(strip_tags((*it)[1].str()))));
            }
        }

        // Look for date ranges associated with activities
        {
            auto begin = std::sregex_iterator(course_html.begin(), course_html.end(), date_range_re);
            auto end = std::sregex_iterator();
            int idx = 0;
            for (auto it = begin; it != end; ++it) {
                LmsDeadline dl;
                dl.course_name = course_name;
                dl.start_time = (*it)[1].str();
                dl.end_time = (*it)[2].str();

                // Normalize date separators to dashes
                for (auto& c : dl.start_time) if (c == '.' || c == '/') c = '-';
                for (auto& c : dl.end_time) if (c == '.' || c == '/') c = '-';

                // Try to associate with an activity name
                if (idx < (int)activities.size()) {
                    dl.title = activities[idx];
                } else {
                    dl.title = "Activity " + std::to_string(idx + 1);
                }

                // Guess type from context
                std::string lower_title = dl.title;
                for (auto& c : lower_title) c = std::tolower(c);

                if (lower_title.find("?쒗뿕") != std::string::npos ||
                    lower_title.find("exam") != std::string::npos ||
                    lower_title.find("quiz") != std::string::npos ||
                    lower_title.find("test") != std::string::npos) {
                    dl.type = "exam";
                } else if (lower_title.find("怨쇱젣") != std::string::npos ||
                           lower_title.find("assign") != std::string::npos ||
                           lower_title.find("?쒖텧") != std::string::npos ||
                           lower_title.find("report") != std::string::npos) {
                    dl.type = "assignment";
                } else {
                    dl.type = "lecture";
                }

                deadlines.push_back(dl);
                idx++;
            }
        }

        return deadlines;
    }

    // ?? Full Sync: Login ??Courses ??Deadlines ??DB ??
    json sync(Database& db) {
        json result = {
            {"success", false},
            {"courses_found", 0},
            {"deadlines_found", 0},
            {"message", ""}
        };

        if (!logged_in_ && !sso_login()) {
            result["message"] = "SSO login failed. Check credentials in config.json.";
            return result;
        }

        // Get mypage to extract courses
        std::string mypage_html = http_get("https://lms.ssu.ac.kr/mypage");
        if (mypage_html.empty()) {
            result["message"] = "Failed to fetch LMS mypage";
            return result;
        }

        auto courses = parse_courses(mypage_html);
        if (courses.empty()) {
            std::string courses_html = http_get("https://lms.ssu.ac.kr/my/courses.php");
            std::cout << "[LMS] Fallback /my/courses.php fetched (" << courses_html.size() << " bytes)\n";
            if (!courses_html.empty()) courses = parse_courses(courses_html);
        }
        if (courses.empty()) {
            std::string dashboard_html = http_get("https://lms.ssu.ac.kr/dashboard");
            std::cout << "[LMS] Fallback /dashboard fetched (" << dashboard_html.size() << " bytes)\n";
            if (!dashboard_html.empty()) courses = parse_courses(dashboard_html);
        }
        result["courses_found"] = (int)courses.size();

        int total_deadlines = 0;
        for (auto& course : courses) {
            // Save course to DB
            int course_id = db.upsert_lms_course(course.lms_id, course.name, course.professor);

            // Fetch course page for deadlines
            std::string course_url = course.url;
            if (course_url.find("http") != 0) {
                course_url = "https://lms.ssu.ac.kr" + course_url;
            }

            std::string course_html = http_get(course_url);
            if (course_html.empty()) continue;

            auto deadlines = parse_deadlines(course_html, course.name);
            for (auto& dl : deadlines) {
                db.upsert_lms_deadline(course_id, dl.type, dl.title, dl.start_time, dl.end_time);
                total_deadlines++;
            }
        }

        result["success"] = true;
        result["deadlines_found"] = total_deadlines;
        result["message"] = "Synced " + std::to_string(courses.size()) + " courses, "
                          + std::to_string(total_deadlines) + " deadlines";

        std::cout << "[LMS] " << result["message"].get<std::string>() << "\n";
        return result;
    }
};

