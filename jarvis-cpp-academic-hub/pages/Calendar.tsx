import React, { useState, useEffect, useCallback } from 'react';

interface CalendarEvent {
    id?: number;
    time: string;
    type: string;
    title: string;
    location: string;
    duration: string;
    done: boolean;
    description?: string;
    attendees?: number;
    source?: string;
    google_link?: string;
}

interface CalendarData {
    month: string;
    selected_date: string;
    week: string;
    events: CalendarEvent[];
}

interface MonthData {
    dates: string[];
    google_dates: string[];
}

// ── Daily Donut Chart Component ──
const DailyDonut: React.FC<{ events: CalendarEvent[] }> = ({ events }) => {
    const r = 56, cx = 75, cy = 75;
    const circumference = 2 * Math.PI * r;

    // Parse time to hour (0-24)
    const parseHour = (t: string) => {
        const parts = t.match(/(\d+):?(\d*)\s*(AM|PM)?/i);
        if (!parts) return 0;
        let h = parseInt(parts[1], 10);
        const ampm = parts[3]?.toUpperCase();
        if (ampm === 'PM' && h !== 12) h += 12;
        if (ampm === 'AM' && h === 12) h = 0;
        return h;
    };

    // Parse duration to hours
    const parseDuration = (d: string) => {
        if (!d) return 1;
        const hMatch = d.match(/(\d+)\s*h/i);
        const mMatch = d.match(/(\d+)\s*m/i);
        return (hMatch ? parseInt(hMatch[1]) : 0) + (mMatch ? parseInt(mMatch[1]) / 60 : 0) || 1;
    };

    const typeColors: Record<string, string> = {
        'Exam': '#f97316', 'Workshop': '#13a4ec', 'Task': '#64748b',
        'Meeting': '#a855f7', 'Google': '#3b82f6',
    };

    const scheduled = events.filter(e => !e.done && e.time);

    return (
        <div className="bg-card-dark border border-card-border rounded-xl p-3 lg:p-4 mb-3 lg:mb-4">
            <div className="flex items-center gap-2 mb-3">
                <span className="material-icons text-primary text-sm">donut_large</span>
                <span className="text-xs font-bold text-muted uppercase tracking-widest">Daily Overview</span>
            </div>
            <div className="flex items-center justify-center">
                <svg className="w-[120px] h-[120px] lg:w-[150px] lg:h-[150px]" viewBox="0 0 150 150">
                    {/* Background ring */}
                    <circle cx={cx} cy={cy} r={r} fill="none" stroke="currentColor" strokeWidth="12" className="text-card-border/50" />
                    {/* Hour markers */}
                    {[0, 6, 12, 18].map(h => {
                        const angle = (h / 24) * 360 - 90;
                        const rad = angle * Math.PI / 180;
                        const x1 = cx + (r - 8) * Math.cos(rad);
                        const y1 = cy + (r - 8) * Math.sin(rad);
                        const x2 = cx + (r + 8) * Math.cos(rad);
                        const y2 = cy + (r + 8) * Math.sin(rad);
                        const tx = cx + (r + 18) * Math.cos(rad);
                        const ty = cy + (r + 18) * Math.sin(rad);
                        return (
                            <g key={h}>
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="currentColor" strokeWidth="1" className="text-muted" />
                                <text x={tx} y={ty} textAnchor="middle" dominantBaseline="central"
                                    className="fill-muted text-[8px] font-mono">{h}h</text>
                            </g>
                        );
                    })}
                    {/* Event arcs */}
                    {scheduled.map((ev, idx) => {
                        const startH = parseHour(ev.time);
                        const dur = parseDuration(ev.duration);
                        const startFrac = startH / 24;
                        const durFrac = Math.min(dur / 24, 1);
                        const offset = circumference * (1 - durFrac);
                        const rotation = startFrac * 360 - 90;
                        const color = typeColors[ev.source === 'google' ? 'Google' : ev.type] || '#64748b';
                        return (
                            <circle key={idx} cx={cx} cy={cy} r={r} fill="none"
                                stroke={color} strokeWidth="10" strokeLinecap="round"
                                strokeDasharray={circumference} strokeDashoffset={offset}
                                transform={`rotate(${rotation} ${cx} ${cy})`}
                                opacity="0.8"
                                style={{ filter: `drop-shadow(0 0 4px ${color}44)` }} />
                        );
                    })}
                    {/* Center text */}
                    <text x={cx} y={cy - 6} textAnchor="middle" className="fill-main text-lg font-bold">{events.length}</text>
                    <text x={cx} y={cy + 10} textAnchor="middle" className="fill-muted text-[9px] font-medium uppercase">Events</text>
                </svg>
            </div>
            {/* Legend */}
            {scheduled.length > 0 && (
                <div className="hidden lg:flex flex-wrap gap-2 mt-3 justify-center">
                    {scheduled.map((ev, idx) => {
                        const color = typeColors[ev.source === 'google' ? 'Google' : ev.type] || '#64748b';
                        return (
                            <div key={idx} className="flex items-center gap-1.5 text-[10px] text-muted">
                                <div className="w-2 h-2 rounded-full" style={{ backgroundColor: color }}></div>
                                <span className="truncate max-w-[80px]">{ev.title}</span>
                                <span className="text-muted/70">{ev.time}</span>
                            </div>
                        );
                    })}
                </div>
            )}
        </div>
    );
};

