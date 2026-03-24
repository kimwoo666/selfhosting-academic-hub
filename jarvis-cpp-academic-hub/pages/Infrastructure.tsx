import React, { useState, useEffect } from 'react';

interface SummaryData {
    system: { cpu: number; ram: number; disk: number; temp: number };
    uptime: string;
    network: { download_mbps: number; upload_mbps: number };
    active_nodes: number;
    containers: Array<{ id: string; name: string; status: string; cpu: number; ram: number }>;
    ai_insights: Array<{ level: string; message: string }>;
}

const CircularProgress: React.FC<{ value: number, label: string, color: string, rotate?: string }> = ({ value, label, color, rotate }) => (
    <div className="bg-card-dark border border-card-border rounded-xl p-3 flex flex-col items-center justify-center relative overflow-hidden group h-32">
        <div className={`absolute inset-0 opacity-0 group-hover:opacity-100 transition-opacity bg-${color}/5`}></div>
        <div className="relative w-16 h-8 overflow-hidden mb-2">
            <div className="absolute top-0 left-0 w-16 h-16 rounded-full border-4 border-card-border/50"></div>
            <div
                className={`absolute top-0 left-0 w-16 h-16 rounded-full border-4 border-b-transparent border-l-transparent border-r-transparent ${rotate}`}
                style={{ borderColor: color, clipPath: 'polygon(0 0, 100% 0, 100% 50%, 0 50%)' }}
            ></div>
        </div>
        <span className="text-2xl font-bold text-main">{value}<span className="text-xs text-muted">%</span></span>
        <span className="text-[10px] uppercase tracking-wider font-semibold text-muted mt-1">{label}</span>
    </div>
);

