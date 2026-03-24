
import sys
import json
import time
import traceback
import re
import os
import subprocess
import threading
from pathlib import Path
from grade_parser import (
    clean_text,
    extract_courses_from_rows,
    extract_valid_semesters_from_rows,
    looks_like_grade,
    looks_like_year,
    parse_numeric_text,
    score_detail_rows,
    semester_sort_key,
)
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.common.keys import Keys

def log_debug(msg):
    """Log to stderr to avoid polluting stdout which is reserved for JSON result."""
    timestamp = time.strftime("%H:%M:%S")
    sys.stderr.write(f"[{timestamp}] DEBUG: {msg}\n")
    sys.stderr.flush()

def dump_visible_tables(driver, output_dir, label):
    if not output_dir:
        return

    safe_label = re.sub(r'[^A-Za-z0-9._-]+', '_', label).strip('_') or 'tables'
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    table_dump = []
    for t_idx, table in enumerate(driver.find_elements(By.TAG_NAME, "table")):
        table_dump.append(f"=== TABLE {t_idx} ===")
        for r_idx, d_row in enumerate(table.find_elements(By.TAG_NAME, "tr")[:40]):
            d_cols = d_row.find_elements(By.TAG_NAME, "td")
            if not d_cols:
                continue
            values = [clean_text(col.text).replace("\n", " / ")[:180] for col in d_cols]
            table_dump.append(f"{r_idx}: td={len(d_cols)} :: " + " | ".join(values))

    (out_dir / f"{safe_label}.txt").write_text("\n".join(table_dump), encoding="utf-8")

def write_debug_artifact(output_dir, label, suffix, content):
    if not output_dir:
        return

    safe_label = re.sub(r'[^A-Za-z0-9._-]+', '_', label).strip('_') or 'debug'
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{safe_label}.{suffix}").write_text(content, encoding="utf-8")

def dump_visible_controls(driver, output_dir, label):
    if not output_dir:
        return

    controls = []
    selectors = [
        "button",
        "a",
        "input",
        "select",
        "option",
        "[onclick]",
    ]
    seen = set()
    for selector in selectors:
        for idx, element in enumerate(driver.find_elements(By.CSS_SELECTOR, selector)):
            try:
                tag = element.tag_name
                text = clean_text(element.text or element.get_attribute("value") or element.get_attribute("alt"))
                element_id = clean_text(element.get_attribute("id"))
                name = clean_text(element.get_attribute("name"))
                onclick = clean_text(element.get_attribute("onclick"))
                title = clean_text(element.get_attribute("title"))
                if not any([text, element_id, name, onclick, title]):
                    continue
                key = (tag, text, element_id, name, onclick, title)
                if key in seen:
                    continue
                seen.add(key)
                controls.append(
                    f"{idx}: tag={tag} id={element_id} name={name} title={title} text={text} onclick={onclick}"
                )
            except Exception:
                continue

    write_debug_artifact(output_dir, label, "controls.txt", "\n".join(controls))

def dump_debug_snapshot(driver, output_dir, label):
    if not output_dir:
        return

    safe_label = re.sub(r'[^A-Za-z0-9._-]+', '_', label).strip('_') or 'snapshot'
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        driver.save_screenshot(str(out_dir / f"{safe_label}.png"))
    except Exception:
        pass
    try:
        (out_dir / f"{safe_label}.html").write_text(driver.page_source, encoding="utf-8")
    except Exception:
        pass

def debug_failure(driver, error_msg):
    timestamp = int(time.time())
    log_debug(f"FAILURE: {error_msg}")
    try:
        log_debug(f"Current URL: {driver.current_url}")
        log_debug(f"Title: {driver.title}")
        
        # Screenshot
        filename = f"debug_screenshot_{timestamp}.png"
        driver.save_screenshot(filename)
        log_debug(f"Saved screenshot to {filename}")
        
        # HTML Source (Truncated)
        html = driver.page_source
        with open(f"debug_page_{timestamp}.html", "w", encoding="utf-8") as f:
            f.write(html)
        log_debug(f"Saved HTML to debug_page_{timestamp}.html")
        
    except Exception as e:
        log_debug(f"Failed to capture debug info: {e}")