export const Calendar: React.FC = () => {
    const now = new Date();

    // Check if a specific date was requested (e.g., from Dashboard calendar click)
    const navDate = typeof window !== 'undefined' ? localStorage.getItem('jarvis_nav_date') : null;
    const initDate = navDate ? new Date(navDate + 'T00:00:00') : now;
    if (navDate) localStorage.removeItem('jarvis_nav_date');

    const [year, setYear] = useState(initDate.getFullYear());
    const [month, setMonth] = useState(initDate.getMonth() + 1);
    const [selectedDay, setSelectedDay] = useState(initDate.getDate());
    const [data, setData] = useState<CalendarData | null>(null);
    const [monthDates, setMonthDates] = useState<MonthData>({ dates: [], google_dates: [] });
    const [loading, setLoading] = useState(true);

    // Modal state: 'add' | 'edit' | null
    const [modalMode, setModalMode] = useState<'add' | 'edit' | null>(null);
    const [editingId, setEditingId] = useState<number | null>(null);

    // Event form
    const [formTitle, setFormTitle] = useState('');
    const [formTime, setFormTime] = useState('09:00 AM');
    const [formType, setFormType] = useState('Task');
    const [formLocation, setFormLocation] = useState('');
    const [formDuration, setFormDuration] = useState('');

    const dateStr = `${year}-${String(month).padStart(2, '0')}-${String(selectedDay).padStart(2, '0')}`;

    const fetchEvents = useCallback(() => {
        setLoading(true);
        fetch(`/api/calendar/events?date=${dateStr}`)
            .then(res => res.json())
            .then((d: CalendarData) => { setData(d); setLoading(false); })
            .catch(() => setLoading(false));
    }, [dateStr]);

    const fetchMonthDates = useCallback(() => {
        fetch(`/api/calendar/month?year=${year}&month=${month}`)
            .then(res => res.json())
            .then((d: MonthData) => setMonthDates(d))
            .catch(() => { });
    }, [year, month]);

    useEffect(() => { fetchEvents(); }, [fetchEvents]);
    useEffect(() => { fetchMonthDates(); }, [fetchMonthDates]);

    const daysInMonth = new Date(year, month, 0).getDate();
    const firstDayOfWeek = new Date(year, month - 1, 1).getDay();
    const startOffset = firstDayOfWeek === 0 ? 6 : firstDayOfWeek - 1;
    const prevMonthDays = new Date(year, month - 1, 0).getDate();

    const prevMonth = () => {
        if (month === 1) { setYear(y => y - 1); setMonth(12); }
        else setMonth(m => m - 1);
        setSelectedDay(1);
    };
    const nextMonth = () => {
        if (month === 12) { setYear(y => y + 1); setMonth(1); }
        else setMonth(m => m + 1);
        setSelectedDay(1);
    };
    const goToday = () => {
        const t = new Date();
        setYear(t.getFullYear());
        setMonth(t.getMonth() + 1);
        setSelectedDay(t.getDate());
    };

    const isToday = (d: number) => d === now.getDate() && month === now.getMonth() + 1 && year === now.getFullYear();
    const hasEvent = (d: number) => monthDates.dates.includes(`${year}-${String(month).padStart(2, '0')}-${String(d).padStart(2, '0')}`);
    const hasGoogleEvent = (d: number) => monthDates.google_dates.includes(`${year}-${String(month).padStart(2, '0')}-${String(d).padStart(2, '0')}`);

    const monthNames = ['', 'January', 'February', 'March', 'April', 'May', 'June',
        'July', 'August', 'September', 'October', 'November', 'December'];

    const resetForm = () => {
        setFormTitle(''); setFormTime('09:00 AM'); setFormType('Task');
        setFormLocation(''); setFormDuration('');
        setEditingId(null);
    };

    const openAddModal = () => {
        resetForm();
        setModalMode('add');
    };

    const openEditModal = (event: CalendarEvent) => {
        setFormTitle(event.title);
        setFormTime(event.time);
        setFormType(event.type);
        setFormLocation(event.location || '');
        setFormDuration(event.duration || '');
        setEditingId(event.id || null);
        setModalMode('edit');
    };

    const closeModal = () => {
        setModalMode(null);
        resetForm();
    };

    const submitEvent = async () => {
        if (!formTitle.trim()) return;

        if (modalMode === 'edit' && editingId) {
            await fetch(`/api/calendar/events/${editingId}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    date: dateStr, time: formTime, type: formType,
                    title: formTitle, location: formLocation, duration: formDuration
                })
            });
        } else {
            await fetch('/api/calendar/events', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    date: dateStr, time: formTime, type: formType,
                    title: formTitle, location: formLocation, duration: formDuration
                })
            });
        }
        closeModal();
        fetchEvents();
        fetchMonthDates();
    };

    const deleteEvent = async (id: number) => {
        await fetch(`/api/calendar/events/${id}`, { method: 'DELETE' });
        fetchEvents();
        fetchMonthDates();
    };

    const toggleEvent = async (id: number) => {
        await fetch(`/api/calendar/events/${id}/toggle`, { method: 'POST' });
        fetchEvents();
    };

    const typeColor = (type: string) => {
        if (type === 'Exam') return 'accent-orange';
        if (type === 'Workshop') return 'primary';
        if (type === 'Google') return 'blue';
        return 'slate';
    };

    const events = data?.events ?? [];

    return (
        <div className="flex flex-1 flex-col h-full bg-background-dark text-main relative overflow-y-auto lg:overflow-hidden">
            <header className="flex-none px-6 pt-5 pb-4 flex justify-between items-center bg-card-dark/50 backdrop-blur-md border-b border-card-border/50 z-20">
                <h1 className="text-lg font-semibold tracking-wide text-main pl-8 lg:pl-0">Calendar</h1>
                <div className="flex items-center gap-3">
                    <button onClick={goToday} className="px-3 py-1 rounded-md bg-card-dark border border-card-border text-xs font-medium text-muted hover:bg-card-border/50 transition-colors">
                        Today
                    </button>
                    <button onClick={openAddModal} className="px-3 py-1 rounded-md bg-primary/10 border border-primary/30 text-xs font-medium text-primary hover:bg-primary/20 transition-colors flex items-center gap-1">
                        <span className="material-icons text-sm">add</span> Add Event
                    </button>
                </div>
            </header>

            <div className="flex flex-1 min-h-0 relative flex-col lg:flex-row overflow-visible lg:overflow-hidden">
                {/* Calendar Grid */}
                <div className="flex-none lg:w-96 flex flex-col border-b lg:border-b-0 lg:border-r border-card-border bg-background-dark z-10 lg:shadow-xl overflow-visible lg:overflow-y-auto">
                    <section className="px-4 pb-4 pt-4">
                        <div className="flex justify-between items-center mb-4 px-1">
                            <button onClick={prevMonth} className="p-1 rounded hover:bg-card-border/10 text-muted transition-colors">
                                <span className="material-symbols-outlined">chevron_left</span>
                            </button>
                            <h2 className="text-xl font-medium text-main">{monthNames[month]} {year}</h2>
                            <button onClick={nextMonth} className="p-1 rounded hover:bg-card-border/10 text-muted transition-colors">
                                <span className="material-symbols-outlined">chevron_right</span>
                            </button>
                        </div>

                        <div className="grid grid-cols-7 gap-1 mb-2 text-center">
                            {['Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su'].map((d, i) => (
                                <div key={i} className={`text-[10px] font-medium uppercase tracking-wider ${i > 4 ? 'text-accent-orange/80' : 'text-muted'}`}>{d}</div>
                            ))}
                        </div>

                        <div className="grid grid-cols-7 gap-1">
                            {Array.from({ length: startOffset }).map((_, i) => (
                                <div key={`prev-${i}`} className="aspect-square flex flex-col items-center justify-center rounded text-muted/50 text-xs">
                                    {prevMonthDays - startOffset + 1 + i}
                                </div>
                            ))}
                            {Array.from({ length: daysInMonth }).map((_, i) => {
                                const day = i + 1;
                                const selected = day === selectedDay;
                                const today = isToday(day);
                                const hasEv = hasEvent(day);
                                const hasGoogle = hasGoogleEvent(day);
                                return (
                                    <div key={day} onClick={() => setSelectedDay(day)}
                                        className={`aspect-square flex flex-col items-center justify-center rounded border transition-colors cursor-pointer relative overflow-hidden group
                                        ${selected ? 'bg-card-dark border-card-border text-main shadow-sm' :
                                                today ? 'bg-primary/10 text-primary border-primary/50' :
                                                    'bg-card-dark/30 text-muted border-transparent hover:border-card-border/50'}`}>
                                        {selected && <div className="absolute top-0 right-0 w-2 h-2 bg-accent-orange rounded-bl-md"></div>}
                                        <span className={`text-sm ${today || selected ? 'font-bold' : ''}`}>{day}</span>
                                        <div className="flex gap-0.5 mt-1">
                                            {hasEv && <div className="w-1 h-1 rounded-full bg-accent-orange"></div>}
                                            {hasGoogle && <div className="w-1 h-1 rounded-full bg-blue-400"></div>}
                                            {today && !selected && <div className="w-1 h-1 rounded-full bg-primary"></div>}
                                        </div>
                                    </div>
                                );
                            })}
                            {Array.from({ length: (7 - (startOffset + daysInMonth) % 7) % 7 }).map((_, i) => (
                                <div key={`next-${i}`} className="aspect-square flex flex-col items-center justify-center rounded text-muted/50 text-xs">
                                    {i + 1}
                                </div>
                            ))}
                        </div>
                    </section>

                    {/* Daily Donut Chart — inside sidebar */}
                    {events.length > 0 && (
                        <div className="hidden lg:block px-4 pb-4">
                            <DailyDonut events={events} />
                        </div>
                    )}
                </div>

                {/* Events Timeline */}
                <main className="flex-1 min-h-0 overflow-visible lg:overflow-y-auto bg-background-dark relative pb-8">
                    <div className="sticky top-0 bg-background-dark/95 backdrop-blur-sm z-10 px-6 py-4 border-b border-card-border flex justify-between items-end">
                        <div>
                            <p className="text-xs text-muted font-medium uppercase tracking-widest mb-1">Selected Date</p>
                            <h3 className="text-lg font-bold text-main flex items-center gap-2">
                                {data?.selected_date ?? dateStr}
                                <span className="text-xs font-normal text-muted border border-card-border px-1.5 py-0.5 rounded">{data?.week ?? ''}</span>
                            </h3>
                        </div>
                        <div className="bg-primary/10 px-3 py-1 rounded-full border border-primary/20">
                            <span className="text-xs font-medium text-primary">{events.length} Tasks</span>
                        </div>
                    </div>

                    {loading ? (
                        <div className="flex items-center justify-center py-12">
                            <span className="text-muted text-sm font-mono animate-pulse">Loading events...</span>
                        </div>
                    ) : events.length === 0 ? (
                        <div className="flex flex-col items-center justify-center py-16 gap-4">
                            <span className="material-symbols-outlined text-4xl text-muted">event_available</span>
                            <p className="text-sm text-muted">No events for this date</p>
                            <button onClick={openAddModal} className="px-4 py-2 bg-primary/10 border border-primary/30 text-primary text-xs font-medium rounded-lg hover:bg-primary/20 transition-colors flex items-center gap-1">
                                <span className="material-icons text-sm">add</span> Add Event
                            </button>
                        </div>
                    ) : (
                        <div className="px-5 py-6 space-y-4">
                            {events.map((event, idx) => {
                                const timeParts = event.time.split(' ');
                                const timeStr = timeParts[0];
                                const ampm = timeParts[1] || '';
                                const tc = typeColor(event.type);
                                const isGoogle = event.source === 'google';

                                return (
                                    <div key={idx} className="flex group">
                                        <div className="flex flex-col items-center mr-4 pt-1 w-12 flex-none">
                                            <span className={`text-sm font-semibold ${event.done ? 'text-muted' : 'text-main'}`}>{timeStr}</span>
                                            <span className={`text-[10px] ${event.done ? 'text-muted' : 'text-muted/80'}`}>{ampm}</span>
                                            {idx < events.length - 1 && <div className="h-full w-[1px] bg-card-border mt-2"></div>}
                                        </div>
                                        <div className={`flex-1 ${event.done ? 'bg-card-dark opacity-60' : 'bg-card-dark'} rounded-lg p-3 border ${event.done ? 'border-card-border' : isGoogle ? 'border-blue-500/20' : event.type === 'Exam' ? `border-${tc}/20` : 'border-card-border'} relative overflow-hidden ${!event.done ? 'shadow-sm hover:border-card-border/50 transition-colors' : ''}`}>
                                            <div className={`absolute left-0 top-0 bottom-0 w-1 ${event.done ? 'bg-card-border' : isGoogle ? 'bg-blue-400' : `bg-${tc}`}`}></div>
                                            <div className="flex justify-between items-start mb-1.5">
                                                <span className={`px-2 py-0.5 rounded text-[10px] font-bold uppercase tracking-wider ${event.done ? 'bg-card-dark text-muted flex items-center gap-1' : isGoogle ? 'bg-blue-500/10 text-blue-400' : `bg-${tc}/10 text-${tc}`}`}>
                                                    {event.done && <span className="material-symbols-outlined text-[10px]">check</span>}
                                                    {event.done ? 'Done' : isGoogle ? '📅 Google' : event.type}
                                                </span>
                                                <div className="flex items-center gap-1">
                                                    {!isGoogle && event.id && (
                                                        <>
                                                            <button onClick={() => openEditModal(event)} className="text-muted hover:text-primary transition-colors" title="Edit">
                                                                <span className="material-symbols-outlined text-lg">edit</span>
                                                            </button>
                                                            <button onClick={() => toggleEvent(event.id!)} className="text-muted hover:text-emerald-400 transition-colors" title="Toggle done">
                                                                <span className="material-symbols-outlined text-lg">{event.done ? 'undo' : 'check_circle'}</span>
                                                            </button>
                                                            <button onClick={() => deleteEvent(event.id!)} className="text-muted hover:text-red-400 transition-colors" title="Delete">
                                                                <span className="material-symbols-outlined text-lg">delete</span>
                                                            </button>
                                                        </>
                                                    )}
                                                    {isGoogle && event.google_link && (
                                                        <a href={event.google_link} target="_blank" rel="noopener noreferrer" className="text-blue-400 hover:text-blue-300 transition-colors">
                                                            <span className="material-symbols-outlined text-lg">open_in_new</span>
                                                        </a>
                                                    )}
                                                </div>
                                            </div>
                                            <h4 className={`text-base font-medium ${event.done ? 'text-muted line-through' : 'text-main'} mb-1`}>{event.title}</h4>
                                            {event.description && <p className="text-xs text-muted mb-2 leading-relaxed">{event.description}</p>}
                                            <div className={`flex items-center ${event.done ? 'text-muted' : 'text-muted/80'} text-xs gap-3`}>
                                                {event.location && (
                                                    <div className="flex items-center gap-1">
                                                        <span className="material-symbols-outlined text-sm">location_on</span>
                                                        <span>{event.location}</span>
                                                    </div>
                                                )}
                                                {event.duration && (
                                                    <div className="flex items-center gap-1">
                                                        <span className="material-symbols-outlined text-sm">schedule</span>
                                                        <span>{event.duration}</span>
                                                    </div>
                                                )}
                                            </div>
                                        </div>
                                    </div>
                                );
                            })}
                        </div>
                    )}
                </main>
            </div>

            {/* Add/Edit Event Modal */}
            {modalMode && (
                <div className="fixed inset-0 bg-black/60 backdrop-blur-sm z-50 flex items-center justify-center p-4" onClick={closeModal}>
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6 w-full max-w-md shadow-2xl" onClick={e => e.stopPropagation()}>
                        <div className="flex justify-between items-center mb-6">
                            <h3 className="text-lg font-bold text-main">{modalMode === 'edit' ? 'Edit Event' : 'New Event'}</h3>
                            <button onClick={closeModal} className="text-muted hover:text-main transition-colors">
                                <span className="material-icons">close</span>
                            </button>
                        </div>
                        <div className="space-y-4">
                            <div>
                                <label className="text-[10px] uppercase text-muted font-bold tracking-wider block mb-1">Title *</label>
                                <input value={formTitle} onChange={e => setFormTitle(e.target.value)}
                                    className="w-full bg-background-dark border border-card-border rounded-lg px-3 py-2 text-sm text-main focus:border-primary focus:outline-none"
                                    placeholder="Event title" />
                            </div>
                            <div className="grid grid-cols-2 gap-3">
                                <div>
                                    <label className="text-[10px] uppercase text-muted font-bold tracking-wider block mb-1">Time</label>
                                    <input value={formTime} onChange={e => setFormTime(e.target.value)}
                                        className="w-full bg-background-dark border border-card-border rounded-lg px-3 py-2 text-sm text-main focus:border-primary focus:outline-none"
                                        placeholder="09:00 AM" />
                                </div>
                                <div>
                                    <label className="text-[10px] uppercase text-muted font-bold tracking-wider block mb-1">Type</label>
                                    <select value={formType} onChange={e => setFormType(e.target.value)}
                                        className="w-full bg-background-dark border border-card-border rounded-lg px-3 py-2 text-sm text-main focus:border-primary focus:outline-none">
                                        <option value="Task">Task</option>
                                        <option value="Exam">Exam</option>
                                        <option value="Workshop">Workshop</option>
                                        <option value="Meeting">Meeting</option>
                                    </select>
                                </div>
                            </div>
                            <div>
                                <label className="text-[10px] uppercase text-muted font-bold tracking-wider block mb-1">Location</label>
                                <input value={formLocation} onChange={e => setFormLocation(e.target.value)}
                                    className="w-full bg-background-dark border border-card-border rounded-lg px-3 py-2 text-sm text-main focus:border-primary focus:outline-none"
                                    placeholder="Room / Building" />
                            </div>
                            <div>
                                <label className="text-[10px] uppercase text-muted font-bold tracking-wider block mb-1">Duration</label>
                                <input value={formDuration} onChange={e => setFormDuration(e.target.value)}
                                    className="w-full bg-background-dark border border-card-border rounded-lg px-3 py-2 text-sm text-main focus:border-primary focus:outline-none"
                                    placeholder="1h 30m" />
                            </div>
                        </div>
                        <div className="flex gap-3 mt-6">
                            <button onClick={closeModal}
                                className="flex-1 py-2.5 rounded-lg border border-card-border text-muted text-sm font-medium hover:bg-card-border/10 transition-colors">
                                Cancel
                            </button>
                            <button onClick={submitEvent} disabled={!formTitle.trim()}
                                className="flex-1 py-2.5 rounded-lg bg-primary text-white text-sm font-medium hover:bg-primary/90 transition-colors disabled:opacity-50 disabled:cursor-not-allowed shadow-lg shadow-primary/20">
                                {modalMode === 'edit' ? 'Save Changes' : 'Add Event'}
                            </button>
                        </div>
                    </div>
                </div>
            )}
        </div>
    );
};
