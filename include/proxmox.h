#pragma once
// ============================================================
// proxmox.h ??Proxmox VE API client via libcurl
// ============================================================
#include "nlohmann/json.hpp"
#include "config.h"
#include <curl/curl.h>
#include <string>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

class ProxmoxClient {
    ProxmoxConfig cfg_;

    // Network throughput tracking (delta-based)
    long prev_netin_ = -1;
    long prev_netout_ = -1;
    std::chrono::steady_clock::time_point prev_net_time_;
    double cached_netin_mbps_ = 0.0;
    double cached_netout_mbps_ = 0.0;

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, std::string* data) {
        data->append(ptr, size * nmemb);
        return size * nmemb;
    }

    static std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    static std::string form_encode(const std::string& value) {
        CURL* curl = curl_easy_init();
        if (!curl) return value;
        char* encoded = curl_easy_escape(curl, value.c_str(), (int)value.size());
        std::string out = encoded ? encoded : value;
        if (encoded) curl_free(encoded);
        curl_easy_cleanup(curl);
        return out;
    }

    static bool has_non_empty_data(const json& resp) {
        if (resp.contains("error")) return false;
        if (resp.contains("errors") && !resp["errors"].empty()) return false;
        if (!resp.contains("data")) return false;
        const auto& d = resp["data"];
        if (d.is_null()) return false;
        if (d.is_string()) return !d.get<std::string>().empty();
        return true;
    }

    // Build Authorization header: PVEAPIToken=user!tokenname=tokenvalue
    std::string auth_header() const {
        return "Authorization: PVEAPIToken=" + cfg_.user + "!" + cfg_.token_name + "=" + cfg_.token_value;
    }

    // Perform GET request to Proxmox API
    json api_get(const std::string& endpoint) {
        std::string url = cfg_.host + endpoint;
        std::string response_str;

        CURL* curl = curl_easy_init();
        if (!curl) return {{"error", "curl init failed"}};

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, auth_header().c_str());
        headers = curl_slist_append(headers, "Accept: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // Proxmox uses self-signed certs by default
        if (!cfg_.verify_ssl) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[Proxmox] GET " << endpoint << " failed: " << curl_easy_strerror(res) << "\n";
            return {{"error", curl_easy_strerror(res)}};
        }

        try {
            return json::parse(response_str);
        } catch (...) {
            return {{"error", "JSON parse failed"}, {"raw", response_str}};
        }
    }

    // Perform POST request to Proxmox API
    json api_post(const std::string& endpoint, const std::string& body = "") {
        std::string url = cfg_.host + endpoint;
        std::string response_str;

        CURL* curl = curl_easy_init();
        if (!curl) return {{"error", "curl init failed"}};

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, auth_header().c_str());
        headers = curl_slist_append(headers, "Accept: application/json");
        // Proxmox API expects form-encoded POST bodies.
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        if (!cfg_.verify_ssl) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "[Proxmox] POST " << endpoint << " failed: " << curl_easy_strerror(res) << "\n";
            return {{"error", curl_easy_strerror(res)}};
        }

        try {
            auto parsed = json::parse(response_str);
            if (http_code >= 400) {
                if (!parsed.contains("error")) {
                    parsed["error"] = "HTTP " + std::to_string(http_code);
                }
                parsed["http_code"] = http_code;
            }
            return parsed;
        } catch (...) {
            // Proxmox sometimes returns a raw UPID string ??wrap it
            if (!response_str.empty() && response_str.find("UPID:") != std::string::npos) {
                return {{"data", response_str}};
            }
            // HTTP error pages (HTML)
            if (http_code >= 400) {
                return {{"error", "HTTP " + std::to_string(http_code)}, {"raw", response_str}};
            }
            return {{"error", "JSON parse failed"}, {"raw", response_str}};
        }
    }

