#include "database.h"

#include <cassert>
#include <filesystem>

int main() {
    namespace fs = std::filesystem;

    const fs::path temp_dir = fs::temp_directory_path() / "jarvis_database_smoke";
    const fs::path db_path = temp_dir / "smoke.db";
    fs::create_directories(temp_dir);
    fs::remove(db_path);

    {
        Database db(db_path.string());

        const bool summary_ok = db.update_academic_summary(4.12, 18.5, "5/45", "Spring 2023");
        assert(summary_ok);

        json semesters = json::array({
            {
                {"year", 2023},
                {"semester", "Spring"},
                {"gpa", 4.12},
                {"earned_credits", 18.5},
                {"total_credits", 18.5},
                {"rank", "5/45"},
                {"courses", json::array({
                    {
                        {"code", "TEST101"},
                        {"name", "Sample Course A"},
                        {"full_code", "TEST101-01"},
                        {"professor", "Instructor A"},
                        {"grade", "A+"},
                        {"score", 95.0},
                        {"credits", 3.0},
                        {"color", "emerald"}
                    },
                    {
                        {"code", "TEST102"},
                        {"name", "Sample Course B"},
                        {"full_code", "TEST102-01"},
                        {"professor", "Instructor B"},
                        {"grade", "A0"},
                        {"score", 91.0},
                        {"credits", 3.0},
                        {"color", "primary"}
                    }
                })}
            },
            {
                {"year", 2024},
                {"semester", "Summer"},
                {"gpa", 0.0},
                {"earned_credits", 3.0},
                {"total_credits", 3.0},
                {"rank", ""},
                {"courses", json::array({
                    {
                        {"code", "LAB201"},
                        {"name", "Practice Course"},
                        {"full_code", "LAB201-01"},
                        {"professor", "Instructor C"},
                        {"grade", "P"},
                        {"score", 0.0},
                        {"credits", 3.0},
                        {"color", "blue"}
                    }
                })}
            }
        });

        const bool grades_ok = db.save_semester_grades(semesters);
        assert(grades_ok);

        json grades = db.get_grades();
        assert(!grades.contains("status"));
        assert(grades["gpa"] == 4.12);
        assert(grades["credits"] == 18.5);
        assert(grades["rank"] == "5/45");
        assert(grades["courses"].is_array());
        assert(grades["courses"].size() == 3);
        assert(grades["semesters_data"].is_array());
        assert(grades["semesters_data"].size() == 2);

        const json& latest = grades["semesters_data"][0];
        assert(latest["year"] == 2024);
        assert(latest["semester"] == "Summer");
        assert(latest["courses"].size() == 1);
        assert(latest["courses"][0]["credits"] == 3.0);

        const json& regular = grades["semesters_data"][1];
        assert(regular["year"] == 2023);
        assert(regular["semester"] == "Spring");
        assert(regular["courses"].size() == 2);
        assert(regular["courses"][0]["score"] == 95.0 || regular["courses"][1]["score"] == 95.0);
    }

    fs::remove(db_path);
    fs::remove_all(temp_dir);
    return 0;
}
