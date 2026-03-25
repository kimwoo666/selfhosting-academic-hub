#pragma once
// ============================================================
// gemini.h -- Google Gemini API client via libcurl
// ============================================================
#include "nlohmann/json.hpp"
#include "config.h"
#include <curl/curl.h>
#include <string>
#include <iostream>

using json = nlohmann::json;

class GeminiClient {
    std::string api_key_;
    std::string model_;

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

public:
    explicit GeminiClient(const GeminiConfig& cfg)
        : api_key_(cfg.api_key), model_(cfg.model) {}

    std::string chat(const std::string& system_prompt, const std::string& user_message) {
        if (api_key_.empty()) {
            return "[Gemini API key not configured]";
        }

        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/"
                        + model_ + ":generateContent?key=" + api_key_;

        json body = {
            {"system_instruction", {
                {"parts", json::array({{{"text", system_prompt}}})}
            }},
            {"contents", json::array({
                {{"role", "user"}, {"parts", json::array({{{"text", user_message}}})}}
            })},
            {"generationConfig", {
                {"temperature", 0.7},
                {"maxOutputTokens", 1024}
            }}
        };

        std::string request_body = body.dump();
        std::string response_str;

        CURL* curl = curl_easy_init();
        if (!curl) return "[curl init failed]";

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return std::string("[Gemini API error: ") + curl_easy_strerror(res) + "]";
        }

        try {
            json resp = json::parse(response_str);
            if (resp.contains("candidates") && !resp["candidates"].empty()) {
                auto& parts = resp["candidates"][0]["content"]["parts"];
                if (!parts.empty()) {
                    return parts[0]["text"].get<std::string>();
                }
            }
            if (resp.contains("error")) {
                return "[Gemini error: " + resp["error"]["message"].get<std::string>() + "]";
            }
            return "[Unexpected Gemini response]";
        } catch (const std::exception& e) {
            return std::string("[JSON parse error: ") + e.what() + "]";
        }
    }

    std::string terminal_chat(const std::string& user_message) {
        return chat(
            "You are the Self-Hosting Academic Hub assistant running inside a Proxmox LXC container. "
            "You manage infrastructure, monitor system resources, and help with academic tasks. "
            "IMPORTANT RULES:\n"
            "1. ALWAYS respond in English, regardless of the input language.\n"
            "2. Use markdown formatting: **bold** for emphasis, `code` for technical terms, "
            "```blocks``` for code/commands, - for bullet lists.\n"
            "3. Keep responses concise, structured, and actionable.\n"
            "4. Use headers (## Section) to organize longer responses.\n"
            "5. When asked about system status, provide realistic Proxmox/Linux context.\n"
            "6. Sign off responses with a brief status line like: `[Academic Hub :: Online]`",
            user_message
        );
    }

    std::string generate_insight(const json& system_data) {
        std::string prompt = "Based on this system data, provide exactly 3 brief infrastructure insights "
                           "(one success, one info, one warning). Return as JSON array with objects "
                           "containing 'level' (success/info/warning) and 'message' fields. "
                           "System data: " + system_data.dump();
        return chat(
            "You are Academic Hub infrastructure analysis AI. Return ONLY valid JSON, no markdown fences.",
            prompt
        );
    }

    json analyze_notice(const std::string& title, const std::string& source) {
        std::string source_name = "Soongsil notice board";
        if (source == "aix") source_name = "AI Convergence notice board";
        else if (source == "sw") source_name = "School of Software notice board";
        else if (source == "usaint_academic") source_name = "u-SAINT academic notice board";
        else if (source == "usaint_scholarship") source_name = "u-SAINT scholarship notice board";
        else if (source == "usaint_exchange") source_name = "u-SAINT international exchange notice board";
        else if (source == "usaint_foreign") source_name = "u-SAINT foreign student notice board";
        else if (source == "usaint_hiring") source_name = "u-SAINT hiring notice board";
        else if (source == "usaint_event") source_name = "u-SAINT extracurricular notice board";
        else if (source == "usaint_volunteer") source_name = "u-SAINT volunteer notice board";
        else if (source.compare(0, 7, "usaint_") == 0) source_name = "u-SAINT campus notice board";

        std::string prompt = "Analyze this university notice for relevance to a CS/AI student.\n"
                            "Notice title: \"" + title + "\"\n"
                            "Source: " + source_name + " (Soongsil University)\n\n"
                            "Rate relevance 0-100 based on these interests: "
                            "AI, machine learning, C++, backend development, hackathons, "
                            "competitions, scholarships, internships, research opportunities, "
                            "course registration, exam schedules.\n\n"
                            "Return ONLY valid JSON (no markdown): "
                            "{\"score\": <0-100>, \"summary\": \"<brief Korean summary>\", "
                            "\"category\": \"<Academic|Scholarship|Competition|Research|General>\"}";

        std::string response = chat(
            "You are a notice relevance analyzer. Return ONLY valid JSON, no markdown fences or extra text.",
            prompt
        );

        try {
            std::string cleaned = response;
            if (cleaned.find("```json") != std::string::npos) {
                auto start = cleaned.find("```json") + 7;
                auto end = cleaned.rfind("```");
                if (end != std::string::npos && end > start) {
                    cleaned = cleaned.substr(start, end - start);
                }
            } else if (cleaned.find("```") != std::string::npos) {
                auto start = cleaned.find("```") + 3;
                auto end = cleaned.rfind("```");
                if (end != std::string::npos && end > start) {
                    cleaned = cleaned.substr(start, end - start);
                }
            }
            auto s = cleaned.find_first_not_of(" \t\r\n");
            auto e = cleaned.find_last_not_of(" \t\r\n");
            if (s != std::string::npos) cleaned = cleaned.substr(s, e - s + 1);

            return json::parse(cleaned);
        } catch (...) {
            std::cerr << "[Gemini] Failed to parse notice analysis: " << response << "\n";
            return {{"score", 50}, {"summary", title}, {"category", "General"}};
        }
    }

    std::string summarize_deadline(const std::string& course, const std::string& title,
                                    const std::string& type) {
        std::string prompt = "Create a brief calendar event description (1-2 sentences, Korean) for:\n"
                            "Course: " + course + "\n"
                            "Type: " + type + "\n"
                            "Title: " + title + "\n"
                            "Return ONLY the description text, no JSON or formatting.";
        return chat(
            "You are a concise academic calendar assistant. Return only plain text.",
            prompt
        );
    }
};
