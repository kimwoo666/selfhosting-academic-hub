import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from grade_parser import (  # noqa: E402
    extract_courses_from_rows,
    extract_valid_semesters_from_rows,
    score_detail_rows,
    semester_sort_key,
)


class GradeParserTests(unittest.TestCase):
    def test_extract_valid_semesters_from_rows(self):
        rows = [
            ["", "2023", "Spring", "", "18", "", "4.12", "", "5/45"],
            ["", "2023", "Summer", "", "3", "", "0.00", "", ""],
            ["header"],
        ]

        semesters = extract_valid_semesters_from_rows(rows)

        self.assertEqual(len(semesters), 2)
        self.assertEqual(semesters[0]["year"], 2023)
        self.assertEqual(semesters[0]["semester"], "Spring")
        self.assertEqual(semesters[0]["earned_credits"], 18.0)
        self.assertEqual(semesters[0]["gpa"], 4.12)
        self.assertEqual(semesters[0]["rank"], "5/45")

    def test_extract_courses_from_header_rows(self):
        rows = [
            ["grade", "coursename", "credits", "professor", "coursecode"],
            ["A+", "Sample Course A", "3", "Instructor A", "TEST101"],
            ["B0", "Sample Course B", "3", "Instructor B", "TEST102"],
        ]

        courses = extract_courses_from_rows(rows)

        self.assertEqual(len(courses), 2)
        self.assertEqual(courses[0]["code"], "TEST101")
        self.assertEqual(courses[0]["name"], "Sample Course A")
        self.assertEqual(courses[0]["credits"], 3.0)
        self.assertEqual(courses[0]["grade"], "A+")
        self.assertEqual(courses[0]["professor"], "Instructor A")

    def test_extract_courses_without_header_uses_fallback(self):
        rows = [
            ["TEST201", "Practice Course 2", "Instructor C", "3", "A0"],
            ["TEST202", "Intro Course", "Instructor D", "3", "B+"],
        ]

        courses = extract_courses_from_rows(rows)

        self.assertEqual(len(courses), 2)
        self.assertEqual(courses[1]["code"], "TEST202")
        self.assertEqual(courses[1]["name"], "Intro Course")
        self.assertEqual(courses[1]["professor"], "Instructor D")

    def test_score_detail_rows_with_leading_blank_column(self):
        rows = [
            ["grade", "coursename", "credits", "professor", "coursecode"],
            ["", "A+", "Sample Course A", "3", "Instructor A", "TEST101"],
        ]

        score = score_detail_rows(rows)
        courses = extract_courses_from_rows(rows)

        self.assertGreaterEqual(score, 6)
        self.assertEqual(len(courses), 1)
        self.assertEqual(courses[0]["code"], "TEST101")

    def test_semester_sort_key_orders_latest_semester_first(self):
        semesters = [
            {"year": 2023, "semester": "Spring"},
            {"year": 2024, "semester": "Summer"},
            {"year": 2023, "semester": "Fall"},
        ]

        ordered = sorted(semesters, key=semester_sort_key, reverse=True)

        self.assertEqual(ordered[0]["year"], 2024)
        self.assertEqual(ordered[0]["semester"], "Summer")
        self.assertEqual(ordered[1]["semester"], "Fall")
        self.assertEqual(ordered[2]["semester"], "Spring")


if __name__ == "__main__":
    unittest.main()