def shutdown_driver(driver, timeout=5):
    if not driver:
        return

    service_process = None
    try:
        service_process = getattr(getattr(driver, "service", None), "process", None)
    except Exception:
        service_process = None

    quit_thread = threading.Thread(target=lambda: driver.quit(), daemon=True)
    quit_thread.start()
    quit_thread.join(timeout)
    if not quit_thread.is_alive():
        return

    log_debug("driver.quit() timed out, force-killing webdriver process tree.")
    pid = getattr(service_process, "pid", None)
    if not pid:
        return

    try:
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/PID", str(pid), "/T", "/F"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        else:
            service_process.kill()
    except Exception as e:
        log_debug(f"Failed to force-stop webdriver: {e}")

def js_click_text_debug(driver, text):
    # Only click visible elements
    result = driver.execute_script(f"""
        var text = '{text}';
        var els = document.querySelectorAll('a, span, div, li, td, button');
        var matches = [];
        for (var i=0; i<els.length; i++) {{
            // Check exact or partial match
            if (els[i].textContent.includes(text) && els[i].offsetParent !== null) {{
                matches.push(els[i]);
            }}
        }}
        matches.sort(function(a, b) {{ return a.textContent.length - b.textContent.length; }});
        if (matches.length > 0) {{
            console.log("Clicking: " + matches[0].textContent);
            matches[0].click();
            return true;
        }}
        return false;
    """)
    return result

def js_click_control_text(driver, text):
    result = driver.execute_script("""
        var text = arguments[0];
        var selectors = 'div[ct="B"], [role="button"], button, input[type="button"], input[type="submit"]';
        var els = Array.from(document.querySelectorAll(selectors));
        var matches = [];
        for (var i = 0; i < els.length; i++) {
            var candidate = (els[i].textContent || els[i].value || els[i].alt || '').trim();
            if (candidate.includes(text) && els[i].offsetParent !== null) {
                matches.push(els[i]);
            }
        }
        matches.sort(function(a, b) {
            var aText = (a.textContent || a.value || a.alt || '').trim();
            var bText = (b.textContent || b.value || b.alt || '').trim();
            return aText.length - bText.length;
        });
        if (matches.length > 0) {
            matches[0].click();
            return true;
        }
        return false;
    """, text)
    return result

