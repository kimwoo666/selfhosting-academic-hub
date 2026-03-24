#pragma once
// ============================================================
// saint_crawler.h -- SSU u-SAINT crawler via Python Script
// Executes scripts/crawl_grades.py using Selenium
// Supports Async Execution and Status Tracking
// ============================================================
#include "nlohmann/json.hpp"
#include "config.h"
#include <string>
#include <vector>
#include <iostream>
#include <array>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <mutex>

using json = nlohmann::json;

class SaintCrawler {
    LmsConfig cfg_;
    std::string student_id_;
    std::string password_;
    
    // Async State
    std::atomic<bool> is_crawling_{false};
    std::string current_status_{"Idle"};
    std::mutex status_mutex_;
    std::thread crawl_thread_;
    
    // Last Result
    json last_result_;
    std::mutex result_mutex_;

    void set_status(const std::string& status) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_status_ = status;
    }
    
    void set_result(const json& res) {
        std::lock_guard<std::mutex> lock(result_mutex_);
        last_result_ = res;
    }

public:
    explicit SaintCrawler(const LmsConfig& cfg) : cfg_(cfg) {
        student_id_ = cfg.username;
        password_ = cfg.password;
    }
    
    ~SaintCrawler() {
        if (crawl_thread_.joinable()) {
            crawl_thread_.join();
        }
    }

    void set_credentials(const std::string& id, const std::string& pw) {
        student_id_ = id;
        password_ = pw;
    }

    // Probes session validity (Mock for now as Python handles session)
    json probe_session() {
        return {{"alive", false}, {"message", "Session managed by external Python script"}};
    }

    bool login() {
        // Login is handled inside the python script per execution
        return true;
    }

    // Start background crawl
    void start_crawl() {
        if (is_crawling_) return;
        
        if (crawl_thread_.joinable()) {
            crawl_thread_.join();
        }
        
        is_crawling_ = true;
        set_status("Starting crawler...");
        
        crawl_thread_ = std::thread([this]() {
            this->run_crawl_script();
            this->is_crawling_ = false;
        });
        
        // Detach? No, safer to join in destructor or restart. 
        // But if we call start_crawl again, we join.
    }
    
    std::string get_status() {
        if (!is_crawling_ && current_status_ != "Idle" && current_status_.rfind("Complete", 0) != 0 && current_status_.rfind("Failed", 0) != 0) {
             // If thread finished but status wasn't updated to final state (unlikely with run_crawl_script logic)
             return current_status_;
        }
        std::lock_guard<std::mutex> lock(status_mutex_);
        return current_status_;
    }
    
    bool is_crawling() const {
        return is_crawling_;
    }
    
    json get_last_result() {
        std::lock_guard<std::mutex> lock(result_mutex_);
        return last_result_;
    }

    // Execute Python script and capture JSON output (Blocking version, kept for compatibility)
    json fetch_latest_grade_summary() {
        // Run blocking
        run_crawl_script();
        return get_last_result();
    }

private:
    void run_crawl_script() {
        if (student_id_.empty() || password_.empty()) {
            set_status("Error: No credentials");
            set_result({{"success", false}, {"message", "No credentials provided"}});
            return;
        }

        // Use python3 for Linux environment. 
        // NOTE: On Windows dev environment, might need 'python'. 
        // But target is LXC/Ubuntu. 
        // The user seems to be on Windows, so let's try 'python' if 'python3' fails? 
        // actually existing code used 'python3'.
        
        std::string cmd = "python3 scripts/crawl_grades.py " + student_id_ + " " + password_;
        
        #ifdef _WIN32
            // For local windows testing override
            cmd = "python scripts/crawl_grades.py " + student_id_ + " " + password_;
        #endif

        // Use popen to read line by line
        // Capture stderr too? "2>&1"
        cmd += " 2>&1";
        
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) {
            set_status("Error: popen failed");
            set_result({{"success", false}, {"message", "popen() failed"}});
            return;
        }
        
        std::array<char, 256> buffer;
        std::string full_output;
        
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            std::string line = buffer.data();
            full_output += line;
            
            // Check for PROGRESS:
            // "PROGRESS: message"
            // sanitize line
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

            if (line.rfind("PROGRESS:", 0) == 0) { // Starts with
                std::string msg = line.substr(9); // len("PROGRESS:")
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                set_status(msg);
            }
        }
        
        // Parse final output
        try {
            auto json_start = full_output.find('{');
            auto json_end = full_output.rfind('}');
            if (json_start != std::string::npos && json_end != std::string::npos) {
                std::string json_str = full_output.substr(json_start, json_end - json_start + 1);
                json res = json::parse(json_str);
                
                if (res.value("success", false)) {
                    set_status("Complete. Data fetched.");
                } else {
                    set_status("Failed: " + res.value("message", "Unknown error"));
                }
                set_result(res);
            } else {
                 set_status("Error: Invalid script output");
                 set_result({{"success", false}, {"message", "Invalid output"}, {"raw_output", full_output}});
            }
        } catch (const std::exception& e) {
             set_status(std::string("Error: ") + e.what());
             set_result({{"success", false}, {"message", std::string("JSON parse error: ") + e.what()}, {"raw_output", full_output}});
        }
    }
};
