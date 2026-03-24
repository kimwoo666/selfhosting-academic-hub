import React, { useState, useEffect, useRef } from 'react';
import { PageView } from '../types';

interface SummaryData {
    system: { cpu: number; ram: number; disk: number; temp: number };
    uptime: string;
    kernel: string;
    ai_status: string;
    next_task: { title: string; d_day: string; course: string; due: string };
    active_alerts: number;
    active_nodes: number;
    total_nodes: number;
    network: { download_mbps: number; upload_mbps: number };
    containers: Array<{ id: string; name: string; status: string; cpu: number; ram: number; ram_used_mb: number; ram_total_mb: number }>;
    ai_insights: Array<{ level: string; message: string }>;
    notices: Array<{ title: string; description: string; match: number; date: string }>;
    priorities: Array<{ id: number; title: string; tag: string; due: string; course: string; done: boolean; ai_action: string }>;
}

const CircularProgress: React.FC<{ value: number, label: string, color: string, rotate?: string, sizeClass?: string }> = ({ value, label, color, sizeClass = "h-32" }) => {
    // SVG semicircle gauge ??arc fills from LEFT to RIGHT based on percentage
    const radius = 44;
    const circumference = Math.PI * radius; // half-circle length
    const clampedValue = Math.max(0, Math.min(100, value));
    const fillLength = (clampedValue / 100) * circumference;

    return (
        <div className={`bg-card-dark border border-card-border rounded-xl p-4 flex flex-col items-center justify-center relative overflow-hidden group w-full transition-all hover:border-white/10 ${sizeClass}`}>
            <div className="relative w-28 h-14 mb-3 scale-110 lg:scale-125 transition-transform">
                <svg viewBox="0 0 100 52" className="w-full h-full">
                    {/* Background arc */}
                    <path
                        d="M 6,50 A 44,44 0 0,1 94,50"
                        fill="none"
                        stroke="#334155"
                        strokeWidth="7"
                        strokeLinecap="round"
                    />
                    {/* Value arc – fills from left (start of path) using dash-gap array */}
                    {clampedValue > 0 && (
                        <path
                            d="M 6,50 A 44,44 0 0,1 94,50"
                            fill="none"
                            stroke={color}
                            strokeWidth="7"
                            strokeLinecap="round"
                            strokeDasharray={`${fillLength} ${circumference}`}
                            style={{ transition: 'stroke-dasharray 1s ease-out', filter: `drop-shadow(0 0 6px ${color}66)` }}
                        />
                    )}
                </svg>
            </div>
            <span className="text-3xl lg:text-4xl font-bold text-white tracking-tight">{value}<span className="text-sm text-slate-500 font-medium ml-1">%</span></span>
            <span className="text-xs uppercase tracking-widest font-bold text-slate-400 mt-2">{label}</span>
        </div>
    );
};

interface DashboardProps {
    onNavigate: (page: PageView) => void;
}