def main():
    if len(sys.argv) < 3:
        print(json.dumps({"success": False, "message": "Usage: python crawl_grades.py <id> <pw>"}))
        sys.exit(1)

    student_id = sys.argv[1]
    password = sys.argv[2]

    log_debug("Initializing Chrome Driver (Robot Mode)...")
    options = Options()
    
    # Use standard headless mode for stability
    options.add_argument("--headless") 
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    options.add_argument("--disable-gpu")
    options.add_argument("--window-size=1920,1080")
    options.add_argument("--lang=ko_KR") # Force Korean locale
    options.add_argument("--log-level=3")
    
    driver = None
    try:
        # standard chromedriver path or auto-detect
        try:
            service = Service("/usr/bin/chromedriver")
            driver = webdriver.Chrome(service=service, options=options)
        except:
             driver = webdriver.Chrome(options=options)
        
        driver.set_page_load_timeout(60)
        wait = WebDriverWait(driver, 20)
        
        # 1. Login
        log_debug("Navigating to login page...")
        driver.get("https://saint.ssu.ac.kr/irj/portal")
        
        # Handle landing page "Login" button if present
        try:
            WebDriverWait(driver, 5).until(EC.presence_of_element_located((By.NAME, "j_username")))
        except:
            log_debug("Login form not found immediately, looking for generic login button...")
            login_selectors = [
               "//*[@id='s_btnLogin']",
               "//img[contains(@src, 'login')]",
               "//a[contains(@href, 'login')]"
            ]
            for selector in login_selectors:
                try:
                    btn = driver.find_element(By.XPATH, selector)
                    if btn.is_displayed():
                        btn.click()
                        time.sleep(2)
                        break
                except: continue

        # Perform Login
        try:
            log_debug("Entering credentials...")
            
            # Find username field
            username_input = None
            for selector in ["j_username", "userid"]:
                try:
                    username_input = driver.find_element(By.NAME, selector)
                    if username_input: break
                except: k = driver.find_elements(By.ID, selector); username_input = k[0] if k else None;

            if not username_input: raise Exception("Could not find username input")

            username_input.clear()
            username_input.send_keys(student_id)

            # Find password field
            password_input = None
            for selector in ["j_password", "pwd"]:
                try:
                    password_input = driver.find_element(By.NAME, selector)
                    if password_input: break
                except: k = driver.find_elements(By.ID, selector); password_input = k[0] if k else None;

            if not password_input: raise Exception("Could not find password input")
                 
            password_input.clear()
            password_input.send_keys(password)
            password_input.send_keys(Keys.RETURN)
            
            time.sleep(5) 
                
        except Exception as e:
            # Check if already logged in
            try:
                 if len(driver.find_elements(By.XPATH, "//*[contains(@href, 'logoff')]")) > 0:
                     log_debug("Already logged in.")
                 else:
                     raise e
            except:
                 debug_failure(driver, "Login Failed")
                 print(json.dumps({"success": False, "message": f"Login failed: {str(e)}"}))
                 return
            
        # 2. Popups & Language
        try:
            wait.until(EC.presence_of_element_located((By.XPATH, "//*[contains(@href, 'logoff')]")))
        except: pass

        try:
            if EC.alert_is_present()(driver):
                driver.switch_to.alert.accept()
        except: pass

        # Close popups (generic)
        try:
            close_btns = driver.find_elements(By.XPATH, "//*[contains(text(), '닫기') or contains(text(), 'Close')]")
            for btn in close_btns:
                if btn.is_displayed():
                    try: btn.click(); time.sleep(0.5);
                    except: pass
        except: pass

        # 3. Navigate to Grades
        log_debug("Navigating to Grade menu...")
        try:
            driver.switch_to.default_content()
            
            time.sleep(2)
            # Try finding generic top menu items via JS click (Academics)
            if not js_click_text_debug(driver, "학사관리"):
                if not js_click_text_debug(driver, "Student Information"):
                    # Structural fallback: 3rd Main Menu Item?
                    # Too risky without specific IDs. Assume text fix works with Headless Standard.
                    # If failed, try finding images with alt text?
                    pass

            time.sleep(2)
            if not js_click_text_debug(driver, "성적/졸업"):
                js_click_text_debug(driver, "Grade/Graduation")
            
            time.sleep(2)
            if not js_click_text_debug(driver, "학기별성적조회"):
                js_click_text_debug(driver, "Semester Grade")
            
        except Exception as e:
            debug_failure(driver, str(e))
            print(json.dumps({"success": False, "message": f"Navigation failed: {str(e)}"}))
            return

        # 4. Access Grade Frame
        log_debug("Waiting for grade content frame...")
        time.sleep(5)
        
        found_frame = False
        def switch_to_content_frame():
            driver.switch_to.default_content()
            frames = driver.find_elements(By.TAG_NAME, "iframe")
            for frame in frames:
                try:
                    driver.switch_to.frame(frame)
                    if len(driver.find_elements(By.XPATH, "//*[contains(text(), '평점') or contains(text(), 'GPA')]")) > 0:
                        return True
                    # Nested check
                    subframes = driver.find_elements(By.TAG_NAME, "iframe")
                    for sub in subframes:
                        driver.switch_to.frame(sub)
                        if len(driver.find_elements(By.XPATH, "//*[contains(text(), '평점') or contains(text(), 'GPA')]")) > 0:
                            return True
                        driver.switch_to.parent_frame()
                    driver.switch_to.default_content()
                except:
                    driver.switch_to.default_content()
            return False

        if not switch_to_content_frame():
             time.sleep(3)
             if not switch_to_content_frame():
                 debug_failure(driver, "Content frame not found")
                 print(json.dumps({"success": False, "message": "Grade content frame not found"}))
                 return

        # 5. Extract Semesters
        log_debug("Extracting available semesters...")
        all_semesters_data = []
        
        try:
            def find_summary_table():
                best_table = None
                best_score = 0
                for tbl in driver.find_elements(By.TAG_NAME, "table"):
                    table_rows = []
                    for row in tbl.find_elements(By.TAG_NAME, "tr"):
                        values = [clean_text(col.text) for col in row.find_elements(By.TAG_NAME, "td")]
                        if values:
                            table_rows.append(values)
                    score = len(extract_valid_semesters_from_rows(table_rows))
                    if score > best_score:
                        best_score = score
                        best_table = tbl
                return best_table, best_score

            def find_detail_table():
                best_table = None
                best_score = 0
                for tbl in driver.find_elements(By.TAG_NAME, "table"):
                    header_score = 0
                    score = 0
                    for row in tbl.find_elements(By.TAG_NAME, "tr"):
                        values = [clean_text(col.text) for col in row.find_elements(By.TAG_NAME, "td")]
                        if len(values) < 6:
                            continue
                        if (
                            "과목명" in values
                            and "과목코드" in values
                            and "과목학점" in values
                            and "등급" in values
                        ):
                            header_score = 5
                            continue
                        has_grade = any(looks_like_grade(value) for value in values)
                        has_credit = any(
                            (parse_numeric_text(value) is not None and 0 < parse_numeric_text(value) <= 6.0)
                            for value in values
                        )
                        has_code = any(re.fullmatch(r"[A-Za-z0-9-]{4,}", value) for value in values if value)
                        if has_grade and has_credit and has_code:
                            score += 1
                    total_score = header_score + score
                    if total_score > best_score:
                        best_score = total_score
                        best_table = tbl
                return best_table, best_score

            summary_table, summary_score = find_summary_table()
            if not summary_table or summary_score == 0:
                raise Exception("Could not find summary table structurally.")

            rows = summary_table.find_elements(By.TAG_NAME, "tr")
            summary_rows = []
            for row in rows:
                values = [clean_text(col.text) for col in row.find_elements(By.TAG_NAME, "td")]
                if values:
                    summary_rows.append(values)
            valid_semesters = extract_valid_semesters_from_rows(summary_rows)

            log_debug(f"Found {len(valid_semesters)} semester rows.")

            for semester_target in valid_semesters:
                try:
                    summary_table, _ = find_summary_table()
                    if not summary_table:
                        continue

                    row = None
                    cols = None
                    year = str(semester_target["year"])
                    sem = semester_target["semester"]
                    earned = semester_target["earned_credits"]
                    gpa = semester_target["gpa"]

                    for candidate_row in summary_table.find_elements(By.TAG_NAME, "tr"):
                        candidate_cols = candidate_row.find_elements(By.TAG_NAME, "td")
                        if len(candidate_cols) < 9:
                            continue
                        candidate_year = clean_text(candidate_cols[1].text)
                        candidate_sem = clean_text(candidate_cols[2].text)
                        if candidate_year == year and candidate_sem == sem:
                            row = candidate_row
                            cols = candidate_cols
                            break

                    if row is None or cols is None:
                        log_debug(f"Could not refind summary row for {year}-{sem}.")
                        continue

                    rank = clean_text(cols[8].text)

                    if not looks_like_year(year) or not sem or earned is None or gpa is None:
                        continue

                    sem_data = {
                        "year": int(year),
                        "semester": sem,
                        "earned_credits": earned,
                        "gpa": gpa,
                        "rank": rank,
                        "courses": []
                    }

                    log_debug(f"Processing {year}-{sem}...")
                    debug_dir = os.environ.get("JARVIS_GRADE_DEBUG_DIR", "")
                    write_debug_artifact(debug_dir, f"{year}_{sem}_summary_row", "html", row.get_attribute("outerHTML"))
                    dump_visible_controls(driver, debug_dir, f"{year}_{sem}_before")

                    try:
                        cols[0].click()
                    except:
                        try:
                            cols[0].find_element(By.TAG_NAME, "img").click()
                        except:
                            pass

                    time.sleep(1)
                    clicked_refresh = js_click_control_text(driver, "조회(새로고침)")
                    if not clicked_refresh:
                        clicked_refresh = js_click_control_text(driver, "새로고침")
                    log_debug(f"Refresh button clicked for {year}-{sem}: {clicked_refresh}")

                    time.sleep(3)
                    dump_visible_tables(driver, debug_dir, f"{year}_{sem}_tables")
                    dump_visible_controls(driver, debug_dir, f"{year}_{sem}_after")
                    dump_debug_snapshot(driver, debug_dir, f"{year}_{sem}_after")

                    log_debug(f"Scanning detail table for {year}-{sem}...")
                    detail_table, detail_score = find_detail_table()
                    log_debug(f"Detail table score for {year}-{sem}: {detail_score}")
                    if detail_table and detail_score > 0:
                        write_debug_artifact(debug_dir, f"{year}_{sem}_detail_table", "html", detail_table.get_attribute("outerHTML"))
                        d_rows = detail_table.find_elements(By.TAG_NAME, "tr")
                        header_map = {}
                        for d_row in d_rows:
                            values = [clean_text(col.text) for col in d_row.find_elements(By.TAG_NAME, "td")]
                            if not values:
                                continue
                            if (
                                len(values) == 8
                                and values[:8] == ["성적", "등급", "과목명", "상세성적", "과목학점", "교수명", "비고", "과목코드"]
                            ):
                                header_map = {value: idx for idx, value in enumerate(values)}
                                break

                        code_idx = header_map.get("과목코드")
                        name_idx = header_map.get("과목명")
                        credits_idx = header_map.get("과목학점")
                        grade_idx = header_map.get("등급")
                        prof_idx = header_map.get("교수명")
                        seen_courses = set()

                        for d_row in d_rows:
                            d_cols = d_row.find_elements(By.TAG_NAME, "td")
                            if len(d_cols) < 6:
                                continue

                            values = [clean_text(col.text) for col in d_cols]
                            if not values or len(values) > 10:
                                continue
                            if values == list(header_map.keys()):
                                continue
                            if any(value in {"성적", "등급", "과목명", "상세성적", "과목학점", "교수명", "비고", "과목코드"} for value in values):
                                continue

                            offset = 0
                            if header_map and len(values) == len(header_map) + 1 and values[0] == "":
                                offset = 1

                            code = values[offset + code_idx] if code_idx is not None and len(values) > offset + code_idx else ""
                            name = values[offset + name_idx] if name_idx is not None and len(values) > offset + name_idx else ""
                            credits_text = values[offset + credits_idx] if credits_idx is not None and len(values) > offset + credits_idx else ""
                            grade = values[offset + grade_idx] if grade_idx is not None and len(values) > offset + grade_idx else ""
                            prof = values[offset + prof_idx] if prof_idx is not None and len(values) > offset + prof_idx else ""
                            credits_value = parse_numeric_text(credits_text)

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
                                prof = next((value for value in name_candidates if value != name and len(value) <= 30), "")

                            if not code or not name or credits_value is None or not looks_like_grade(grade):
                                continue
                            if not re.fullmatch(r"[A-Za-z0-9-]{4,}", code):
                                continue

                            course_key = (code, name, credits_value, grade, prof)
                            if course_key in seen_courses:
                                continue
                            seen_courses.add(course_key)

                            sem_data["courses"].append({
                                "code": code,
                                "name": name,
                                "credits": credits_value,
                                "grade": grade,
                                "professor": prof
                            })
                    else:
                        log_debug(f"No detail rows detected for {year}-{sem}.")

                    all_semesters_data.append(sem_data)
                    log_debug(f"Collected {len(sem_data['courses'])} courses for {year}-{sem}.")

                except Exception as semester_error:
                    log_debug(f"Semester crawl failed for {semester_target.get('year')}-{semester_target.get('semester')}: {semester_error}")
                    continue

        except Exception as e:
            debug_failure(driver, "Failed to scrape tables")
            print(json.dumps({"success": False, "message": f"Crawling error: {str(e)}"}))
            return

        all_semesters_data = sorted(all_semesters_data, key=semester_sort_key, reverse=True)
        latest_sem = all_semesters_data[0] if all_semesters_data else None
        if not latest_sem:
            print(json.dumps({
                "success": False,
                "status": "needs_login",
                "message": "No semester grade data returned from u-SAINT",
                "semesters_data": []
            }))
            return

        result = {
            "success": True,
            "gpa": round(float(latest_sem.get("gpa", 0.0)), 2),
            "credits": round(float(latest_sem.get("earned_credits", 0.0)), 2),
            "rank": latest_sem.get("rank", ""),
            "semester": f"{latest_sem.get('year', 0)} {latest_sem.get('semester', '')}".strip(),
            "semesters_data": all_semesters_data
        }

        print(json.dumps(result))

    except Exception as e:
        log_debug(f"Unexpected global error: {e}")
        traceback.print_exc()
        print(json.dumps({"success": False, "message": f"Unexpected error: {str(e)}"}))
    
    finally:
        shutdown_driver(driver)

if __name__ == "__main__":
    main()