export const Infrastructure: React.FC = () => {
    const [data, setData] = useState<SummaryData | null>(null);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        fetch('/api/summary')
            .then(res => res.json())
            .then((d: SummaryData) => { setData(d); setLoading(false); })
            .catch(err => { console.error('Failed to fetch summary:', err); setLoading(false); });
    }, []);

    const sys = data?.system ?? { cpu: 0, ram: 0, disk: 0, temp: 0 };
    const uptime = data?.uptime ?? '--';
    const networkDl = data?.network?.download_mbps ?? 0;
    const networkUl = data?.network?.upload_mbps ?? 0;
    const activeNodes = data?.active_nodes ?? 0;
    const containers = data?.containers ?? [];
    const insights = data?.ai_insights ?? [];

    const statusColor = (s: string) => {
        if (s === 'running') return 'emerald';
        if (s === 'high_load') return 'accent-amber';
        return 'slate';
    };
    const statusLabel = (s: string) => {
        if (s === 'running') return 'Running';
        if (s === 'high_load') return 'High Load';
        return 'Stopped';
    };

    return (
        <div className="flex-1 flex flex-col h-full bg-background-dark overflow-y-auto">
            <header className="pt-8 pb-4 px-6 flex justify-between items-center bg-background-dark/90 backdrop-blur-md sticky top-0 z-20 border-b border-card-border">
                <div className="text-left pl-8 lg:pl-0">
                    <h2 className="text-[10px] font-bold text-primary uppercase tracking-widest mb-0.5">Infrastructure</h2>
                    <h1 className="text-xl font-bold text-main leading-none">Jarvis-Cpp Control</h1>
                </div>
                <button className="p-2 -mr-2 rounded-full hover:bg-card-border transition-colors relative">
                    <span className="absolute top-2 right-2 w-2 h-2 bg-red-500 rounded-full border border-background-dark"></span>
                    <span className="material-icons text-muted">notifications</span>
                </button>
            </header>

            {loading ? (
                <div className="flex-1 flex items-center justify-center">
                    <span className="text-muted text-sm font-mono animate-pulse">Loading infrastructure...</span>
                </div>
            ) : (
                <div className="p-4 space-y-4 pb-20">
                    {/* Status Bar */}
                    <div className="flex items-center justify-between px-2 mb-2">
                        <div className="flex items-center gap-2">
                            <span className="relative flex h-3 w-3">
                                <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>
                                <span className="relative inline-flex rounded-full h-3 w-3 bg-emerald-500"></span>
                            </span>
                            <span className="text-sm font-medium text-emerald-400">System Online</span>
                        </div>
                        <span className="text-xs text-muted font-mono">Uptime: {uptime}</span>
                    </div>

                    {/* Gauges */}
                    <div className="grid grid-cols-3 gap-3">
                        <CircularProgress value={Math.round(sys.cpu)} label="CPU" color="#10b981" rotate="transform -rotate-45" />
                        <CircularProgress value={Math.round(sys.ram)} label="RAM" color="#f59e0b" rotate="transform rotate-[45deg]" />
                        <CircularProgress value={Math.round(sys.disk)} label="DISK" color="#13a4ec" rotate="transform rotate-[10deg]" />
                    </div>

                    {/* Traffic & Nodes */}
                    <div className="grid grid-cols-12 gap-3">
                        <div className="col-span-8 bg-card-dark border border-card-border rounded-xl p-4 relative overflow-hidden">
                            <div className="flex justify-between items-start mb-2">
                                <div>
                                    <h3 className="text-xs font-semibold text-muted uppercase">Network Traffic</h3>
                                    <div className="flex gap-3 mt-1">
                                        <span className="text-xs font-mono text-primary flex items-center"><span className="material-icons text-[10px] mr-1 rotate-180">arrow_downward</span>{networkDl} MB/s</span>
                                        <span className="text-xs font-mono text-accent-purple flex items-center"><span className="material-icons text-[10px] mr-1">arrow_upward</span>{networkUl} MB/s</span>
                                    </div>
                                </div>
                            </div>
                            <div className="h-12 w-full mt-2">
                                <svg className="w-full h-full overflow-visible" viewBox="0 0 200 50">
                                    <defs>
                                        <linearGradient id="gradientPrimary2" x1="0%" x2="0%" y1="0%" y2="100%">
                                            <stop offset="0%" stopColor="#13a4ec" stopOpacity="0.3"></stop>
                                            <stop offset="100%" stopColor="#13a4ec" stopOpacity="0"></stop>
                                        </linearGradient>
                                    </defs>
                                    <path d="M0,40 Q10,35 20,42 T40,30 T60,35 T80,20 T100,25 T120,15 T140,20 T160,10 T180,15 T200,5" fill="none" stroke="#13a4ec" strokeWidth="2"></path>
                                    <path d="M0,40 Q10,35 20,42 T40,30 T60,35 T80,20 T100,25 T120,15 T140,20 T160,10 T180,15 T200,5 V50 H0 Z" fill="url(#gradientPrimary2)" stroke="none"></path>
                                </svg>
                            </div>
                        </div>
                        <div className="col-span-4 bg-card-dark border border-card-border rounded-xl p-3 flex flex-col justify-center items-center">
                            <div className="w-8 h-8 rounded-full bg-primary/20 flex items-center justify-center mb-2">
                                <span className="material-icons text-primary text-sm">cloud_queue</span>
                            </div>
                            <span className="text-2xl font-bold font-mono text-main">{activeNodes}</span>
                            <span className="text-[10px] text-muted text-center leading-tight">Active Nodes</span>
                        </div>
                    </div>

                    {/* Containers List */}
                    <div className="bg-card-dark border border-card-border rounded-xl overflow-hidden flex flex-col">
                        <div className="p-4 border-b border-card-border flex justify-between items-center">
                            <h3 className="text-sm font-semibold text-main">Container Instances</h3>
                            <span className="text-xs bg-card-border/10 text-muted px-2 py-1 rounded border border-card-border/20">LXC/VM</span>
                        </div>
                        <div className="divide-y divide-card-border">
                            {containers.map((c, idx) => {
                                const sc = statusColor(c.status);
                                const isStopped = c.status === 'stopped';
                                return (
                                    <div key={idx} className={`p-4 hover:bg-card-border/10 transition-colors cursor-pointer group ${isStopped ? 'opacity-75' : ''}`}>
                                        <div className="flex justify-between items-center mb-2">
                                            <div className="flex items-center gap-3">
                                                <div className={`relative w-2 h-2 rounded-full bg-${sc}-500 ${c.status === 'running' ? '' : c.status === 'high_load' ? 'animate-pulse' : ''}`}>
                                                    {c.status === 'running' && <div className={`absolute inset-0 rounded-full bg-${sc}-500 animate-ping opacity-75`}></div>}
                                                </div>
                                                <div>
                                                    <p className="text-sm font-semibold text-main">{c.id} ({c.name})</p>
                                                </div>
                                            </div>
                                            <span className={`text-xs font-medium text-${sc}-500 bg-${sc}-500/10 px-2 py-0.5 rounded border border-${sc}-500/20`}>{statusLabel(c.status)}</span>
                                        </div>
                                        <div className="grid grid-cols-2 gap-4 mt-2">
                                            <div>
                                                <div className="flex justify-between text-[10px] text-muted mb-1"><span>CPU</span><span className={c.status === 'high_load' ? 'text-accent-amber' : ''}>{c.cpu}%</span></div>
                                                <div className="w-full bg-card-border/30 h-1 rounded-full overflow-hidden"><div className={`bg-${c.status === 'high_load' ? 'accent-amber' : 'primary'} h-1 rounded-full`} style={{ width: `${c.cpu}%` }}></div></div>
                                            </div>
                                            <div>
                                                <div className="flex justify-between text-[10px] text-muted mb-1"><span>RAM</span><span className={c.status === 'high_load' ? 'text-accent-amber' : ''}>{c.ram}%</span></div>
                                                <div className="w-full bg-card-border/30 h-1 rounded-full overflow-hidden"><div className={`bg-${c.status === 'high_load' ? 'accent-amber' : 'accent-purple'} h-1 rounded-full`} style={{ width: `${c.ram}%` }}></div></div>
                                            </div>
                                        </div>
                                    </div>
                                );
                            })}
                        </div>
                    </div>

                    {/* AI Insights */}
                    <div className="bg-[#0a0f12] border border-card-border rounded-xl p-4 shadow-inner relative overflow-hidden">
                        <div className="absolute top-0 left-0 w-full h-1 bg-gradient-to-r from-primary via-accent-purple to-emerald-500 opacity-50"></div>
                        <div className="flex items-center gap-2 mb-3">
                            <span className="material-icons text-primary text-sm animate-pulse">terminal</span>
                            <h3 className="text-xs font-bold text-muted uppercase tracking-widest">AI Insights</h3>
                        </div>
                        <div className="font-mono text-xs space-y-2">
                            {insights.map((insight, idx) => (
                                <p key={idx} className={`${insight.level === 'success' ? 'text-emerald-400' : insight.level === 'warning' ? 'text-accent-amber' : 'text-muted'}`}>
                                    &gt; {insight.level === 'success' ? <span className="text-emerald-300">{insight.message}</span> : insight.message}
                                </p>
                            ))}
                            <div className="flex items-center gap-1 mt-2 text-primary opacity-70">
                                <span className="w-1.5 h-3 bg-primary animate-pulse inline-block"></span>
                            </div>
                        </div>
                    </div>
                </div>
            )}
        </div>
    );
};