public:
    explicit ProxmoxClient(const ProxmoxConfig& cfg) : cfg_(cfg) {}

    // ?? Get cluster/node status ??
    json get_node_status() {
        auto resp = api_get("/api2/json/nodes/" + cfg_.node + "/status");
        if (resp.contains("data")) {
            auto& d = resp["data"];

            // Network traffic: compute rate from cumulative counter delta
            long netin_bytes = 0, netout_bytes = 0;
            if (d.contains("netin")) netin_bytes = d["netin"].get<long>();
            if (d.contains("netout")) netout_bytes = d["netout"].get<long>();

            auto now = std::chrono::steady_clock::now();
            if (prev_netin_ >= 0) {
                double elapsed = std::chrono::duration<double>(now - prev_net_time_).count();
                if (elapsed > 0.5) {
                    cached_netin_mbps_ = (double)(netin_bytes - prev_netin_) / elapsed / 1048576.0;
                    cached_netout_mbps_ = (double)(netout_bytes - prev_netout_) / elapsed / 1048576.0;
                    if (cached_netin_mbps_ < 0) cached_netin_mbps_ = 0;
                    if (cached_netout_mbps_ < 0) cached_netout_mbps_ = 0;
                }
            }
            prev_netin_ = netin_bytes;
            prev_netout_ = netout_bytes;
            prev_net_time_ = now;

            return {
                {"cpu", d.value("cpu", 0.0) * 100.0},
                {"ram_used", d.value("memory", json::object()).value("used", 0L)},
                {"ram_total", d.value("memory", json::object()).value("total", 1L)},
                {"ram_pct", d.contains("memory") ? 
                    (double)d["memory"].value("used", 0L) / (double)d["memory"].value("total", 1L) * 100.0 : 0.0},
                {"uptime", d.value("uptime", 0)},
                {"disk_used", d.value("rootfs", json::object()).value("used", 0L)},
                {"disk_total", d.value("rootfs", json::object()).value("total", 1L)},
                {"disk_pct", d.contains("rootfs") ?
                    (double)d["rootfs"].value("used", 0L) / (double)d["rootfs"].value("total", 1L) * 100.0 : 0.0},
                {"netin_mbps", std::round(cached_netin_mbps_ * 100) / 100},
                {"netout_mbps", std::round(cached_netout_mbps_ * 100) / 100}
            };
        }
        return resp; // return error as-is
    }

    // ?? List all LXC containers ??
    json get_containers() {
        auto resp = api_get("/api2/json/nodes/" + cfg_.node + "/lxc");
        json result = json::array();

        if (resp.contains("data") && resp["data"].is_array()) {
            for (auto& ct : resp["data"]) {
                std::string status = ct.value("status", "unknown");
                double cpu_pct = ct.value("cpu", 0.0) * 100.0;
                long maxmem = ct.value("maxmem", 1L);
                long mem = ct.value("mem", 0L);
                double ram_pct = maxmem > 0 ? (double)mem / (double)maxmem * 100.0 : 0.0;
                long maxdisk = ct.value("maxdisk", 1L);
                long disk = ct.value("disk", 0L);
                double disk_pct = maxdisk > 0 ? (double)disk / (double)maxdisk * 100.0 : 0.0;

                std::string ct_status = "stopped";
                if (status == "running") {
                    ct_status = cpu_pct > 70 ? "high_load" : "running";
                }

                result.push_back({
                    {"id", ct.value("vmid", 0)},
                    {"name", ct.value("name", "unknown")},
                    {"status", ct_status},
                    {"type", "lxc"},
                    {"cpu_pct", std::round(cpu_pct * 10) / 10},
                    {"ram_pct", std::round(ram_pct * 10) / 10},
                    {"disk_pct", std::round(disk_pct * 10) / 10},
                    {"maxmem", maxmem},
                    {"mem", mem},
                    {"maxdisk", maxdisk},
                    {"disk_used", disk},
                    {"uptime", ct.value("uptime", 0)}
                });
            }
        }

        // Also get VMs (QEMU)
        auto qemu_resp = api_get("/api2/json/nodes/" + cfg_.node + "/qemu");
        if (qemu_resp.contains("data") && qemu_resp["data"].is_array()) {
            for (auto& vm : qemu_resp["data"]) {
                std::string status = vm.value("status", "unknown");
                double cpu_pct = vm.value("cpu", 0.0) * 100.0;
                long maxmem = vm.value("maxmem", 1L);
                long mem = vm.value("mem", 0L);
                double ram_pct = maxmem > 0 ? (double)mem / (double)maxmem * 100.0 : 0.0;

                std::string vm_status = "stopped";
                if (status == "running") {
                    vm_status = cpu_pct > 70 ? "high_load" : "running";
                }

                long maxdisk = vm.value("maxdisk", 0L);
                long disk = vm.value("disk", 0L);
                double disk_pct = maxdisk > 0 ? (double)disk / (double)maxdisk * 100.0 : 0.0;

                result.push_back({
                    {"id", vm.value("vmid", 0)},
                    {"name", vm.value("name", "unknown")},
                    {"status", vm_status},
                    {"type", "qemu"},
                    {"cpu_pct", std::round(cpu_pct * 10) / 10},
                    {"ram_pct", std::round(ram_pct * 10) / 10},
                    {"disk_pct", std::round(disk_pct * 10) / 10},
                    {"maxmem", maxmem},
                    {"mem", mem},
                    {"maxdisk", maxdisk},
                    {"disk_used", disk},
                    {"uptime", vm.value("uptime", 0)}
                });
            }
        }

        return result;
    }

    // Helper: format bytes to human-readable (e.g. 1073741824 -> "1.0G")
    static std::string format_bytes(long bytes) {
        if (bytes <= 0) return "0";
        if (bytes >= 1073741824L) {
            double gb = (double)bytes / 1073741824.0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1fG", gb);
            return buf;
        }
        double mb = (double)bytes / 1048576.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0fM", mb);
        return buf;
    }

    // ?? Build /api/nodes response (for the frontend Nodes page) ??
    json get_nodes_for_frontend() {
        auto containers = get_containers();
        json nodes = json::array();

        for (auto& ct : containers) {
            long maxmem = ct.value("maxmem", 0L);
            long mem = ct.value("mem", 0L);
            long maxdisk = ct.value("maxdisk", 0L);
            long disk_used = ct.value("disk_used", 0L);

            std::string ram_str = format_bytes(mem) + " / " + format_bytes(maxmem);
            std::string disk_str = format_bytes(disk_used) + " / " + format_bytes(maxdisk);

            nodes.push_back({
                {"id", ct["id"]},
                {"name", ct["name"]},
                {"status", ct["status"]},
                {"ip", "192.168.1." + std::to_string(100 + ct.value("id", 0) % 100)},
                {"os", ct.value("type", "lxc") == "lxc" ? "Ubuntu" : "Debian"},
                {"cpu", ct["cpu_pct"]},
                {"ram", ram_str},
                {"disk", disk_str},
                {"disk_pct", ct["disk_pct"]},
                {"cpu_pct", ct["cpu_pct"]},
                {"ram_pct", ct["ram_pct"]},
                {"maxmem", maxmem},
                {"mem", mem},
                {"maxdisk", maxdisk},
                {"disk_used", disk_used}
            });
        }
        return nodes;
    }

    // ?? Build /api/summary response data (system overview) ??
    json get_summary_system() {
        auto node = get_node_status();
        if (node.contains("error")) return node;

        // Convert uptime seconds to human-readable
        int uptime_sec = node.value("uptime", 0);
        int days = uptime_sec / 86400;
        int hours = (uptime_sec % 86400) / 3600;
        int mins = (uptime_sec % 3600) / 60;
        std::string uptime_str = std::to_string(days) + "d " + std::to_string(hours) + "h " + std::to_string(mins) + "m";

        auto containers = get_containers();
        int running = 0;
        for (auto& c : containers) {
            if (c.value("status", "stopped") != "stopped") running++;
        }

        // Build containers for dashboard display
        json ct_display = json::array();
        for (auto& c : containers) {
            std::string ct_id = c.value("type", "lxc") == "lxc"
                ? "lxc-" + std::to_string(c.value("id", 0))
                : "vm-" + std::to_string(c.value("id", 0));
            long maxmem = c.value("maxmem", 0L);
            long mem_used = c.value("mem", 0L);
            ct_display.push_back({
                {"id", ct_id},
                {"name", c["name"]},
                {"status", c["status"]},
                {"cpu", c["cpu_pct"]},
                {"ram", c["ram_pct"]},
                {"ram_used_mb", (double)mem_used / 1048576.0},
                {"ram_total_mb", (double)maxmem / 1048576.0}
            });
        }

        return {
            {"system", {
                {"cpu", node.value("cpu", 0.0)},
                {"ram", node.value("ram_pct", 0.0)},
                {"disk", node.value("disk_pct", 0.0)},
                {"temp", 55.0}
            }},
            {"uptime", uptime_str},
            {"kernel", "6.5.0-generic"},
            {"active_nodes", running},
            {"total_nodes", (int)containers.size()},
            {"containers", ct_display},
            {"network", {
                {"download_mbps", node.value("netin_mbps", 0.0)},
                {"upload_mbps", node.value("netout_mbps", 0.0)}
            }}
        };
    }

    // ?? Determine if a VMID is LXC or QEMU ??
    std::string detect_type(int vmid) {
        // Check LXC list
        auto lxc = api_get("/api2/json/nodes/" + cfg_.node + "/lxc");
        if (lxc.contains("data") && lxc["data"].is_array()) {
            for (auto& ct : lxc["data"]) {
                if (ct.value("vmid", 0) == vmid) return "lxc";
            }
        }
        // Check QEMU list
        auto qemu = api_get("/api2/json/nodes/" + cfg_.node + "/qemu");
        if (qemu.contains("data") && qemu["data"].is_array()) {
            for (auto& vm : qemu["data"]) {
                if (vm.value("vmid", 0) == vmid) return "qemu";
            }
        }
        return "unknown";
    }

    // ?? Container control ??
    json control_container(int vmid, const std::string& action) {
        // Map frontend action names to Proxmox API action names
        std::string pve_action = action;
        if (action == "restart") pve_action = "reboot";   // Proxmox uses "reboot" not "restart"
        if (action == "shutdown") pve_action = "shutdown"; // graceful stop

        // Determine container type from the actual container list
        std::string type = detect_type(vmid);
        if (type == "unknown") {
            return {
                {"success", false},
                {"node_id", vmid},
                {"action", action},
                {"message", "VMID " + std::to_string(vmid) + " not found on node " + cfg_.node}
            };
        }

        std::string endpoint = "/api2/json/nodes/" + cfg_.node + "/" + type + "/" + std::to_string(vmid) + "/status/" + pve_action;
        auto resp = api_post(endpoint);

        if (has_non_empty_data(resp)) {
            return {
                {"success", true},
                {"node_id", vmid},
                {"action", pve_action},
                {"message", "Action '" + pve_action + "' executed on " + type + " " + std::to_string(vmid)},
                {"upid", resp["data"]}
            };
        }

        // Extract better error message
        std::string err_msg = "Unknown error";
        if (resp.contains("errors") && resp["errors"].is_object()) {
            for (auto& [k, v] : resp["errors"].items()) {
                err_msg = k + ": " + v.get<std::string>();
                break;
            }
        } else if (resp.contains("message")) {
            err_msg = resp["message"].get<std::string>();
        } else if (resp.contains("error")) {
            err_msg = resp["error"].get<std::string>();
        }

        return {
            {"success", false},
            {"node_id", vmid},
            {"action", pve_action},
            {"message", err_msg}
        };
    }

    // ?? Deploy a new LXC node by cloning a template ??
    json deploy_container(const std::string& base_name = "academic-hub-node") {
        // Next VMID
        auto nextid_resp = api_get("/api2/json/cluster/nextid");
        int nextid = 0;
        if (nextid_resp.contains("data")) {
            if (nextid_resp["data"].is_number_integer()) nextid = nextid_resp["data"].get<int>();
            else if (nextid_resp["data"].is_string()) nextid = std::stoi(nextid_resp["data"].get<std::string>());
        }
        if (nextid <= 0) {
            return {
                {"success", false},
                {"message", "Failed to get next VMID from Proxmox"},
                {"error", nextid_resp.value("error", "unknown")}
            };
        }

        // Pick an LXC template
        auto lxc_resp = api_get("/api2/json/nodes/" + cfg_.node + "/lxc");
        int template_vmid = 0;
        std::string template_name = "";
        if (lxc_resp.contains("data") && lxc_resp["data"].is_array()) {
            for (auto& ct : lxc_resp["data"]) {
                bool is_template = false;
                if (ct.contains("template")) {
                    if (ct["template"].is_boolean()) is_template = ct["template"].get<bool>();
                    else if (ct["template"].is_number_integer()) is_template = ct["template"].get<int>() == 1;
                }
                if (!is_template) {
                    std::string name_low = to_lower(ct.value("name", ""));
                    if (name_low.find("template") != std::string::npos) is_template = true;
                }
                if (is_template) {
                    template_vmid = ct.value("vmid", 0);
                    template_name = ct.value("name", "");
                    break;
                }
            }
        }

        if (template_vmid <= 0) {
            return {
                {"success", false},
                {"message", "No LXC template found on node " + cfg_.node + ". Create a template first."}
            };
        }

        std::string hostname = base_name + "-" + std::to_string(nextid);
        std::string endpoint = "/api2/json/nodes/" + cfg_.node + "/lxc/" + std::to_string(template_vmid) + "/clone";
        std::string body = "newid=" + form_encode(std::to_string(nextid)) +
                           "&hostname=" + form_encode(hostname) +
                           "&full=1";

        auto resp = api_post(endpoint, body);
        if (has_non_empty_data(resp)) {
            return {
                {"success", true},
                {"node_id", nextid},
                {"template_vmid", template_vmid},
                {"template_name", template_name},
                {"hostname", hostname},
                {"message", "Deploy started for " + hostname},
                {"upid", resp["data"]}
            };
        }

        std::string err_msg = "Unknown deploy error";
        if (resp.contains("errors") && resp["errors"].is_object()) {
            for (auto& [k, v] : resp["errors"].items()) {
                err_msg = k + ": " + v.get<std::string>();
                break;
            }
        } else if (resp.contains("message")) {
            err_msg = resp["message"].get<std::string>();
        } else if (resp.contains("error")) {
            err_msg = resp["error"].get<std::string>();
        }

        return {
            {"success", false},
            {"node_id", nextid},
            {"template_vmid", template_vmid},
            {"message", err_msg}
        };
    }
};
