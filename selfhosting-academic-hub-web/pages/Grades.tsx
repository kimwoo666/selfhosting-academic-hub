
import React, { useState, useEffect } from 'react';

// Enhanced Data Structures
interface DetailedScore {
    category: string;
    score: number;
    max_score: number;
}

interface Course {
    code: string;
    name: string;
    full_code: string;
    professor: string;
    grade: string;
    score: number;
    credits: number;
    rank: string;
    color: string;
    details?: DetailedScore[];
    year?: number;
    semester?: string;
}

interface SemesterData {
    year: number;
    semester: string;
    gpa: number;
    earned_credits: number;
    total_credits: number;
    rank: string;
    week?: string;
    courses: Course[];
    ai_forecast?: { projected_gpa: number; confidence: number; risk: string };
}

export const Grades: React.FC = () => {
    // State
    const [history, setHistory] = useState<SemesterData[]>([]);
    const [selectedSemesterIdx, setSelectedSemesterIdx] = useState<number>(0);
    const [selectedCourse, setSelectedCourse] = useState<Course | null>(null);

    const [loading, setLoading] = useState(true);
    const [syncing, setSyncing] = useState(false);
    const [syncStatus, setSyncStatus] = useState<'idle' | 'verifying' | 'crawling' | 'success' | 'error'>('idle');
    const [syncMsg, setSyncMsg] = useState('');
    const [needsLogin, setNeedsLogin] = useState(false);
    const [studentId, setStudentId] = useState('');
    const [password, setPassword] = useState('');

    const calcGpa = (grade: string): number => {
        const map: { [key: string]: number } = {
            'A+': 4.5, 'A0': 4.3, 'A-': 4.0,
            'B+': 3.5, 'B0': 3.3, 'B-': 3.0,
            'C+': 2.5, 'C0': 2.3, 'C-': 2.0,
            'D+': 1.5, 'D0': 1.3, 'D-': 1.0,
            'F': 0.0, 'P': 0.0, 'NP': 0.0
        };
        return map[grade] || 0.0;
    };

    const normalizeSemesterText = (value: string) => value.replace(/\s+/g, ' ').trim();

    const semesterOrder = (value: string) => {
        const normalized = normalizeSemesterText(value);
        const lowered = normalized.toLowerCase();
        if (normalized.startsWith('1') || normalized.includes('\uBD04') || lowered.includes('spring')) return 1;
        if (normalized.includes('\uC5EC\uB984') || lowered.includes('summer')) return 2;
        if (normalized.startsWith('2') || normalized.includes('\uAC00\uC744') || lowered.includes('fall') || lowered.includes('autumn')) return 3;
        if (normalized.includes('\uACA8\uC6B8') || lowered.includes('winter')) return 4;
        return 0;
    };

    const parseSummarySemester = (value: string) => {
        const normalized = normalizeSemesterText(value);
        if (!normalized) return null;

        const match = normalized.match(/^(\d{4})\s+(.*)$/);
        if (!match) {
            return { year: NaN, semester: normalized };
        }

        return {
            year: Number(match[1]),
            semester: normalizeSemesterText(match[2]),
        };
    };

    const gradeBarWidth = (course: Course) => {
        if (course.grade === 'P') return '100%';

        const percent = (calcGpa(course.grade) / 4.5) * 100;
        return `${Math.max(12, Math.min(100, percent)).toFixed(0)}%`;
    };

    const hasMeaningfulAiForecast = (forecast?: SemesterData['ai_forecast']) => {
        if (!forecast) return false;
        return forecast.projected_gpa > 0 || forecast.confidence > 0 || forecast.risk.trim().length > 0;
    };

    const fetchGrades = async () => {
        setLoading(true);
        setNeedsLogin(false);
        try {
            const res = await fetch('/api/academic/grades');
            if (!res.ok) {
                if (res.status === 401) setNeedsLogin(true);
                throw new Error("Failed to fetch");
            }
            const data = await res.json();
            const summarySemester = parseSummarySemester(data.semester || '');
            const rawSemesters = Array.isArray(data.semesters_data) ? data.semesters_data : [];
            const rawCourses = Array.isArray(data.courses) ? data.courses : [];

            if (data.status === 'needs_login' || (rawSemesters.length === 0 && rawCourses.length === 0)) {
                setNeedsLogin(true);
                setHistory([]);
                setLoading(false);
                return;
            }

            const mapCourse = (course: Partial<Course>): Course => ({
                code: course.code || '',
                name: course.name || '',
                full_code: course.full_code || course.code || '',
                professor: course.professor || '',
                grade: course.grade || '',
                score: typeof course.score === 'number' ? course.score : 0,
                credits: typeof course.credits === 'number'
                    ? course.credits
                    : (typeof course.score === 'number' ? course.score : 0),
                rank: course.rank || '--',
                color: course.color || 'primary',
                details: Array.isArray(course.details) ? course.details : undefined,
                year: course.year,
                semester: course.semester,
            });

            const sortSemesters = (left: SemesterData, right: SemesterData) => {
                if (right.year !== left.year) return right.year - left.year;
                return semesterOrder(right.semester) - semesterOrder(left.semester);
            };

            const buildComputedSemester = (year: number, semester: string, semesterCourses: Course[]): SemesterData => {
                let totalPts = 0;
                let totalCreds = 0;
                semesterCourses.forEach(course => {
                    const gradePoint = calcGpa(course.grade);
                    if (course.grade !== 'P' && course.grade !== 'NP') {
                        totalPts += gradePoint * course.credits;
                        totalCreds += course.credits;
                    }
                });

                const gpa = totalCreds > 0 ? (totalPts / totalCreds) : 0.0;
                const earnedCredits = semesterCourses.reduce((sum, course) => sum + (course.grade !== 'F' && course.grade !== 'NP' ? course.credits : 0), 0);
                const normalizedSemester = normalizeSemesterText(semester);
                const isSummarySemester = summarySemester !== null
                    && summarySemester.year === year
                    && summarySemester.semester === normalizedSemester;

                return {
                    year,
                    semester: normalizedSemester,
                    gpa: parseFloat(gpa.toFixed(2)),
                    earned_credits: earnedCredits,
                    total_credits: semesterCourses.reduce((sum, course) => sum + course.credits, 0),
                    rank: isSummarySemester ? (data.rank || '--') : '--',
                    week: isSummarySemester && data.week ? data.week : undefined,
                    courses: semesterCourses,
                    ai_forecast: isSummarySemester ? data.ai_forecast : undefined
                };
            };

            let newHistory: SemesterData[] = [];

            if (rawSemesters.length > 0) {
                newHistory = rawSemesters.map((semesterData: any) => {
                    const year = typeof semesterData.year === 'number' ? semesterData.year : parseInt(String(semesterData.year || 0), 10);
                    const semester = normalizeSemesterText(String(semesterData.semester || 'Unknown'));
                    const courses = Array.isArray(semesterData.courses)
                        ? semesterData.courses.map((course: Partial<Course>) => mapCourse({ ...course, year, semester }))
                        : [];
                    const computed = buildComputedSemester(year, semester, courses);
                    const isSummarySemester = summarySemester !== null
                        && summarySemester.year === year
                        && summarySemester.semester === semester;

                    return {
                        year,
                        semester,
                        gpa: typeof semesterData.gpa === 'number' ? semesterData.gpa : computed.gpa,
                        earned_credits: typeof semesterData.earned_credits === 'number' ? semesterData.earned_credits : computed.earned_credits,
                        total_credits: typeof semesterData.total_credits === 'number' && semesterData.total_credits > 0
                            ? semesterData.total_credits
                            : computed.total_credits,
                        rank: semesterData.rank || (isSummarySemester ? (data.rank || '--') : '--'),
                        week: isSummarySemester && data.week ? data.week : undefined,
                        courses,
                        ai_forecast: isSummarySemester ? data.ai_forecast : undefined
                    };
                });
            } else {
                const courses = rawCourses.map((course: Partial<Course>) => mapCourse(course));
                const grouped: { [key: string]: Course[] } = {};

                courses.forEach(course => {
                    const year = course.year || 0;
                    const semester = normalizeSemesterText(course.semester || 'Unknown');
                    const key = `${year}-${semester}`;
                    if (!grouped[key]) grouped[key] = [];
                    grouped[key].push(course);
                });

                newHistory = Object.keys(grouped).map(key => {
                    const [yearStr, ...semesterParts] = key.split('-');
                    return buildComputedSemester(parseInt(yearStr, 10), semesterParts.join('-'), grouped[key]);
                });
            }

            newHistory.sort(sortSemesters);

            setSelectedCourse(null);
            setHistory(newHistory);
            setSelectedSemesterIdx(0);
        } catch (e) {
            console.error(e);
        } finally {
            setLoading(false);
        }
    };

    useEffect(() => {
        fetchGrades();
    }, []);

    const syncSaintGrades = async (requireCredentials = false) => {
        const trimmedStudentId = studentId.trim();
        const hasInlineCredentials = trimmedStudentId.length > 0 && password.length > 0;

        if (requireCredentials && !hasInlineCredentials) {
            setSyncStatus('error');
            setSyncMsg('Enter your student ID and password to sync u-SAINT grades.');
            setNeedsLogin(true);
            return;
        }

        setSyncing(true);
        setSyncStatus('verifying');
        setSyncMsg('Connecting to u-SAINT crawler...');
        setNeedsLogin(false);

        try {
            const inlinePassword = password;
            if (hasInlineCredentials) {
                setPassword('');
            }

            const payload = hasInlineCredentials
                ? { student_id: trimmedStudentId, password: inlinePassword }
                : {};

            const startRes = await fetch('/api/academic/grades/sync', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });

            const data = await startRes.json().catch(() => ({
                success: false,
                message: `Sync failed (${startRes.status})`
            }));

            if (startRes.ok && data.success) {
                setSyncStatus('success');
                setSyncMsg(data.message || 'u-SAINT grades synced successfully.');

                await fetchGrades();
                setTimeout(() => setSyncMsg(''), 5000);
            } else {
                setSyncStatus('error');
                setSyncMsg(data.message || 'u-SAINT grade sync failed.');
                setNeedsLogin(true);
            }
        } catch (err) {
            setSyncStatus('error');
            setSyncMsg('Network error while syncing u-SAINT grades.');
            setNeedsLogin(true);
        } finally {
            setSyncing(false);
        }
    };

    const currentData = history[selectedSemesterIdx] || null;
    const latestSemester = history.length > 0 ? history[0] : null;
    const showLoginRequired = history.length === 0 && (needsLogin || !loading);

    const gradeColorIdx = (grade: string) => {
        if (grade.startsWith('A') || grade === 'P') return 'text-emerald-400';
        if (grade.startsWith('B')) return 'text-blue-400';
        if (grade.startsWith('C')) return 'text-orange-400';
        return 'text-red-400';
    };

    const gradeColor = (color: string) => {
        if (color === 'emerald') return 'accent-emerald';
        if (color === 'primary') return 'primary';
        if (color === 'orange') return 'accent-amber';
        if (color === 'blue') return 'blue-500';
        if (color === 'purple') return 'accent-purple';
        return 'slate-400';
    };

    const handleLoginConfirm = (event?: React.FormEvent<HTMLFormElement>) => {
        event?.preventDefault();
        syncSaintGrades(true);
    };

    return (
        <div className="flex-1 flex flex-col h-full bg-background-dark overflow-y-auto w-full relative text-main">
            {/* Header */}
            <header className="pt-8 pb-4 px-6 flex justify-between items-center bg-background-dark/90 backdrop-blur-md sticky top-0 z-20 border-b border-card-border">
                <div className="text-left pl-8 lg:pl-0">
                    <h2 className="text-[10px] font-bold text-primary uppercase tracking-widest mb-0.5">Academic</h2>
                    <h1 className="text-xl font-bold text-main leading-none">Academic Dashboard</h1>
                </div>
                <div className="flex items-center gap-4">
                    <button
                        onClick={syncSaintGrades}
                        disabled={syncing}
                        className="px-3 py-1.5 rounded-lg bg-primary/10 border border-primary/30 text-primary text-xs font-medium hover:bg-primary/20 transition-colors disabled:opacity-50 flex items-center gap-1"
                    >
                        <span className={`material-icons text-sm ${syncing ? 'animate-spin' : ''}`}>
                            {syncing ? 'sync' : 'cloud_download'}
                        </span>
                        {syncing ? 'Syncing...' : 'Sync u-SAINT'}
                    </button>
                    <button className="p-2 -mr-2 rounded-full hover:bg-card-border transition-colors relative">
                        <span className="absolute top-2 right-2 w-2 h-2 bg-red-500 rounded-full border border-background-dark"></span>
                        <span className="material-icons text-muted">notifications</span>
                    </button>
                </div>
            </header>

            {syncMsg && (
                <div className={`mx-6 mt-4 px-4 py-2 rounded-lg text-xs font-mono border ${syncStatus === 'success' ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300' :
                    syncStatus === 'error' ? 'bg-red-500/10 border-red-500/30 text-red-300' :
                        'bg-primary/10 border-primary/30 text-primary'
                    }`}>
                    {syncMsg}
                </div>
            )}

            {/* Content */}
            {loading ? (
                <div className="flex-1 flex items-center justify-center">
                    <span className="text-muted text-sm font-mono animate-pulse">Loading academic records...</span>
                </div>
            ) : showLoginRequired ? (
                <div className="flex-1 flex flex-col items-center justify-center p-8 space-y-6 animate-in fade-in duration-500">
                    <span className="material-icons text-7xl text-muted opacity-20">lock_person</span>
                    <div className="text-center space-y-2">
                        <h3 className="text-xl font-bold text-main tracking-tight">u-SAINT Login Required</h3>
                        <p className="text-sm text-muted/60 max-w-sm mx-auto leading-relaxed">
                            Enter your student ID and u-SAINT password to pull your real grade history from Soongsil u-SAINT.
                        </p>
                    </div>
                    <form onSubmit={handleLoginConfirm} className="w-full max-w-sm space-y-3">
                        <input
                            value={studentId}
                            onChange={event => setStudentId(event.target.value)}
                            type="text"
                            autoComplete="username"
                            placeholder="Student ID"
                            className="w-full rounded-lg border border-card-border bg-card-dark px-4 py-3 text-sm text-main outline-none transition-colors focus:border-primary/50 focus:ring-2 focus:ring-primary/20"
                        />
                        <input
                            value={password}
                            onChange={event => setPassword(event.target.value)}
                            type="password"
                            autoComplete="current-password"
                            placeholder="u-SAINT Password"
                            className="w-full rounded-lg border border-card-border bg-card-dark px-4 py-3 text-sm text-main outline-none transition-colors focus:border-primary/50 focus:ring-2 focus:ring-primary/20"
                        />
                        <button
                            type="submit"
                            disabled={syncing}
                            className="w-full px-8 py-3 rounded-lg bg-primary hover:bg-primary/90 text-white font-bold text-sm shadow-xl shadow-primary/20 transition-all transform hover:scale-[1.01] active:scale-[0.99] disabled:opacity-60 disabled:transform-none flex items-center justify-center gap-2"
                        >
                            <span className={`material-icons text-base ${syncing ? 'animate-spin' : ''}`}>{syncing ? 'sync' : 'cloud_download'}</span>
                            {syncing ? 'Syncing...' : 'Sync u-SAINT Grades'}
                        </button>
                    </form>
                    <p className="text-[11px] text-muted/50 max-w-sm text-center leading-relaxed">
                        Credentials are sent to the local Academic Hub backend only to run the u-SAINT crawler for this sync.
                    </p>
                </div>
            ) : (
                <div className="p-6 space-y-8 pb-20 animate-in slide-in-from-bottom-5 duration-500">

                    {/* --- Top Section: Latest Synced Semester --- */}
                    {latestSemester && (
                        <div className="space-y-4">
                            <h2 className="text-lg font-bold text-main flex items-center gap-2">
                                <span className="material-icons text-primary/80">dashboard</span>
                                Latest Synced Semester
                            </h2>

                            <div className="flex items-center justify-between px-2">
                                <div className="flex items-center gap-2">
                                    <span className="relative flex h-3 w-3">
                                        <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-primary opacity-75"></span>
                                        <span className="relative inline-flex rounded-full h-3 w-3 bg-primary"></span>
                                    </span>
                                    <span className="text-sm font-medium text-primary">Latest: {latestSemester.year} {latestSemester.semester}</span>
                                </div>
                                <span className="text-xs text-muted font-mono">Week {latestSemester.week ?? '--'}</span>
                            </div>

                            <div className="grid grid-cols-2 gap-3">
                                <div className="bg-card-dark border border-card-border rounded-xl p-4 flex flex-col items-center justify-center relative overflow-hidden group">
                                    <div className="relative w-24 h-12 overflow-hidden mb-2 mt-1">
                                        <div className="absolute top-0 left-0 w-24 h-24 rounded-full border-[6px] border-slate-700"></div>
                                        <div className="absolute top-0 left-0 w-24 h-24 rounded-full border-[6px] border-primary border-b-transparent border-l-transparent border-r-transparent transform -rotate-[25deg]" style={{ clipPath: 'polygon(0 0, 100% 0, 100% 50%, 0 50%)' }}></div>
                                    </div>
                                    <span className="text-3xl font-bold text-main mt-1">{latestSemester.gpa.toFixed(2)}</span>
                                    <span className="text-[10px] uppercase tracking-wider font-semibold text-muted mt-1">Semester GPA</span>
                                </div>
                                <div className="bg-card-dark border border-card-border rounded-xl p-4 flex flex-col items-center justify-center relative overflow-hidden">
                                    <div className="w-12 h-12 rounded-full bg-accent-emerald/10 flex items-center justify-center mb-2">
                                        <span className="material-symbols-outlined text-accent-emerald text-2xl">leaderboard</span>
                                    </div>
                                    <span className="text-2xl font-bold text-main">{latestSemester.rank}</span>
                                    <span className="text-[10px] uppercase tracking-wider font-semibold text-muted mt-1">Class Rank</span>
                                </div>
                            </div>

                            {/* Course List */}
                            <div className="bg-card-dark border border-card-border rounded-xl overflow-hidden flex flex-col">
                                <div className="p-4 border-b border-card-border flex justify-between items-center bg-card-dark/50">
                                    <h3 className="text-sm font-semibold text-muted">Courses</h3>
                                </div>
                                <div className="divide-y divide-card-border">
                                    {latestSemester.courses.map((course, idx) => (
                                        <div key={idx} className="p-4 hover:bg-slate-800/30 transition-colors cursor-pointer group" onClick={() => setSelectedCourse(course)}>
                                            <div className="flex justify-between items-start mb-2">
                                                <div className="flex items-center gap-3">
                                                    <div className={`w-8 h-8 rounded bg-${gradeColor(course.color)}/20 text-${gradeColor(course.color)} flex items-center justify-center font-bold text-sm`}>{course.code}</div>
                                                    <div>
                                                        <p className="text-sm font-semibold text-main">{course.name}</p>
                                                        <p className="text-[10px] font-mono text-muted">{course.full_code} 쨌 {course.professor}</p>
                                                    </div>
                                                </div>
                                                <div className="flex flex-col items-end">
                                                    <span className={`text-lg font-bold text-${gradeColor(course.color)}`}>{course.grade}</span>
                                                    <span className="text-[10px] text-muted">{course.credits} Credits</span>
                                                </div>
                                            </div>
                                            <div className="w-full bg-slate-700 h-1.5 rounded-full overflow-hidden mt-2">
                                                <div className={`bg-${gradeColor(course.color)} h-full rounded-full relative`} style={{ width: gradeBarWidth(course) }}>
                                                    {idx === 0 && <div className="absolute inset-0 bg-white/20 w-full animate-[shimmer_2s_infinite]"></div>}
                                                </div>
                                            </div>
                                        </div>
                                    ))}
                                </div>
                            </div>

                            {/* AI Insight Footer */}
                            {hasMeaningfulAiForecast(latestSemester.ai_forecast) && (
                                <div className="bg-[#0a0f12] border border-card-border rounded-xl p-4 shadow-inner relative overflow-hidden">
                                    <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-primary via-accent-purple to-accent-emerald opacity-50"></div>
                                    <div className="flex items-center gap-2 mb-3">
                                        <span className="material-icons text-primary text-sm animate-pulse">psychology</span>
                                        <h3 className="text-xs font-bold text-muted uppercase tracking-widest">Grade Forecast AI</h3>
                                    </div>
                                    <div className="font-mono text-xs space-y-3">
                                        <p className="text-accent-emerald flex items-start gap-2">
                                            <span className="opacity-50">&gt;</span>
                                            <span>Final GPA projected: <span className="text-main font-bold">{latestSemester.ai_forecast.projected_gpa}</span> (Confidence: {latestSemester.ai_forecast.confidence}%)</span>
                                        </p>
                                        <div className="bg-slate-800/50 p-2 rounded border border-slate-700/50">
                                            <p className="text-muted mb-1">Upcoming Risks:</p>
                                            <p className="text-accent-amber">&gt; WARNING: <span className="text-main">{latestSemester.ai_forecast.risk}</span></p>
                                        </div>
                                    </div>
                                </div>
                            )}
                        </div>
                    )}

                    <div className="h-px bg-card-border w-full my-8"></div>

                    {/* --- Bottom Section: Grade History Archive --- */}
                    <div className="space-y-4">
                        <h2 className="text-lg font-bold text-main flex items-center gap-2">
                            <span className="material-icons text-primary/80">history</span>
                            Grade History Archive
                        </h2>

                        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
                            {/* Selector */}
                            <div className="bg-card-dark border border-card-border rounded-xl p-4 flex flex-col gap-3 h-full">
                                <label className="text-xs uppercase font-bold text-muted tracking-wide">Select Semester</label>
                                <div className="space-y-1 max-h-48 overflow-y-auto pr-1 custom-scrollbar">
                                    {history.map((sem, idx) => (
                                        <button
                                            key={idx}
                                            onClick={() => setSelectedSemesterIdx(idx)}
                                            className={`w-full text-left px-3 py-2.5 rounded-lg text-sm transition-all flex justify-between items-center group ${idx === selectedSemesterIdx
                                                ? 'bg-primary/10 text-primary border border-primary/30 font-semibold'
                                                : 'text-muted hover:bg-card-border/30 hover:text-main'
                                                }`}
                                        >
                                            <span>{sem.year} - {sem.semester}</span>
                                            {idx === selectedSemesterIdx && <span className="material-icons text-xs">check</span>}
                                        </button>
                                    ))}
                                </div>
                            </div>

                            {/* Detailed Table */}
                            {currentData && (
                                <div className="lg:col-span-2 bg-card-dark border border-card-border rounded-xl overflow-hidden flex flex-col">
                                    <div className="p-4 border-b border-card-border flex justify-between items-center bg-card-dark/50">
                                        <h3 className="text-sm font-bold text-main">
                                            {currentData.year} {currentData.semester} Details
                                        </h3>
                                        <span className="text-xs text-muted font-mono bg-card-border/30 px-2 py-1 rounded">GPA: {currentData.gpa.toFixed(2)}</span>
                                    </div>

                                    {/* Table Header (Desktop) */}
                                    <div className="hidden lg:grid grid-cols-12 gap-4 px-6 py-3 border-b border-card-border bg-background-dark/50 text-xs font-bold text-muted uppercase tracking-wider">
                                        <div className="col-span-4">Course Name</div>
                                        <div className="col-span-2 text-center">Prof</div>
                                        <div className="col-span-1 text-center">Cred</div>
                                        <div className="col-span-1 text-center">Grade</div>
                                        <div className="col-span-2 text-center">Rank</div>
                                        <div className="col-span-2 text-center"></div>
                                    </div>

                                    {/* List */}
                                    <div className="divide-y divide-card-border">
                                        {currentData.courses.map((course, idx) => (
                                            <div key={idx} className="group hover:bg-white/5 transition-colors">
                                                <div className="hidden lg:grid grid-cols-12 gap-4 px-6 py-4 items-center">
                                                    <div className="col-span-4">
                                                        <div className="text-sm font-semibold text-main group-hover:text-primary transition-colors">{course.name}</div>
                                                        <div className="text-[10px] text-muted font-mono">{course.full_code}</div>
                                                    </div>
                                                    <div className="col-span-2 text-center text-sm text-main">{course.professor}</div>
                                                    <div className="col-span-1 text-center text-sm font-mono text-main">{course.credits}</div>
                                                    <div className={`col-span-1 text-center text-sm font-bold ${gradeColorIdx(course.grade)}`}>{course.grade}</div>
                                                    <div className="col-span-2 text-center text-xs text-muted font-mono">{course.rank}</div>
                                                    <div className="col-span-2 text-center flex justify-end">
                                                        <button
                                                            onClick={() => setSelectedCourse(course)}
                                                            className="p-1.5 rounded bg-primary/10 text-primary hover:bg-primary hover:text-white transition-all transform hover:scale-105"
                                                            title="View Detailed Scores"
                                                        >
                                                            <span className="material-icons text-sm">search</span>
                                                        </button>
                                                    </div>
                                                </div>
                                                {/* Mobile Row */}
                                                <div className="lg:hidden p-4 flex justify-between items-center" onClick={() => setSelectedCourse(course)}>
                                                    <div>
                                                        <div className="text-sm font-bold text-main">{course.name}</div>
                                                        <div className="text-xs text-muted">{course.grade} ({course.credits}cr)</div>
                                                    </div>
                                                    <button className="text-primary"><span className="material-icons">chevron_right</span></button>
                                                </div>
                                            </div>
                                        ))}
                                    </div>
                                </div>
                            )}
                        </div>
                    </div>
                </div>
            )}

            {/* Detailed Score Modal (Shared) */}
            {selectedCourse && (
                <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm" onClick={() => setSelectedCourse(null)}>
                    <div className="bg-card-dark border border-card-border rounded-xl shadow-2xl w-full max-w-lg overflow-hidden animate-in fade-in zoom-in-95 duration-200" onClick={e => e.stopPropagation()}>
                        <div className="px-6 py-4 border-b border-card-border flex justify-between items-center bg-card-dark/50">
                            <div>
                                <h3 className="text-lg font-bold text-main">{selectedCourse.name}</h3>
                                <p className="text-xs text-muted font-mono">{selectedCourse.full_code} 쨌 {selectedCourse.professor}</p>
                            </div>
                            <button onClick={() => setSelectedCourse(null)} className="text-muted hover:text-white transition-colors">
                                <span className="material-icons">close</span>
                            </button>
                        </div>

                        <div className="p-6">
                            <div className="mb-4 flex items-center justify-between bg-primary/5 p-3 rounded-lg border border-primary/10">
                                <div className="text-center">
                                    <div className="text-[10px] uppercase text-muted font-bold">Credits</div>
                                    <div className="text-xl font-bold text-primary">{selectedCourse.credits}</div>
                                </div>
                                <div className="w-[1px] h-8 bg-primary/20"></div>
                                <div className="text-center">
                                    <div className="text-[10px] uppercase text-muted font-bold">Grade</div>
                                    <div className={`text-xl font-bold ${gradeColorIdx(selectedCourse.grade)}`}>{selectedCourse.grade}</div>
                                </div>
                                <div className="w-[1px] h-8 bg-primary/20"></div>
                                <div className="text-center">
                                    <div className="text-[10px] uppercase text-muted font-bold">Rank</div>
                                    <div className="text-sm font-mono text-main mt-1">{selectedCourse.rank}</div>
                                </div>
                            </div>

                            <h4 className="text-xs font-bold text-muted uppercase tracking-wider mb-3">Score Breakdown</h4>

                            {(!selectedCourse.details || selectedCourse.details.length === 0) ? (
                                <div className="text-center py-6 text-muted text-sm border border-dashed border-card-border rounded-lg">
                                    No detailed scores available for this course.
                                </div>
                            ) : (
                                <div className="space-y-3">
                                    {selectedCourse.details.map((detail, idx) => (
                                        <div key={idx} className="bg-background-dark border border-card-border p-3 rounded-lg flex items-center justify-between">
                                            <span className="text-sm font-medium text-main">{detail.category}</span>
                                            <div className="flex items-center gap-3">
                                                <div className="w-24 h-1.5 bg-card-border rounded-full overflow-hidden">
                                                    <div
                                                        className="h-full bg-primary rounded-full"
                                                        style={{ width: `${(detail.score / detail.max_score) * 100}%` }}
                                                    ></div>
                                                </div>
                                                <span className="text-sm font-mono text-main w-16 text-right">
                                                    <span className="font-bold">{detail.score}</span>
                                                    <span className="text-muted">/{detail.max_score}</span>
                                                </span>
                                            </div>
                                        </div>
                                    ))}
                                </div>
                            )}
                        </div>

                        <div className="px-6 py-4 bg-background-dark/50 border-t border-card-border flex justify-end">
                            <button
                                onClick={() => setSelectedCourse(null)}
                                className="px-4 py-2 rounded-lg bg-card-border hover:bg-slate-600/50 text-xs font-bold text-main transition-colors"
                            >
                                Close Details
                            </button>
                        </div>
                    </div>
                </div>
            )}
        </div>
    );
};
