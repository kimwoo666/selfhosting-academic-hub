import React, { useState, useEffect, useRef } from 'react';

interface NodeData {
    id: number;
    name: string;
    status: string;
    ip: string;
    os: string;
    cpu: number;
    ram: string;
    disk: string;
    cpu_pct: number;
    ram_pct: number;
    disk_pct: number;
}

export const Nodes: React.FC = () => {
    const [nodes, setNodes] = useState<NodeData[]>([]);
    const [loading, setLoading] = useState(true);
    const [deploying, setDeploying] = useState(false);
    const [controlMsg, setControlMsg] = useState('');
    const [controlling, setControlling] = useState<number | null>(null);
    const [hoveredCell, setHoveredCell] = useState<string | null>(null);
    const nodeOrderRef = useRef<number[]>([]);
    const nodeHistoryRef = useRef<Record<number, { cpu: number[]; ram: number[] }>>({});

    // Display preference: 'percent' = show % by default, hover shows absolute
    //                     'absolute' = show absolute by default, hover shows %
    const displayPref = typeof window !== 'undefined'
        ? (localStorage.getItem('jarvis_resource_display') || 'percent')
        : 'percent';

    const toNumber = (v: unknown) => {
        const n = typeof v === 'number' ? v : Number(v);
        return Number.isFinite(n) ? n : 0;
    };

    const normalizeNode = (n: any): NodeData => ({
        ...n,
        cpu: toNumber(n?.cpu),
        cpu_pct: toNumber(n?.cpu_pct),
        ram_pct: toNumber(n?.ram_pct),
        disk_pct: toNumber(n?.disk_pct),
    });

    const applyStableOrder = (incoming: NodeData[]) => {
        if (nodeOrderRef.current.length === 0) {
            nodeOrderRef.current = incoming.map(n => n.id);
        } else {
            const known = new Set(nodeOrderRef.current);
            for (const n of incoming) {
                if (!known.has(n.id)) {
                    nodeOrderRef.current.push(n.id);
                    known.add(n.id);
                }
            }
        }

        const byId = new Map(incoming.map(n => [n.id, n] as const));
        return nodeOrderRef.current
            .map(id => byId.get(id))
            .filter((n): n is NodeData => Boolean(n));
    };

    const updateNodeHistory = (incoming: NodeData[]) => {
        const nextHistory: Record<number, { cpu: number[]; ram: number[] }> = {};

        incoming.forEach((node) => {
            const prev = nodeHistoryRef.current[node.id] ?? { cpu: [], ram: [] };
            nextHistory[node.id] = {
                cpu: [...prev.cpu, toNumber(node.cpu_pct)].slice(-20),
                ram: [...prev.ram, toNumber(node.ram_pct)].slice(-20),
            };
        });

        nodeHistoryRef.current = nextHistory;
    };

    const fetchNodes = () => {
        fetch('/api/nodes')
            .then(res => res.json())
            .then((d: NodeData[]) => {
                const normalized = Array.isArray(d) ? d.map(normalizeNode) : [];
                updateNodeHistory(normalized);
                setNodes(applyStableOrder(normalized));
                setLoading(false);
            })
            .catch(err => { console.error('Failed to fetch nodes:', err); setLoading(false); });
    };

    useEffect(() => {
        fetchNodes();
        const interval = setInterval(fetchNodes, 5000);
        return () => clearInterval(interval);
    }, []);

    const buildMetricPath = (samples: number[]) => {
        const padded = samples.length >= 2 ? samples : [samples[0] ?? 0, samples[0] ?? 0];
        const width = 200;
        const maxIndex = Math.max(1, padded.length - 1);
        const step = width / maxIndex;
        const toY = (value: number) => {
            const clamped = Math.max(0, Math.min(100, value));
            return 74 - (clamped / 100) * 64;
        };

        let d = `M0,${toY(padded[0])}`;
        for (let i = 1; i < padded.length; i++) {
            const prevX = (i - 1) * step;
            const prevY = toY(padded[i - 1]);
            const x = i * step;
            const y = toY(padded[i]);
            const cx = prevX + step / 2;
            d += ` C${cx},${prevY} ${cx},${y} ${x},${y}`;
        }
        return d;
    };

    const handleControl = async (nodeId: number, action: string) => {
        setControlling(nodeId);
        try {
            const res = await fetch(`/api/nodes/${nodeId}/control`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ action }),
            });
            const data = await res.json();
            setControlMsg(data.message);
            setTimeout(() => setControlMsg(''), 4000);
            // Refresh node list after a brief delay for Proxmox to process
            setTimeout(() => fetchNodes(), 1500);
        } catch (err) {
            console.error('Control action failed:', err);
            setControlMsg('Control action failed. Check console.');
            setTimeout(() => setControlMsg(''), 4000);
        } finally {
            setTimeout(() => setControlling(null), 1500);
        }
    };

    const handleDeploy = async () => {
        setDeploying(true);
        try {
            const res = await fetch('/api/nodes/deploy', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ name: 'jarvis-node' }),
            });
            const text = await res.text();
            let data: any = {};
            try {
                data = text ? JSON.parse(text) : {};
            } catch {
                data = {};
            }

            const ok = res.ok && Boolean(data?.success);
            setControlMsg(data.message || data.error || (ok ? 'Deploy started' : `Deploy failed (HTTP ${res.status})`));
            setTimeout(() => setControlMsg(''), 5000);
            if (ok) {
                // Give Proxmox a moment to register the cloned node.
                setTimeout(() => fetchNodes(), 2500);
            }
        } catch (err) {
            console.error('Deploy failed:', err);
            setControlMsg('Deploy action failed. Check server logs.');
            setTimeout(() => setControlMsg(''), 5000);
        } finally {
            setDeploying(false);
        }
    };

    const statusBadge = (status: string) => {
        if (status === 'running') return { color: 'emerald', label: 'Running', icon: '' };
        if (status === 'high_load') return { color: 'accent-amber', label: 'High Load', icon: 'warning' };
        return { color: 'slate', label: 'Stopped', icon: '' };
    };

    const runningNodes = nodes.filter(n => n.status !== 'stopped');
    const totalLoad = runningNodes.length > 0
        ? Math.round(runningNodes.reduce((a, n) => a + toNumber(n.cpu), 0) / runningNodes.length)
        : 0;

    return (
        <div className="flex-1 flex flex-col h-full bg-background-dark overflow-y-auto w-full">
            <header className="pt-8 pb-4 px-6 flex justify-between items-center bg-background-dark/90 backdrop-blur-md sticky top-0 z-20 border-b border-card-border">
                <div className="text-left pl-8 lg:pl-0">
                    <h2 className="text-[10px] font-bold text-primary uppercase tracking-widest mb-0.5">Infrastructure</h2>
                    <h1 className="text-xl font-bold text-main leading-none">Nodes Management</h1>
                </div>
                <button className="p-2 -mr-2 rounded-full hover:bg-card-border transition-colors relative">
                    <span className="absolute top-2 right-2 w-2 h-2 bg-red-500 rounded-full border border-background-dark"></span>
                    <span className="material-icons text-muted">notifications</span>
                </button>
            </header>

            {controlMsg && (
                <div className="mx-4 mt-2 px-4 py-2 bg-emerald-500/10 border border-emerald-500/30 rounded-lg text-sm text-emerald-400 font-mono animate-[fadeIn_0.3s]">
                    {controlMsg}
                </div>
            )}

            <div className="p-4 space-y-4 pb-32">
                {/* Stats */}
                <div className="grid grid-cols-2 gap-3 mb-2">
                    <div className="bg-card-dark border border-card-border rounded-xl p-3 flex items-center justify-between">
                        <div>
                            <span className="text-[10px] uppercase text-muted font-semibold block">Active Nodes</span>
                            <span className="text-2xl font-bold text-main">{nodes.filter(n => n.status !== 'stopped').length}<span className="text-sm font-normal text-muted">/{nodes.length}</span></span>
                        </div>
                        <div className="h-8 w-8 rounded-full bg-emerald-500/10 flex items-center justify-center">
                            <span className="material-icons text-emerald-500 text-sm">dns</span>
                        </div>
                    </div>
                    <div className="bg-card-dark border border-card-border rounded-xl p-3 flex items-center justify-between">
                        <div>
                            <span className="text-[10px] uppercase text-muted font-semibold block">Total Load</span>
                            <span className="text-2xl font-bold text-main">{totalLoad}%</span>
                        </div>
                        <div className="h-8 w-8 rounded-full bg-primary/10 flex items-center justify-center">
                            <span className="material-icons text-primary text-sm">show_chart</span>
                        </div>
                    </div>
                </div>

                {loading ? (
                    <div className="flex items-center justify-center py-12">
                        <span className="text-muted text-sm font-mono animate-pulse">Loading nodes...</span>
                    </div>
                ) : (
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                        {nodes.map((node) => {
                            const badge = statusBadge(node.status);
                            const isHighLoad = node.status === 'high_load';
                            const isRunning = node.status === 'running';
                            const isStopped = node.status === 'stopped';
                            const history = nodeHistoryRef.current[node.id] ?? {
                                cpu: [node.cpu_pct],
                                ram: [node.ram_pct],
                            };
                            const cpuPath = buildMetricPath(history.cpu);
                            const ramPath = buildMetricPath(history.ram);

                            return (
                                <div key={node.id} className={`bg-card-dark border border-card-border rounded-xl p-4 relative overflow-hidden group shadow-lg ${isHighLoad ? 'ring-1 ring-accent-amber/30' : ''}`}>
                                    <div className="flex justify-between items-start mb-4 relative">
                                        <div className="flex items-center gap-3">
                                            <div className={`w-10 h-10 rounded-lg ${isHighLoad ? 'bg-accent-amber/10 border-accent-amber/20' : 'bg-card-border'} flex items-center justify-center border-0`}>
                                                <span className={`material-symbols-outlined ${isHighLoad ? 'text-accent-amber' : 'text-muted'}`}>{isHighLoad ? 'memory' : 'terminal'}</span>
                                            </div>
                                            <div>
                                                <h3 className="text-base font-bold text-main">{node.name}</h3>
                                                <div className="flex items-center gap-2">
                                                    <span className="text-[10px] font-mono text-muted">{node.ip}</span>
                                                    <span className="w-1 h-1 rounded-full bg-muted"></span>
                                                    <span className="text-[10px] text-muted uppercase">{node.os}</span>
                                                </div>
                                            </div>
                                        </div>
                                        <span className={`inline-flex items-center px-2 py-1 rounded text-[10px] font-medium bg-${badge.color}-500/10 text-${badge.color}-500 border border-${badge.color}-500/20`}>
                                            {badge.icon && <span className="material-icons text-[10px] mr-1">{badge.icon}</span>}
                                            {!badge.icon && isRunning && <span className={`w-1.5 h-1.5 rounded-full bg-${badge.color}-500 mr-1.5 animate-pulse`}></span>}
                                            {badge.label}
                                        </span>
                                    </div>
                                    <div className="mb-4 relative h-24">
                                        <div className="flex items-center justify-between">
                                            <span className="text-[10px] text-muted uppercase font-semibold">CPU Load</span>
                                            <span className="text-[9px] text-muted font-mono">{new Date().toLocaleTimeString('ko-KR', { hour: '2-digit', minute: '2-digit' })}</span>
                                        </div>
                                        <svg className="w-full h-full" preserveAspectRatio="none" viewBox="0 0 200 80">
                                            <defs>
                                                <linearGradient id={`grad-${node.id}`} x1="0%" x2="0%" y1="0%" y2="100%">
                                                    <stop offset="0%" stopColor={isHighLoad ? '#f59e0b' : '#10b981'} stopOpacity="0.2"></stop>
                                                    <stop offset="100%" stopColor={isHighLoad ? '#f59e0b' : '#10b981'} stopOpacity="0"></stop>
                                                </linearGradient>
                                            </defs>
                                            <path d={cpuPath} fill="none" stroke={isHighLoad ? '#f59e0b' : '#10b981'} strokeWidth="2.25"></path>
                                            <path d={`${cpuPath} V80 H0 Z`} fill={`url(#grad-${node.id})`} stroke="none"></path>
                                            <path d={ramPath} fill="none" stroke="#a855f7" strokeWidth="1.25" strokeDasharray="5 4" opacity="0.8"></path>
                                        </svg>
                                    </div>
                                    <div className="grid grid-cols-3 gap-2 mb-4 border-t border-card-border py-3">
                                        <div className="text-center">
                                            <span className="block text-[10px] text-muted mb-0.5">CPU</span>
                                            <span className={`block text-sm font-mono font-bold ${isHighLoad ? 'text-accent-amber' : 'text-muted'}`}>{node.cpu}%</span>
                                        </div>
                                        <div className="text-center border-l border-r border-card-border cursor-default"
                                            onMouseEnter={() => setHoveredCell(`${node.id}-ram`)}
                                            onMouseLeave={() => setHoveredCell(null)}>
                                            <span className="block text-[10px] text-muted mb-0.5">RAM</span>
                                            <span className={`block text-xs font-mono font-bold transition-all ${isHighLoad ? 'text-accent-amber' : 'text-accent-purple'}`}>
                                                {(displayPref === 'percent') !== (hoveredCell === `${node.id}-ram`)
                                                    ? `${node.ram_pct}%`
                                                    : node.ram}
                                            </span>
                                        </div>
                                        <div className="text-center cursor-default"
                                            onMouseEnter={() => setHoveredCell(`${node.id}-disk`)}
                                            onMouseLeave={() => setHoveredCell(null)}>
                                            <span className="block text-[10px] text-muted mb-0.5">DISK</span>
                                            <span className="block text-xs font-mono font-bold text-muted transition-all">
                                                {(displayPref === 'percent') !== (hoveredCell === `${node.id}-disk`)
                                                    ? `${node.disk_pct}%`
                                                    : node.disk}
                                            </span>
                                        </div>
                                    </div>
                                    <div className="grid grid-cols-3 gap-2">
                                        <button disabled={controlling === node.id || !isStopped} onClick={() => handleControl(node.id, 'start')} className={`flex items-center justify-center px-3 py-2 rounded bg-card-border/50 ${isStopped && controlling !== node.id ? 'hover:bg-emerald-500/10 hover:text-emerald-500 text-muted' : 'text-muted/50 cursor-not-allowed'} transition-colors`}>
                                            <span className="material-icons text-sm">{controlling === node.id ? 'hourglass_empty' : 'play_arrow'}</span>
                                            <span className="ml-1 text-xs font-medium">Start</span>
                                        </button>
                                        <button disabled={controlling === node.id || isStopped} onClick={() => handleControl(node.id, 'restart')} className={`flex items-center justify-center px-3 py-2 rounded bg-card-border/50 ${!isStopped && controlling !== node.id ? 'hover:bg-primary/10 hover:text-primary text-muted' : 'text-muted/50 cursor-not-allowed'} transition-colors`}>
                                            <span className="material-icons text-sm">{controlling === node.id ? 'hourglass_empty' : 'restart_alt'}</span>
                                            <span className="ml-1 text-xs font-medium">Reboot</span>
                                        </button>
                                        <button disabled={controlling === node.id || isStopped} onClick={() => handleControl(node.id, 'stop')} className={`flex items-center justify-center px-3 py-2 rounded bg-card-border/50 ${!isStopped && controlling !== node.id ? 'hover:bg-red-500/10 hover:text-red-500 text-muted' : 'text-muted/50 cursor-not-allowed'} transition-colors`}>
                                            <span className="material-icons text-sm">{controlling === node.id ? 'hourglass_empty' : 'stop'}</span>
                                            <span className="ml-1 text-xs font-medium">Stop</span>
                                        </button>
                                    </div>
                                </div>
                            );
                        })}
                    </div>
                )}
            </div>

            <div className="sticky bottom-0 z-40 bg-background-dark/95 backdrop-blur-lg border-t border-card-border p-4">
                <button
                    onClick={handleDeploy}
                    disabled={deploying}
                    className="w-full bg-primary hover:bg-primary/90 disabled:opacity-60 disabled:cursor-not-allowed text-white font-medium py-3 rounded-lg flex items-center justify-center gap-2 shadow-lg shadow-primary/20 transition-all"
                >
                    <span className={`material-icons ${deploying ? 'animate-spin' : ''}`}>
                        {deploying ? 'sync' : 'add'}
                    </span>
                    Deploy New Node
                </button>
            </div>
        </div>
    );
};
