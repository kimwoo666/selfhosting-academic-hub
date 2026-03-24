import re

GRADE_PATTERN = re.compile(r"^(A\+|A0|A-|B\+|B0|B-|C\+|C0|C-|D\+|D0|D-|F|P|NP)$")

HEADER_ALIASES = {
    "code": {"\uacfc\ubaa9\ucf54\ub4dc", "coursecode", "code"},
    "name": {"\uacfc\ubaa9\uba85", "coursename", "course", "name"},
    "credits": {"\uacfc\ubaa9\ud559\uc810", "\ud559\uc810", "credits", "credit"},
    "grade": {"\ub4f1\uae09", "\uc131\uc801", "grade"},
    "professor": {"\uad50\uc218\uba85", "\uad50\uc218", "professor", "instructor"},
}


def clean_text(text):
    if not text:
        return ""
    return str(text).strip()


def parse_numeric_text(text):
    normalized = clean_text(text).replace(",", "")
    if not normalized or normalized.count(".") > 1:
        return None
    if normalized.replace(".", "", 1).isdigit():
        return float(normalized)
    return None


def looks_like_year(text):
    normalized = clean_text(text)
    return len(normalized) == 4 and normalized.isdigit()


def looks_like_grade(text):
    return bool(GRADE_PATTERN.fullmatch(clean_text(text)))


def semester_sort_key(semester_data):
    semester_text = clean_text(str(semester_data.get("semester", "")))
    lowered = semester_text.lower()
    order = 9
    if "1" in semester_text or "spring" in lowered or "\ubd04" in semester_text:
        order = 1
    elif "\uc5ec\ub984" in semester_text or "summer" in lowered:
        order = 2
    elif "2" in semester_text or "fall" in lowered or "autumn" in lowered or "\uac00\uc744" in semester_text:
        order = 3
    elif "\uaca8\uc6b8" in semester_text or "winter" in lowered:
        order = 4
    return (int(semester_data.get("year", 0)), order, semester_text)


def extract_valid_semesters_from_rows(rows):
    valid_semesters = []
    for row in rows:
        values = [clean_text(value) for value in row]
        if len(values) < 9:
            continue
        year = values[1]
        semester = values[2]
        earned = parse_numeric_text(values[4])
        gpa = parse_numeric_text(values[6])
        rank = values[8]
        if not looks_like_year(year) or not semester or earned is None or gpa is None:
            continue
        valid_semesters.append(
            {
                "year": int(year),
                "semester": semester,
                "earned_credits": earned,
                "gpa": gpa,
                "rank": rank,
            }
        )
    return valid_semesters


def _normalize_header_label(value):
    return clean_text(value).lower().replace(" ", "")


def _build_header_map(values):
    header_map = {}
    normalized_values = [_normalize_header_label(value) for value in values]
    for idx, normalized in enumerate(normalized_values):
        for key, aliases in HEADER_ALIASES.items():
            if normalized in aliases and key not in header_map:
                header_map[key] = idx
    return header_map


def score_detail_rows(rows):
    header_bonus = 0
    for row in rows:
        header_map = _build_header_map(row)
        if {"code", "name", "credits", "grade"}.issubset(header_map):
            header_bonus = 5
            break
    return header_bonus + len(extract_courses_from_rows(rows))


def extract_courses_from_rows(rows):
    header_map = {}
    header_values = None
    for row in rows:
        candidate_map = _build_header_map(row)
        if {"code", "name", "credits", "grade"}.issubset(candidate_map):
            header_map = candidate_map
            header_values = {_normalize_header_label(value) for value in row}
            break

    courses = []
    seen_courses = set()
    for row in rows:
        values = [clean_text(value) for value in row]
        if len(values) < 4 or len(values) > 10 or not any(values):
            continue

        if header_values and {_normalize_header_label(value) for value in values} == header_values:
            continue

        offset = 1 if header_map and len(values) == len(header_map) + 1 and values[0] == "" else 0
        code = ""
        name = ""
        grade = ""
        professor = ""
        credits_value = None

        if header_map:
            def get_value(key):
                idx = header_map.get(key)
                if idx is None:
                    return ""
                actual_idx = offset + idx
                return values[actual_idx] if actual_idx < len(values) else ""

            code = get_value("code")
            name = get_value("name")
            grade = get_value("grade")
            professor = get_value("professor")
            credits_value = parse_numeric_text(get_value("credits"))

        if not (code and name and credits_value is not None and looks_like_grade(grade)):
            grade = next((value for value in values if looks_like_grade(value)), "")
            credits_value = None
            for value in values:
                parsed = parse_numeric_text(value)
                if parsed is not None and 0 < parsed <= 6.0:
                    credits_value = parsed
                    break
            code = next((value for value in values if re.fullmatch(r"[A-Za-z0-9-]{4,}", value)), "")
            excluded = {code, grade}
            name_candidates = [
                value for value in values
                if value and value not in excluded and parse_numeric_text(value) is None and len(value) > 1
            ]
            name = max(name_candidates, key=len) if name_candidates else ""
            professor = next((value for value in name_candidates if value != name and len(value) <= 30), "")

        if not code or not name or credits_value is None or not looks_like_grade(grade):
            continue
        if not re.fullmatch(r"[A-Za-z0-9-]{4,}", code):
            continue

        course_key = (code, name, credits_value, grade, professor)
        if course_key in seen_courses:
            continue
        seen_courses.add(course_key)
        courses.append(
            {
                "code": code,
                "name": name,
                "credits": credits_value,
                "grade": grade,
                "professor": professor,
            }
        )

    return courses