export const Dashboard: React.FC<DashboardProps> = ({ onNavigate }) => {
    const [data, setData] = useState<SummaryData | null>(null);
    const [loading, setLoading] = useState(true);
    const [hoveredRam, setHoveredRam] = useState<number | null>(null);
    const [localPriorities, setLocalPriorities] = useState<SummaryData['priorities']>([]);
    const [summarizing, setSummarizing] = useState<number | null>(null);
    const [summaryResult, setSummaryResult] = useState<{ id: number; text: string } | null>(null);
    const containerOrderRef = useRef<string[]>([]);
    const dlHistory = useRef<number[]>([]);
    const ulHistory = useRef<number[]>([]);
    const [netTick, setNetTick] = useState(0);
    const [insightsUpdatedAt, setInsightsUpdatedAt] = useState('--:--:--');

    const displayPref = typeof window !== 'undefined'
        ? (localStorage.getItem('jarvis_resource_display') || 'percent')
        : 'percent';

    const fetchSummary = () => {
        fetch('/api/summary')
            .then(res => res.json())
            .then((d: SummaryData) => {
                setData(d);
                setLoading(false);
                // Set initial priorities from server
                if (d.priorities) setLocalPriorities(d.priorities);
                // Establish stable container order on first load
                if (d.containers && containerOrderRef.current.length === 0) {
                    containerOrderRef.current = [...d.containers]
                        .sort((a, b) => b.ram_total_mb - a.ram_total_mb)
                        .map(c => c.id);
                }
                // Push new network values into history (max 20 points)
                const dl = d?.network?.download_mbps ?? 0;
                const ul = d?.network?.upload_mbps ?? 0;
                dlHistory.current = [...dlHistory.current, dl].slice(-20);
                ulHistory.current = [...ulHistory.current, ul].slice(-20);
                setNetTick(t => t + 1);
                setInsightsUpdatedAt(new Date().toLocaleTimeString('ko-KR', { hour12: false }));
            })
            .catch(err => { console.error('Failed to fetch summary:', err); setLoading(false); });
    };

    useEffect(() => {
        fetchSummary();
        const interval = setInterval(fetchSummary, 5000);
        return () => clearInterval(interval);
    }, []);

    // Fallback values while loading
    const sys = data?.system ?? { cpu: 0, ram: 0, disk: 0, temp: 0 };
    const uptime = data?.uptime ?? '--';
    const kernel = data?.kernel ?? '--';
    const activeNodes = data?.active_nodes ?? 0;
    const networkDl = data?.network?.download_mbps ?? 0;
    const networkUl = data?.network?.upload_mbps ?? 0;
    // Sort containers using stable order (first poll determines position by RAM total)
    const rawContainers = data?.containers ?? [];
    const containers = containerOrderRef.current.length > 0
        ? containerOrderRef.current
            .map(id => rawContainers.find(c => c.id === id))
            .filter(Boolean) as typeof rawContainers
        : rawContainers;
    const insights = (data?.ai_insights && data.ai_insights.length > 0)
        ? data.ai_insights
        : [{ level: 'info', message: 'No AI insight available yet. Waiting for next summary tick.' }];
    const notices = data?.notices ?? [];
    const priorities = localPriorities;

    const handleToggleDone = async (eventId: number) => {
        try {
            await fetch(`/api/calendar/events/${eventId}/toggle`, { method: 'POST' });
            setLocalPriorities(prev => prev.map(p =>
                p.id === eventId
                    ? { ...p, done: !p.done, tag: !p.done ? 'DONE' : (p.tag === 'DONE' ? 'STUDY' : p.tag) }
                    : p
            ));
        } catch (e) { console.error('Toggle failed:', e); }
    };

    const handleSummarize = async (eventId: number) => {
        setSummarizing(eventId);
        setSummaryResult(null);
        try {
            const res = await fetch(`/api/calendar/events/${eventId}/summarize`, { method: 'POST' });
            const data = await res.json();
            if (data.summary) {
                setSummaryResult({ id: eventId, text: data.summary });
            }
        } catch (e) { console.error('Summarize failed:', e); }
        setSummarizing(null);
    };

    const statusColor = (s: string) => {
        if (s === 'running') return 'emerald';
        if (s === 'high_load') return 'accent-amber';
        return 'slate';
    };

    const statusLabel = (s: string) => {
        if (s === 'running') return 'RUNNING';
        if (s === 'high_load') return 'HIGH LOAD';
        return 'STOPPED';
    };

    return (
        <div className="flex-1 flex flex-col h-full bg-background-dark overflow-y-auto no-scrollbar">
            {/* Main Header */}
            <header className="pt-6 pb-4 px-6 flex justify-between items-center bg-background-dark/95 backdrop-blur-md sticky top-0 z-30 border-b border-card-border">
                <div className="text-left pl-8 lg:pl-0">
                    <h2 className="text-[10px] font-bold text-primary uppercase tracking-widest mb-0.5">System Overview</h2>
                    <h1 className="text-xl font-bold text-main leading-none">Command Center</h1>
                </div>
                <div className="flex items-center gap-4">
                    <button className="p-2 -mr-2 rounded-full hover:bg-card-border transition-colors relative">
                        {data && data.active_alerts > 0 && <span className="absolute top-2 right-2 w-2 h-2 bg-red-500 rounded-full border border-background-dark"></span>}
                        <span className="material-icons text-muted">notifications</span>
                    </button>
                    <button
                        onClick={() => onNavigate('profile')}
                        className="relative w-8 h-8 rounded-full overflow-hidden border border-card-border ring-2 ring-bg-base cursor-pointer hover:ring-primary/50 transition-all"
                    >
                        <img alt="User profile" className="w-full h-full object-cover" src="https://ui-avatars.com/api/?name=Alex+Chen&background=13a4ec&color=fff" />
                    </button>
                </div>
            </header>

            {loading ? (
                <div className="flex-1 flex items-center justify-center">
                    <div className="text-muted text-sm font-mono animate-pulse">Loading system data...</div>
                </div>
            ) : (
                <div className="p-6 pb-24 grid grid-cols-1 xl:grid-cols-4 gap-6 animate-[fadeIn_0.5s_ease-out]">

                    {/* --- LEFT COLUMN: INFRASTRUCTURE --- */}
                    <div className="xl:col-span-3 space-y-6">

                        {/* Status Bar */}
                        <div className="flex items-center justify-between px-2 bg-card-dark/30 py-2 rounded-lg border border-card-border">
                            <div className="flex items-center gap-3 px-2">
                                <span className="relative flex h-3 w-3">
                                    <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>
                                    <span className="relative inline-flex rounded-full h-3 w-3 bg-emerald-500"></span>
                                </span>
                                <span className="text-sm font-medium text-emerald-400 tracking-wide uppercase">System Online</span>
                            </div>
                            <div className="flex items-center gap-4 px-2">
                                <span className="text-xs text-muted font-mono hidden sm:inline">Kernel: {kernel}</span>
                                <span className="text-xs text-muted font-mono bg-card-border/10 px-2 py-1 rounded">Uptime: {uptime}</span>
                            </div>
                        </div>

                        {/* Gauges */}
                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
                            <CircularProgress value={Math.round(sys.cpu)} label="CPU Load" color="#10b981" rotate="transform -rotate-45" sizeClass="h-40 lg:h-48" />
                            <CircularProgress value={Math.round(sys.ram)} label="RAM Usage" color="#f59e0b" rotate="transform rotate-[45deg]" sizeClass="h-40 lg:h-48" />
                            <CircularProgress value={Math.round(sys.disk)} label="Disk Usage" color="#13a4ec" rotate="transform rotate-[10deg]" sizeClass="h-40 lg:h-48" />
                        </div>

                        {/* Traffic & Nodes Row */}
                        <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
                            {/* Traffic Chart */}
                            <div className="lg:col-span-2 bg-card-dark border border-card-border rounded-xl p-5 relative overflow-hidden flex flex-col justify-between min-h-[16rem]">
                                <div className="flex justify-between items-start mb-4 relative z-10">
                                    <div>
                                        <h3 className="text-sm font-bold text-muted uppercase tracking-wide">Network Traffic</h3>
                                        <div className="flex gap-4 mt-2">
                                            <span className="text-sm font-mono text-primary flex items-center bg-primary/10 px-2 py-1 rounded border border-primary/20">
                                                <span className="material-icons text-[12px] mr-2 rotate-180">arrow_downward</span>{networkDl} MB/s
                                            </span>
                                            <span className="text-sm font-mono text-accent-purple flex items-center bg-accent-purple/10 px-2 py-1 rounded border border-accent-purple/20">
                                                <span className="material-icons text-[12px] mr-2">arrow_upward</span>{networkUl} MB/s
                                            </span>
                                        </div>
                                    </div>
                                    <select className="bg-background-dark border border-card-border text-[10px] text-muted rounded px-2 py-1 outline-none">
                                        <option>Eth0</option>
                                        <option>Eth1</option>
                                    </select>
                                </div>

                                <div className="h-40 w-full mt-auto relative">
                                    <div className="absolute inset-0 bg-gradient-to-t from-background-dark/20 to-transparent z-0"></div>
                                    <svg className="w-full h-full overflow-visible" viewBox="0 0 400 120" preserveAspectRatio="none">
                                        <defs>
                                            <linearGradient id="gradientPrimary" x1="0%" x2="0%" y1="0%" y2="100%">
                                                <stop offset="0%" stopColor="#13a4ec" stopOpacity="0.4"></stop>
                                                <stop offset="100%" stopColor="#13a4ec" stopOpacity="0"></stop>
                                            </linearGradient>
                                            <linearGradient id="gradientUpload" x1="0%" x2="0%" y1="0%" y2="100%">
                                                <stop offset="0%" stopColor="#a855f7" stopOpacity="0.3"></stop>
                                                <stop offset="100%" stopColor="#a855f7" stopOpacity="0"></stop>
                                            </linearGradient>
                                        </defs>
                                        {/* Download & Upload lines from history */}
                                        {(() => {
                                            const dlPts = dlHistory.current.length > 0 ? dlHistory.current : [0];
                                            const ulPts = ulHistory.current.length > 0 ? ulHistory.current : [0];
                                            // Pad to 20 if fewer
                                            const pad = (arr: number[]) => {
                                                const padded = [...Array(Math.max(0, 20 - arr.length)).fill(0), ...arr];
                                                return padded;
                                            };
                                            const dl20 = pad(dlPts);
                                            const ul20 = pad(ulPts);
                                            const allMax = Math.max(...dl20, ...ul20, 0.1);
                                            // Map to SVG y coordinates (0 = top, 120 = bottom)
                                            const toY = (v: number) => 115 - (v / allMax) * 100;
                                            const step = 400 / 19;
                                            const makePath = (pts: number[]) => {
                                                const ys = pts.map(toY);
                                                let d = `M0,${ys[0]}`;
                                                for (let i = 1; i < ys.length; i++) {
                                                    const cx = (i - 0.5) * step;
                                                    const cy = (ys[i - 1] + ys[i]) / 2;
                                                    d += ` Q${cx},${cy} ${i * step},${ys[i]}`;
                                                }
                                                return d;
                                            };
                                            const dlPath = makePath(dl20);
                                            const ulPath = makePath(ul20);
                                            return (
                                                <>
                                                    <path d={dlPath} fill="none" stroke="#13a4ec" strokeWidth="2.5" className="drop-shadow-[0_0_8px_rgba(19,164,236,0.5)]" style={{ transition: 'all 0.8s ease-out' }} />
                                                    <path d={`${dlPath} V120 H0 Z`} fill="url(#gradientPrimary)" stroke="none" style={{ transition: 'all 0.8s ease-out' }} />
                                                    <path d={ulPath} fill="none" stroke="#a855f7" strokeWidth="1.5" strokeDasharray="6 3" opacity="0.7" style={{ transition: 'all 0.8s ease-out' }} />
                                                    <path d={`${ulPath} V120 H0 Z`} fill="url(#gradientUpload)" stroke="none" style={{ transition: 'all 0.8s ease-out' }} />
                                                </>
                                            );
                                        })()}
                                    </svg>
                                    <div className="absolute inset-0 border-t border-dashed border-card-border top-1/4"></div>
                                    <div className="absolute inset-0 border-t border-dashed border-card-border top-2/4"></div>
                                    <div className="absolute inset-0 border-t border-dashed border-card-border top-3/4"></div>
                                </div>
                            </div>

                            {/* Nodes Widget */}
                            <div className="lg:col-span-1 bg-card-dark border border-card-border rounded-xl p-5 flex flex-col justify-center items-center relative overflow-hidden group">
                                <div className="absolute top-0 right-0 p-4 opacity-5 group-hover:opacity-10 transition-opacity">
                                    <span className="material-icons text-9xl">dns</span>
                                </div>
                                <div className="w-16 h-16 rounded-full bg-primary/10 flex items-center justify-center mb-4 ring-1 ring-primary/30 group-hover:ring-primary/60 transition-all">
                                    <span className="material-icons text-primary text-3xl">cloud_queue</span>
                                </div>
                                <span className="text-5xl font-bold font-mono text-main tracking-tighter">{activeNodes}</span>
                                <span className="text-sm text-muted text-center uppercase tracking-widest mt-2 font-bold">Active Nodes</span>
                                <div className="flex gap-2 mt-6">
                                    <span className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse"></span>
                                    <span className="text-[10px] text-emerald-400">All Systems Normal</span>
                                </div>
                            </div>
                        </div>

                        {/* Containers Grid */}
                        <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
                            {containers.map((c, idx) => {
                                const sc = statusColor(c.status);
                                const isRunning = c.status === 'running';
                                const isStopped = c.status === 'stopped';
                                return (
                                    <div key={idx} onClick={() => onNavigate('nodes')} className={`bg-card-dark border border-card-border rounded-xl p-4 hover:bg-card-border/10 transition-colors cursor-pointer group flex flex-col justify-between ${c.status === 'high_load' ? 'ring-1 ring-accent-amber/20' : ''} ${isStopped ? 'opacity-70' : ''}`}>
                                        <div className="flex justify-between items-center mb-3">
                                            <div className="flex items-center gap-3">
                                                <div className={`relative w-2 h-2 rounded-full bg-${sc}-500 ${isRunning ? '' : ''}`}>
                                                    {isRunning && <div className={`absolute inset-0 rounded-full bg-${sc}-500 animate-ping opacity-75`}></div>}
                                                </div>
                                                <div>
                                                    <p className={`text-sm font-bold text-main group-hover:text-${isRunning ? 'primary' : 'main'} transition-colors`}>{c.id}</p>
                                                    <p className="text-[10px] font-mono text-muted">{c.name}</p>
                                                </div>
                                            </div>
                                            <span className={`text-[10px] font-bold text-${sc}-500 bg-${sc}-500/10 px-1.5 py-0.5 rounded border border-${sc}-500/20`}>{statusLabel(c.status)}</span>
                                        </div>
                                        <div className="space-y-2">
                                            <div>
                                                <div className="flex justify-between text-[10px] text-muted mb-1"><span>CPU</span><span className={c.status === 'high_load' ? 'text-accent-amber' : ''}>{c.cpu}%</span></div>
                                                <div className="w-full bg-card-border/30 h-1.5 rounded-full overflow-hidden"><div className={`bg-${isRunning ? 'primary' : sc === 'accent-amber' ? 'accent-amber' : 'slate-500'} h-1.5 rounded-full`} style={{ width: `${c.cpu}%` }}></div></div>
                                            </div>
                                            <div
                                                onMouseEnter={() => setHoveredRam(idx)}
                                                onMouseLeave={() => setHoveredRam(null)}>
                                                <div className="flex justify-between text-[10px] text-muted mb-1">
                                                    <span>RAM</span>
                                                    <span className={`transition-all ${c.status === 'high_load' ? 'text-accent-amber' : ''}`}>
                                                        {(displayPref === 'percent') !== (hoveredRam === idx)
                                                            ? `${c.ram}%`
                                                            : (c.ram_total_mb >= 1024 ? `${(c.ram_used_mb / 1024).toFixed(1)}G / ${(c.ram_total_mb / 1024).toFixed(1)}G` : `${Math.round(c.ram_used_mb)}M / ${Math.round(c.ram_total_mb)}M`)}
                                                    </span>
                                                </div>
                                                <div className="w-full bg-card-border/30 h-1.5 rounded-full overflow-hidden"><div className={`bg-${isRunning ? 'accent-purple' : sc === 'accent-amber' ? 'accent-amber' : 'slate-500'} h-1.5 rounded-full`} style={{ width: `${c.ram}%` }}></div></div>
                                            </div>
                                        </div>
                                    </div>
                                );
                            })}
                        </div>

                        {/* AI Insights */}
                        <div className="bg-[#0a0f12] border border-card-border rounded-xl p-6 shadow-2xl relative overflow-hidden mt-auto">
                            <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-primary via-accent-purple to-emerald-500 opacity-70"></div>
                            <div className="flex items-center gap-3 mb-4">
                                <div className="p-1.5 bg-primary/10 rounded-md">
                                    <span className="material-icons text-primary text-sm animate-pulse">terminal</span>
                                </div>
                                <h3 className="text-sm font-bold text-slate-200 uppercase tracking-widest">AI System Insights</h3>
                                <span className="ml-auto text-[10px] text-slate-500 font-mono">UPDATED: {insightsUpdatedAt}</span>
                            </div>
                            <div className="font-mono text-sm space-y-3 pl-2 border-l-2 border-white/5">
                                {insights.map((insight, idx) => (
                                    <p key={idx} className={`${insight.level === 'success' ? 'text-emerald-400' : insight.level === 'warning' ? 'text-accent-amber' : 'text-slate-400'} flex items-start gap-2`}>
                                        <span className="opacity-50 mt-1">&gt;</span>
                                        <span className={insight.level === 'success' ? 'text-slate-300' : ''}>{insight.message}</span>
                                    </p>
                                ))}
                                <div className="flex items-center gap-1 mt-3 text-primary opacity-70">
                                    <span className="w-2 h-4 bg-primary animate-pulse inline-block"></span>
                                </div>
                            </div>
                        </div>
                    </div>

                    {/* --- RIGHT COLUMN: ACADEMIC --- */}
                    <div className="xl:col-span-1 space-y-8 h-full flex flex-col">
                        <div className="hidden xl:flex items-center gap-2 border-b border-card-border pb-2">
                            <span className="material-icons text-muted">school</span>
                            <h2 className="text-lg font-semibold text-main">Academic Overview</h2>
                        </div>

                        {/* Calendar Strip */}
                        <div className="">
                            {(() => {
                                const now = new Date();
                                const dayLabels = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
                                const monthNames = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
                                const weekDates = Array.from({ length: 5 }, (_, i) => {
                                    const offset = i - 2; // keep today at the 3rd slot
                                    const d = new Date(now);
                                    d.setDate(now.getDate() + offset);
                                    const fullDate = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
                                    return { day: dayLabels[d.getDay()], date: String(d.getDate()), fullDate, active: offset === 0 };
                                });
                                return (
                                    <>
                                        <div className="flex justify-between items-center mb-5 px-1">
                                            <h2 className="text-sm font-medium text-muted uppercase tracking-wider">{monthNames[now.getMonth()]} {now.getFullYear()}</h2>
                                            <button
                                                onClick={() => {
                                                    const today = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
                                                    localStorage.setItem('jarvis_nav_date', today);
                                                    onNavigate('calendar');
                                                }}
                                                className="text-accent-orange text-xs font-mono border border-accent-orange/30 px-2 py-1 rounded hover:bg-accent-orange/10 transition-colors"
                                            >
                                                JUMP TO TODAY
                                            </button>
                                        </div>
                                        <div className="flex space-x-2 overflow-x-auto no-scrollbar py-1 xl:grid xl:grid-cols-5 xl:space-x-0 xl:gap-2">
                                            {weekDates.map((item, idx) => (
                                                <div
                                                    key={idx}
                                                    onClick={() => {
                                                        localStorage.setItem('jarvis_nav_date', item.fullDate);
                                                        onNavigate('calendar');
                                                    }}
                                                    className={`flex flex-col items-center justify-center min-w-[3.5rem] h-16 rounded-lg border transition-colors cursor-pointer ${item.active
                                                        ? 'bg-slate-200 text-bg-base border-transparent shadow-sm'
                                                        : 'bg-card-dark border-card-border text-muted hover:border-muted/20'
                                                        }`}
                                                >
                                                    <span className="text-[10px] uppercase font-bold tracking-wider mb-1">{item.day}</span>
                                                    <span className={`text-sm font-mono ${item.active ? 'font-bold' : ''}`}>{item.date}</span>
                                                </div>
                                            ))}
                                        </div>
                                    </>
                                );
                            })()}
                        </div>

                        {/* Notice Scout */}
                        <div className="border-t border-card-border pt-6">
                            <div className="flex items-center justify-between mb-4">
                                <div className="flex items-center space-x-2">
                                    <span className="material-icons text-main text-sm">radar</span>
                                    <h2 className="text-base font-semibold text-main">Notice Scout</h2>
                                </div>
                                <span className="text-[10px] font-mono px-2 py-0.5 rounded bg-bg-highlight text-muted border border-card-border">AI CURATED</span>
                            </div>
                            <div className="flex space-x-4 overflow-x-auto no-scrollbar pb-2 xl:flex-col xl:space-x-0 xl:space-y-4 xl:overflow-visible">
                                {notices.map((notice, idx) => (
                                    <div key={idx} className="min-w-[280px] p-5 rounded-xl bg-card-dark border border-card-border relative group hover:border-muted/20 transition-all cursor-pointer">
                                        <div className="absolute top-4 right-4">
                                            <div className={`flex items-center space-x-1 ${notice.match >= 90 ? 'text-accent-orange' : 'text-muted'} text-[10px] font-mono border ${notice.match >= 90 ? 'border-accent-orange/30' : 'border-card-border'} px-2 py-1 rounded-md ${notice.match >= 90 ? 'bg-accent-orange/5' : 'bg-card-border/10'}`}>
                                                <span className="material-icons text-[10px]">{notice.match >= 90 ? 'check_circle' : 'data_usage'}</span>
                                                <span>{notice.match}% MATCH</span>
                                            </div>
                                        </div>
                                        <div className="mt-8 mb-4">
                                            <h3 className="text-base font-semibold text-main mb-2">{notice.title}</h3>
                                            <p className="text-xs text-muted leading-relaxed">{notice.description}</p>
                                        </div>
                                        <div className="flex items-center justify-between pt-4 border-t border-card-border">
                                            <span className="text-[10px] font-mono text-muted">{notice.date}</span>
                                            <button className="text-xs font-medium text-main hover:text-accent-orange transition-colors flex items-center gap-1 group-hover:underline decoration-muted underline-offset-4">
                                                View Details <span className="material-icons text-[10px]">arrow_forward</span>
                                            </button>
                                        </div>
                                    </div>
                                ))}
                            </div>
                        </div>

                        {/* Priorities */}
                        <div className="">
                            <h2 className="text-base font-semibold mb-4 text-main">Priorities</h2>
                            <div className="space-y-3">
                                {priorities.map((p, idx) => (
                                    <div key={p.id || idx} className={`rounded-xl bg-card-dark border border-card-border overflow-hidden transition-opacity ${p.done ? 'opacity-60' : ''}`}>
                                        <div className="p-4 flex items-start gap-3">
                                            <div className="mt-0.5">
                                                <input
                                                    className={`w-4 h-4 rounded-sm border-slate-600 ${p.tag === 'URGENT' ? 'text-accent-orange' : p.done ? 'text-muted' : 'text-main'} focus:ring-0 focus:ring-offset-0 bg-background-dark cursor-pointer`}
                                                    type="checkbox"
                                                    checked={p.done}
                                                    onChange={() => handleToggleDone(p.id)}
                                                />
                                            </div>
                                            <div className="flex-1">
                                                <div className="flex justify-between items-start mb-1">
                                                    <h3 className={`text-sm font-medium ${p.done ? 'text-muted line-through' : 'text-main'} leading-tight`}>{p.title}</h3>
                                                    <span className={`text-[10px] font-mono ${p.tag === 'URGENT' ? 'text-accent-orange px-1.5 py-0.5 rounded border border-accent-orange/30 bg-accent-orange/5' : p.tag === 'DONE' ? 'text-muted px-1.5 py-0.5 rounded border border-card-border bg-background-dark' : 'text-muted px-1.5 py-0.5 rounded border border-card-border bg-background-dark'}`}>{p.tag}</span>
                                                </div>
                                                <p className={`text-xs ${p.done ? 'text-slate-600' : 'text-muted'} font-mono`}>{p.due}{p.course ? ` ??${p.course}` : ''}</p>
                                            </div>
                                        </div>
                                        {summaryResult?.id === p.id && (
                                            <div className="bg-primary/5 px-4 py-3 border-t border-primary/20">
                                                <p className="text-xs text-slate-300 whitespace-pre-wrap leading-relaxed">{summaryResult.text}</p>
                                            </div>
                                        )}
                                        {p.ai_action && !p.done && (
                                            <div className="bg-white/5 px-4 py-2 border-t border-card-border flex items-center justify-end">
                                                <button
                                                    onClick={() => handleSummarize(p.id)}
                                                    disabled={summarizing === p.id}
                                                    className={`flex items-center space-x-2 text-muted hover:text-main transition-colors text-xs group ${summarizing === p.id ? 'opacity-50 cursor-wait' : ''}`}
                                                >
                                                    <span className={`material-icons text-[14px] ${summarizing === p.id ? 'animate-spin' : ''}`}>{summarizing === p.id ? 'sync' : (p.ai_action.includes('Summarize') ? 'auto_awesome' : 'flash_on')}</span>
                                                    <span>{summarizing === p.id ? 'Analyzing...' : p.ai_action}</span>
                                                </button>
                                            </div>
                                        )}
                                    </div>
                                ))}
                            </div>
                        </div>
                    </div>

                </div>
            )}

            <button
                onClick={() => onNavigate('calendar')}
                className="hidden md:flex fixed bottom-6 right-6 w-14 h-14 bg-primary text-white rounded-full shadow-lg shadow-primary/30 flex items-center justify-center hover:scale-105 transition-transform z-30 ring-4 ring-background-dark"
            >
                <span className="material-icons text-2xl">add</span>
            </button>
        </div>
    );
};
