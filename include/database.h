#pragma once
// ============================================================
// database.h — SQLite wrapper for academic data
// ============================================================
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>
#include <sqlite3.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>

using json = nlohmann::json;

class Database {
    sqlite3* db_ = nullptr;

    static std::string ascii_lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static int semester_order(const std::string& semester) {
        const std::string lowered = ascii_lower(semester);

        if (!semester.empty() && semester.front() == '1') return 1;
        if (semester.find("\xEB\xB4\x84") != std::string::npos || lowered.find("spring") != std::string::npos) return 1;
        if (semester.find("\xEC\x97\xAC\xEB\xA6\x84") != std::string::npos || lowered.find("summer") != std::string::npos) return 2;
        if (!semester.empty() && semester.front() == '2') return 3;
        if (semester.find("\xEA\xB0\x80\xEC\x9D\x84") != std::string::npos || lowered.find("fall") != std::string::npos || lowered.find("autumn") != std::string::npos) return 3;
        if (semester.find("\xEA\xB2\xA8\xEC\x9A\xB8") != std::string::npos || lowered.find("winter") != std::string::npos) return 4;

        return 0;
    }

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("SQL error: " + msg);
        }
    }

public:
    explicit Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            throw std::runtime_error("Cannot open database: " + path);
        }
        exec("PRAGMA journal_mode=WAL;");
        exec("PRAGMA foreign_keys=ON;");
        init_schema();
        seed_if_empty();
    }

    ~Database() {
        if (db_) sqlite3_close(db_);
    }

    // ── Schema ──
    void init_schema() {
        exec(R"(
            CREATE TABLE IF NOT EXISTS profile (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                name TEXT DEFAULT 'User',
                role TEXT DEFAULT 'System Admin',
                user_id TEXT DEFAULT 'J-0001',
                level TEXT DEFAULT '01',
                avatar_url TEXT DEFAULT 'https://ui-avatars.com/api/?name=User&background=13a4ec&color=fff&size=128',
                term TEXT DEFAULT 'Spring 2026',
                status TEXT DEFAULT 'ACTIVE',
                semester_progress INTEGER DEFAULT 0,
                security_score INTEGER DEFAULT 98,
                version TEXT DEFAULT 'v2.4.1 (Build 8920)',
                university TEXT DEFAULT '',
                email TEXT DEFAULT '',
                student_id TEXT DEFAULT ''
            );

            CREATE TABLE IF NOT EXISTS devices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                icon TEXT DEFAULT 'devices',
                last_active TEXT DEFAULT 'Just now',
                online INTEGER DEFAULT 1
            );

            CREATE TABLE IF NOT EXISTS academic_summary (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                gpa REAL DEFAULT 0.0,
                credits REAL DEFAULT 0.0,
                rank TEXT DEFAULT '',
                semester TEXT DEFAULT 'Spring 2026',
                week TEXT DEFAULT '',
                projected_gpa REAL DEFAULT 0.0,
                forecast_confidence INTEGER DEFAULT 0,
                forecast_risk TEXT DEFAULT ''
            );

            CREATE TABLE IF NOT EXISTS courses (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                code TEXT NOT NULL,
                name TEXT NOT NULL,
                full_code TEXT,
                professor TEXT,
                grade TEXT,
                score REAL,
                credits REAL DEFAULT 0.0,
                color TEXT DEFAULT 'primary'
            );

            CREATE TABLE IF NOT EXISTS semester_grades (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                year INTEGER NOT NULL,
                semester TEXT NOT NULL,
                gpa REAL DEFAULT 0.0,
                earned_credits REAL DEFAULT 0.0,
                total_credits REAL DEFAULT 0.0,
                rank TEXT DEFAULT '',
                UNIQUE(year, semester)
            );

            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                date TEXT NOT NULL,
                time TEXT,
                type TEXT DEFAULT 'Task',
                title TEXT NOT NULL,
                location TEXT DEFAULT '',
                duration TEXT DEFAULT '',
                description TEXT DEFAULT '',
                attendees INTEGER DEFAULT 0,
                done INTEGER DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS alerts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                severity TEXT NOT NULL DEFAULT 'info',
                category TEXT NOT NULL DEFAULT 'System',
                title TEXT NOT NULL,
                message TEXT DEFAULT '',
                time TEXT DEFAULT '',
                color TEXT DEFAULT 'primary',
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS notices (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                source TEXT NOT NULL,
                title TEXT NOT NULL,
                url TEXT NOT NULL UNIQUE,
                date TEXT DEFAULT '',
                relevance_score INTEGER DEFAULT 0,
                ai_summary TEXT DEFAULT '',
                processed INTEGER DEFAULT 0,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS lms_courses (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                lms_id TEXT UNIQUE,
                name TEXT NOT NULL,
                professor TEXT DEFAULT ''
            );

            CREATE TABLE IF NOT EXISTS lms_deadlines (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                course_id INTEGER,
                type TEXT NOT NULL,
                title TEXT NOT NULL,
                start_time TEXT DEFAULT '',
                end_time TEXT DEFAULT '',
                synced_to_gcal INTEGER DEFAULT 0,
                gcal_event_id TEXT DEFAULT '',
                FOREIGN KEY(course_id) REFERENCES lms_courses(id)
            );
        )");

        // Migration: add new profile columns if they don't exist yet
        migrate_profile_columns();
    }

    void migrate_profile_columns() {
        // Check if university column exists on profile
        sqlite3_stmt* stmt;
        bool has_university = false;
        sqlite3_prepare_v2(db_, "PRAGMA table_info(profile)", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string col_name = (const char*)sqlite3_column_text(stmt, 1);
            if (col_name == "university") has_university = true;
        }
        sqlite3_finalize(stmt);

        if (!has_university) {
            exec("ALTER TABLE profile ADD COLUMN university TEXT DEFAULT '';");
            exec("ALTER TABLE profile ADD COLUMN email TEXT DEFAULT '';");
            exec("ALTER TABLE profile ADD COLUMN student_id TEXT DEFAULT '';");
            std::cout << "[DB] Migrated profile table with new columns.\n";
        }
        
        // Migrate courses table (add year, semester)
        bool has_year = false;
        bool has_credits = false;
        sqlite3_prepare_v2(db_, "PRAGMA table_info(courses)", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string col_name = (const char*)sqlite3_column_text(stmt, 1);
            if (col_name == "year") has_year = true;
            if (col_name == "credits") has_credits = true;
        }
        sqlite3_finalize(stmt);
        
        if (!has_year) {
             exec("ALTER TABLE courses ADD COLUMN year INTEGER;");
             exec("ALTER TABLE courses ADD COLUMN semester TEXT;");
             std::cout << "[DB] Migrated courses table with year/semester columns.\n";
        }

        if (!has_credits) {
            exec("ALTER TABLE courses ADD COLUMN credits REAL DEFAULT 0.0;");
            exec("UPDATE courses SET credits = COALESCE(score, 0.0) WHERE credits IS NULL OR credits = 0.0;");
            std::cout << "[DB] Migrated courses table with credits column.\n";
        }

        bool has_semester_grades = false;
        sqlite3_prepare_v2(db_, "SELECT name FROM sqlite_master WHERE type='table' AND name='semester_grades'", -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            has_semester_grades = true;
        }
        sqlite3_finalize(stmt);

        if (!has_semester_grades) {
            exec(R"(
                CREATE TABLE IF NOT EXISTS semester_grades (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    year INTEGER NOT NULL,
                    semester TEXT NOT NULL,
                    gpa REAL DEFAULT 0.0,
                    earned_credits REAL DEFAULT 0.0,
                    total_credits REAL DEFAULT 0.0,
                    rank TEXT DEFAULT '',
                    UNIQUE(year, semester)
                );
            )");
            std::cout << "[DB] Created semester_grades table.\n";
        }
    }

    // ── Seed default data if tables are empty ──
    void seed_if_empty() {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM profile", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (count == 0) {
            exec("INSERT INTO profile (id) VALUES (1);");
            // DO NOT seed academic_summary to allow empty state ("needs_login")
            // exec("INSERT INTO academic_summary (id) VALUES (1);");
            std::cout << "[DB] Seeded default profile.\n";
        }
    }

    // ── Profile ──
    json get_profile() {
        json result;
        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(db_,
            "SELECT name, role, user_id, level, avatar_url, term, status, "
            "semester_progress, security_score, version, university, email, student_id "
            "FROM profile WHERE id=1", -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result["name"] = (const char*)sqlite3_column_text(stmt, 0);
            result["role"] = (const char*)sqlite3_column_text(stmt, 1);
            result["id"] = (const char*)sqlite3_column_text(stmt, 2);
            result["level"] = (const char*)sqlite3_column_text(stmt, 3);
            result["avatar_url"] = (const char*)sqlite3_column_text(stmt, 4);
            result["term"] = (const char*)sqlite3_column_text(stmt, 5);
            result["status"] = (const char*)sqlite3_column_text(stmt, 6);
            result["semester_progress"] = sqlite3_column_int(stmt, 7);
            result["security_score"] = sqlite3_column_int(stmt, 8);
            result["version"] = (const char*)sqlite3_column_text(stmt, 9);
            result["university"] = sqlite3_column_text(stmt, 10) ? (const char*)sqlite3_column_text(stmt, 10) : "";
            result["email"] = sqlite3_column_text(stmt, 11) ? (const char*)sqlite3_column_text(stmt, 11) : "";
            result["student_id"] = sqlite3_column_text(stmt, 12) ? (const char*)sqlite3_column_text(stmt, 12) : "";
        }
        sqlite3_finalize(stmt);

        // Devices
        result["devices"] = json::array();
        sqlite3_prepare_v2(db_, "SELECT name, icon, last_active, online FROM devices ORDER BY id", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result["devices"].push_back({
                {"name", (const char*)sqlite3_column_text(stmt, 0)},
                {"icon", (const char*)sqlite3_column_text(stmt, 1)},
                {"last_active", (const char*)sqlite3_column_text(stmt, 2)},
                {"online", sqlite3_column_int(stmt, 3) != 0}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ── Update Profile ──
    bool update_profile(const json& data) {
        // Build dynamic UPDATE query from provided fields
        std::vector<std::string> fields;
        std::vector<std::string> values;

        auto add_field = [&](const std::string& key, const std::string& col) {
            if (data.contains(key) && data[key].is_string()) {
                fields.push_back(col + " = ?");
                values.push_back(data[key].get<std::string>());
            }
        };

        add_field("name", "name");
        add_field("role", "role");
        add_field("id", "user_id");
        add_field("level", "level");
        add_field("avatar_url", "avatar_url");
        add_field("term", "term");
        add_field("status", "status");
        add_field("university", "university");
        add_field("email", "email");
        add_field("student_id", "student_id");
        add_field("version", "version");

        // Integer fields
        if (data.contains("semester_progress") && data["semester_progress"].is_number()) {
            fields.push_back("semester_progress = ?");
            values.push_back(std::to_string(data["semester_progress"].get<int>()));
        }
        if (data.contains("security_score") && data["security_score"].is_number()) {
            fields.push_back("security_score = ?");
            values.push_back(std::to_string(data["security_score"].get<int>()));
        }

        if (fields.empty()) return false;

        std::string sql = "UPDATE profile SET ";
        for (size_t i = 0; i < fields.size(); i++) {
            if (i > 0) sql += ", ";
            sql += fields[i];
        }
        sql += " WHERE id = 1";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        for (size_t i = 0; i < values.size(); i++) {
            sqlite3_bind_text(stmt, (int)i + 1, values[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    // ── Grades ──
    json get_grades() {
        json result;
        sqlite3_stmt* stmt;

        // Summary
        sqlite3_prepare_v2(db_, "SELECT gpa, credits, rank, semester, week, projected_gpa, forecast_confidence, forecast_risk FROM academic_summary WHERE id=1", -1, &stmt, nullptr);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result["gpa"] = sqlite3_column_double(stmt, 0);
            result["credits"] = sqlite3_column_double(stmt, 1);
            result["rank"] = (const char*)sqlite3_column_text(stmt, 2);
            result["semester"] = (const char*)sqlite3_column_text(stmt, 3);
            result["week"] = (const char*)sqlite3_column_text(stmt, 4);
            result["ai_forecast"] = {
                {"projected_gpa", sqlite3_column_double(stmt, 5)},
                {"confidence", sqlite3_column_int(stmt, 6)},
                {"risk", sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : ""}
            };
            sqlite3_finalize(stmt);
        } else {
             // No summary row -> Needs Login
             result["status"] = "needs_login";
             sqlite3_finalize(stmt);
             return result;
        }

        // Courses
        result["courses"] = json::array();
        // Check if year/semester columns exist (handle migration case gracefully if queried before migration? wrapper handles init)
        sqlite3_prepare_v2(
            db_,
            "SELECT code, name, full_code, professor, grade, score, credits, color, year, semester "
            "FROM courses ORDER BY year DESC, id DESC, name",
            -1, &stmt, nullptr
        );
        
        if (stmt) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const double score = sqlite3_column_double(stmt, 5);
                const double credits = sqlite3_column_type(stmt, 6) == SQLITE_NULL
                    ? score
                    : sqlite3_column_double(stmt, 6);
                json course = {
                    {"code", (const char*)sqlite3_column_text(stmt, 0)},
                    {"name", (const char*)sqlite3_column_text(stmt, 1)},
                    {"full_code", (const char*)sqlite3_column_text(stmt, 2)},
                    {"professor", (const char*)sqlite3_column_text(stmt, 3)},
                    {"grade", (const char*)sqlite3_column_text(stmt, 4)},
                    {"score", score},
                    {"credits", credits},
                    {"color", (const char*)sqlite3_column_text(stmt, 7)}
                };
                if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) course["year"] = sqlite3_column_int(stmt, 8);
                if (sqlite3_column_type(stmt, 9) != SQLITE_NULL) course["semester"] = (const char*)sqlite3_column_text(stmt, 9);
                
                result["courses"].push_back(course);
            }
            sqlite3_finalize(stmt);
        } else {
             // Fallback if column missing (should not happen if init called)
             sqlite3_prepare_v2(db_, "SELECT code, name, full_code, professor, grade, score, credits, color FROM courses ORDER BY id", -1, &stmt, nullptr);
             while (sqlite3_step(stmt) == SQLITE_ROW) {
                const double score = sqlite3_column_double(stmt, 5);
                const double credits = sqlite3_column_type(stmt, 6) == SQLITE_NULL
                    ? score
                    : sqlite3_column_double(stmt, 6);
                result["courses"].push_back({
                    {"code", (const char*)sqlite3_column_text(stmt, 0)},
                    {"name", (const char*)sqlite3_column_text(stmt, 1)},
                    {"full_code", (const char*)sqlite3_column_text(stmt, 2)},
                    {"professor", (const char*)sqlite3_column_text(stmt, 3)},
                    {"grade", (const char*)sqlite3_column_text(stmt, 4)},
                    {"score", score},
                    {"credits", credits},
                    {"color", (const char*)sqlite3_column_text(stmt, 7)}
                });
             }
             sqlite3_finalize(stmt);
        }

        result["semesters_data"] = json::array();
        sqlite3_prepare_v2(
            db_,
            "SELECT year, semester, gpa, earned_credits, total_credits, rank "
            "FROM semester_grades",
            -1, &stmt, nullptr
        );

        if (stmt) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int year = sqlite3_column_int(stmt, 0);
                std::string semester = sqlite3_column_text(stmt, 1)
                    ? (const char*)sqlite3_column_text(stmt, 1)
                    : "";

                json semester_data = {
                    {"year", year},
                    {"semester", semester},
                    {"gpa", sqlite3_column_double(stmt, 2)},
                    {"earned_credits", sqlite3_column_double(stmt, 3)},
                    {"total_credits", sqlite3_column_double(stmt, 4)},
                    {"rank", sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : ""},
                    {"courses", json::array()}
                };

                sqlite3_stmt* course_stmt = nullptr;
                sqlite3_prepare_v2(
                    db_,
                    "SELECT code, name, full_code, professor, grade, score, credits, color "
                    "FROM courses WHERE year = ? AND semester = ? ORDER BY name",
                    -1, &course_stmt, nullptr
                );

                if (course_stmt) {
                    sqlite3_bind_int(course_stmt, 1, year);
                    sqlite3_bind_text(course_stmt, 2, semester.c_str(), -1, SQLITE_TRANSIENT);

                    while (sqlite3_step(course_stmt) == SQLITE_ROW) {
                        const double score = sqlite3_column_double(course_stmt, 5);
                        const double credits = sqlite3_column_type(course_stmt, 6) == SQLITE_NULL
                            ? score
                            : sqlite3_column_double(course_stmt, 6);
                        semester_data["courses"].push_back({
                            {"code", sqlite3_column_text(course_stmt, 0) ? (const char*)sqlite3_column_text(course_stmt, 0) : ""},
                            {"name", sqlite3_column_text(course_stmt, 1) ? (const char*)sqlite3_column_text(course_stmt, 1) : ""},
                            {"full_code", sqlite3_column_text(course_stmt, 2) ? (const char*)sqlite3_column_text(course_stmt, 2) : ""},
                            {"professor", sqlite3_column_text(course_stmt, 3) ? (const char*)sqlite3_column_text(course_stmt, 3) : ""},
                            {"grade", sqlite3_column_text(course_stmt, 4) ? (const char*)sqlite3_column_text(course_stmt, 4) : ""},
                            {"score", score},
                            {"credits", credits},
                            {"color", sqlite3_column_text(course_stmt, 7) ? (const char*)sqlite3_column_text(course_stmt, 7) : ""},
                            {"year", year},
                            {"semester", semester}
                        });
                    }
                    sqlite3_finalize(course_stmt);
                }

                result["semesters_data"].push_back(semester_data);
            }
            sqlite3_finalize(stmt);
        }

        std::sort(result["semesters_data"].begin(), result["semesters_data"].end(), [](const json& left, const json& right) {
            const int left_year = left.value("year", 0);
            const int right_year = right.value("year", 0);
            if (left_year != right_year) return left_year > right_year;

            const std::string left_semester = left.value("semester", "");
            const std::string right_semester = right.value("semester", "");
            const int left_order = Database::semester_order(left_semester);
            const int right_order = Database::semester_order(right_semester);
            if (left_order != right_order) return left_order > right_order;

            return left_semester > right_semester;
        });
        return result;
    }

    bool update_academic_summary(double gpa, double credits, const std::string& rank, const std::string& semester) {
        // Ensure row exists
        exec("INSERT OR IGNORE INTO academic_summary (id) VALUES (1)");

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "UPDATE academic_summary SET gpa = ?, credits = ?, rank = ?, semester = ? WHERE id = 1",
            -1, &stmt, nullptr);
        sqlite3_bind_double(stmt, 1, gpa);
        sqlite3_bind_double(stmt, 2, credits);
        sqlite3_bind_text(stmt, 3, rank.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, semester.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    // Save detailed semester grades
    bool save_semester_grades(const json& semesters_data) {
        if (!semesters_data.is_array()) return false;
        
        exec("BEGIN TRANSACTION;");
        
        try {
            // First, clear existing courses to avoid duplicates
            exec("DELETE FROM courses;");
            exec("DELETE FROM semester_grades;");
            
            // Note: We need to ensure the columns exist (year, semester).
            // They are added in schema migration.
            
            sqlite3_stmt* course_stmt = nullptr;
            sqlite3_stmt* semester_stmt = nullptr;
            const char* course_sql = "INSERT INTO courses (code, name, full_code, professor, grade, score, credits, color, year, semester) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            const char* semester_sql = "INSERT INTO semester_grades (year, semester, gpa, earned_credits, total_credits, rank) "
                                       "VALUES (?, ?, ?, ?, ?, ?)";
                              
            if (sqlite3_prepare_v2(db_, course_sql, -1, &course_stmt, nullptr) != SQLITE_OK) {
                // Determine if failure is due to missing columns (if migration didn't run yet? construction runs migration)
                 throw std::runtime_error("Failed to prepare statement. Schema might be outdated.");
            }            

            if (sqlite3_prepare_v2(db_, semester_sql, -1, &semester_stmt, nullptr) != SQLITE_OK) {
                sqlite3_finalize(course_stmt);
                throw std::runtime_error("Failed to prepare semester statement. Schema might be outdated.");
            }

            for (const auto& sem : semesters_data) {
                int year = sem.value("year", 0);
                std::string semester_name = sem.value("semester", "");
                double gpa = sem.value("gpa", 0.0);
                double earned_credits = sem.value("earned_credits", 0.0);
                double total_credits = sem.value("total_credits", 0.0);
                std::string rank = sem.value("rank", "");

                if (total_credits <= 0.0 && sem.contains("courses") && sem["courses"].is_array()) {
                    for (const auto& c : sem["courses"]) {
                        total_credits += c.value("credits", 0.0);
                    }
                }

                sqlite3_reset(semester_stmt);
                sqlite3_clear_bindings(semester_stmt);
                sqlite3_bind_int(semester_stmt, 1, year);
                sqlite3_bind_text(semester_stmt, 2, semester_name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_double(semester_stmt, 3, gpa);
                sqlite3_bind_double(semester_stmt, 4, earned_credits);
                sqlite3_bind_double(semester_stmt, 5, total_credits);
                sqlite3_bind_text(semester_stmt, 6, rank.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(semester_stmt);
                
                if (sem.contains("courses") && sem["courses"].is_array()) {
                    for (const auto& c : sem["courses"]) {
                        std::string code = c.value("code", "");
                        std::string name = c.value("name", "");
                        std::string full_code = c.value("full_code", code);
                        std::string professor = c.value("professor", "");
                        std::string grade = c.value("grade", "");
                        double score = c.value("score", 0.0);
                        double credits = c.value("credits", 0.0);
                        std::string color = c.value("color", "primary");
                        
                        sqlite3_reset(course_stmt);
                        sqlite3_clear_bindings(course_stmt);
                        sqlite3_bind_text(course_stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(course_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(course_stmt, 3, full_code.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(course_stmt, 4, professor.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(course_stmt, 5, grade.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_double(course_stmt, 6, score);
                        sqlite3_bind_double(course_stmt, 7, credits);
                        sqlite3_bind_text(course_stmt, 8, color.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(course_stmt, 9, year);
                        sqlite3_bind_text(course_stmt, 10, semester_name.c_str(), -1, SQLITE_TRANSIENT);
                        
                        sqlite3_step(course_stmt);
                    }
                }
            }
            sqlite3_finalize(course_stmt);
            sqlite3_finalize(semester_stmt);
            exec("COMMIT;");
            return true;
        } catch (const std::exception& e) {
            exec("ROLLBACK;");
            std::cerr << "DB Error in save_semester_grades: " << e.what() << std::endl;
            return false;
        }
    }

    // ── Calendar Events — get for a specific date ──
    json get_events_for_date(const std::string& date) {
        json result;

        // Compute display info from date string (YYYY-MM-DD)
        int year, month, day;
        sscanf(date.c_str(), "%d-%d-%d", &year, &month, &day);

        // Month name
        static const char* month_names[] = {"", "January", "February", "March", "April",
            "May", "June", "July", "August", "September", "October", "November", "December"};
        result["month"] = std::string(month_names[month]) + " " + std::to_string(year);

        // Day-of-week
        struct tm tm_date = {};
        tm_date.tm_year = year - 1900;
        tm_date.tm_mon = month - 1;
        tm_date.tm_mday = day;
        mktime(&tm_date);
        static const char* day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char* month_abbr[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

        result["selected_date"] = std::string(day_names[tm_date.tm_wday]) + ", " +
                                  month_abbr[month] + " " + std::to_string(day);

        // Week number (ISO)
        int yday = tm_date.tm_yday;
        int wday = tm_date.tm_wday == 0 ? 7 : tm_date.tm_wday;
        int week_num = (yday + 7 - wday) / 7 + 1;
        result["week"] = "W" + std::to_string(week_num);

        result["events"] = json::array();

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT id, time, type, title, location, duration, description, attendees, done "
            "FROM events WHERE date = ? ORDER BY time", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json ev = {
                {"id", sqlite3_column_int(stmt, 0)},
                {"time", (const char*)sqlite3_column_text(stmt, 1)},
                {"type", (const char*)sqlite3_column_text(stmt, 2)},
                {"title", (const char*)sqlite3_column_text(stmt, 3)},
                {"location", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""},
                {"duration", sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : ""},
                {"done", sqlite3_column_int(stmt, 8) != 0},
                {"source", "local"}
            };
            auto desc = sqlite3_column_text(stmt, 6);
            if (desc && strlen((const char*)desc) > 0) ev["description"] = (const char*)desc;
            int attendees = sqlite3_column_int(stmt, 7);
            if (attendees > 0) ev["attendees"] = attendees;
            result["events"].push_back(ev);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ── Legacy get_events (returns all events) ──
    json get_events() {
        // Return today's date events by default
        time_t now = time(nullptr);
        struct tm* local = localtime(&now);
        char date_buf[16];
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", local);
        return get_events_for_date(date_buf);
    }

    // ── Get dates that have events in a month (for calendar dots) ──
    json get_event_dates_in_month(int year, int month) {
        json result = json::array();
        char start[16], end[16];
        snprintf(start, sizeof(start), "%04d-%02d-01", year, month);
        snprintf(end, sizeof(end), "%04d-%02d-31", year, month);

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT DISTINCT date FROM events WHERE date >= ? AND date <= ? ORDER BY date",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, start, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, end, -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back((const char*)sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ── Add event ──
    int add_event(const std::string& date, const std::string& time, const std::string& type,
                  const std::string& title, const std::string& location = "",
                  const std::string& duration = "", const std::string& description = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "INSERT INTO events (date, time, type, title, location, duration, description) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, location.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, duration.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (int)sqlite3_last_insert_rowid(db_);
    }

    // ── Update event ──
    bool update_event(int id, const std::string& date, const std::string& time,
                      const std::string& type, const std::string& title,
                      const std::string& location = "", const std::string& duration = "",
                      const std::string& description = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "UPDATE events SET date=?, time=?, type=?, title=?, location=?, duration=?, description=? "
            "WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, location.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, duration.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, id);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    // ── Delete event ──
    bool delete_event(int id) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "DELETE FROM events WHERE id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    // ── Toggle event done ──
    bool toggle_event(int id) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "UPDATE events SET done = NOT done WHERE id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    // ── Alerts ──
    json get_alerts() {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, "SELECT severity, category, title, message, time, color FROM alerts ORDER BY created_at DESC", -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"severity", (const char*)sqlite3_column_text(stmt, 0)},
                {"category", (const char*)sqlite3_column_text(stmt, 1)},
                {"title", (const char*)sqlite3_column_text(stmt, 2)},
                {"message", sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : ""},
                {"time", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""},
                {"color", sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "primary"}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ── Add alert ──
    void add_alert(const std::string& severity, const std::string& category,
                   const std::string& title, const std::string& message,
                   const std::string& color = "primary") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "INSERT INTO alerts (severity, category, title, message, time, color) VALUES (?, ?, ?, ?, strftime('%H:%M', 'now', 'localtime'), ?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, severity.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, color.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // ============================================================
    // Notice Board Methods
    // ============================================================

    // Try to insert a notice; returns true if new, false if duplicate (url UNIQUE)
    bool add_notice(const std::string& source, const std::string& title,
                    const std::string& url, const std::string& date = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO notices (source, title, url, date) VALUES (?, ?, ?, ?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return sqlite3_changes(db_) > 0;
    }

    // Get notices not yet processed by LLM
    json get_unprocessed_notices() {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT id, source, title, url, date FROM notices WHERE processed = 0 ORDER BY id",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"source", (const char*)sqlite3_column_text(stmt, 1)},
                {"title", (const char*)sqlite3_column_text(stmt, 2)},
                {"url", (const char*)sqlite3_column_text(stmt, 3)},
                {"date", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // Mark a notice as processed with relevance score and AI summary
    void mark_notice_processed(int id, int score, const std::string& summary) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "UPDATE notices SET processed = 1, relevance_score = ?, ai_summary = ? WHERE id = ?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, score);
        sqlite3_bind_text(stmt, 2, summary.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Get all notices (for /api/notices)
    json get_notices(int limit = 50) {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT id, source, title, url, date, relevance_score, ai_summary, processed, created_at "
            "FROM notices ORDER BY created_at DESC LIMIT ?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"source", (const char*)sqlite3_column_text(stmt, 1)},
                {"title", (const char*)sqlite3_column_text(stmt, 2)},
                {"url", (const char*)sqlite3_column_text(stmt, 3)},
                {"date", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""},
                {"relevance_score", sqlite3_column_int(stmt, 5)},
                {"ai_summary", sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : ""},
                {"processed", sqlite3_column_int(stmt, 7) != 0},
                {"created_at", sqlite3_column_text(stmt, 8) ? (const char*)sqlite3_column_text(stmt, 8) : ""}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ============================================================
    // LMS Course Methods
    // ============================================================

    int upsert_lms_course(const std::string& lms_id, const std::string& name,
                          const std::string& professor = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO lms_courses (lms_id, name, professor) VALUES (?, ?, ?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, lms_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, professor.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (sqlite3_changes(db_) == 0) {
            sqlite3_prepare_v2(db_,
                "UPDATE lms_courses SET name = ?, professor = ? WHERE lms_id = ?",
                -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, professor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, lms_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        sqlite3_prepare_v2(db_, "SELECT id FROM lms_courses WHERE lms_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, lms_id.c_str(), -1, SQLITE_TRANSIENT);
        int course_id = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            course_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return course_id;
    }

    json get_lms_courses() {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT id, lms_id, name, professor FROM lms_courses ORDER BY name",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"lms_id", (const char*)sqlite3_column_text(stmt, 1)},
                {"name", (const char*)sqlite3_column_text(stmt, 2)},
                {"professor", sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : ""}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    // ============================================================
    // LMS Deadline Methods
    // ============================================================

    void upsert_lms_deadline(int course_id, const std::string& type,
                             const std::string& title,
                             const std::string& start_time = "",
                             const std::string& end_time = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT id FROM lms_deadlines WHERE course_id = ? AND type = ? AND title = ?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, course_id);
        sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db_,
                "UPDATE lms_deadlines SET start_time = ?, end_time = ? WHERE id = ?",
                -1, &stmt, nullptr);
            sqlite3_bind_text(stmt, 1, start_time.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } else {
            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db_,
                "INSERT INTO lms_deadlines (course_id, type, title, start_time, end_time) "
                "VALUES (?, ?, ?, ?, ?)",
                -1, &stmt, nullptr);
            sqlite3_bind_int(stmt, 1, course_id);
            sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, start_time.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, end_time.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    json get_lms_deadlines() {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT d.id, c.name AS course, d.type, d.title, d.start_time, d.end_time, "
            "d.synced_to_gcal, d.gcal_event_id "
            "FROM lms_deadlines d LEFT JOIN lms_courses c ON d.course_id = c.id "
            "ORDER BY d.end_time",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"course", sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : ""},
                {"type", (const char*)sqlite3_column_text(stmt, 2)},
                {"title", (const char*)sqlite3_column_text(stmt, 3)},
                {"start_time", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""},
                {"end_time", sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : ""},
                {"synced", sqlite3_column_int(stmt, 6) != 0},
                {"gcal_event_id", sqlite3_column_text(stmt, 7) ? (const char*)sqlite3_column_text(stmt, 7) : ""}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    json get_unsynced_deadlines() {
        json result = json::array();
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "SELECT d.id, c.name AS course, d.type, d.title, d.start_time, d.end_time "
            "FROM lms_deadlines d LEFT JOIN lms_courses c ON d.course_id = c.id "
            "WHERE d.synced_to_gcal = 0 ORDER BY d.end_time",
            -1, &stmt, nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"course", sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : ""},
                {"type", (const char*)sqlite3_column_text(stmt, 2)},
                {"title", (const char*)sqlite3_column_text(stmt, 3)},
                {"start_time", sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : ""},
                {"end_time", sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : ""}
            });
        }
        sqlite3_finalize(stmt);
        return result;
    }

    void mark_deadline_synced(int id, const std::string& gcal_event_id = "") {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_,
            "UPDATE lms_deadlines SET synced_to_gcal = 1, gcal_event_id = ? WHERE id = ?",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, gcal_event_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
};
