import React, { useEffect, useState } from 'react';

interface AlertData {
    severity: string;
    category: string;
    title: string;
    message: string;
    time: string;
    color: string;
}

type FilterTab = 'all' | 'Academic' | 'Scholarship' | 'Competition' | 'System';

export const Alerts: React.FC = () => {
    const [alerts, setAlerts] = useState<AlertData[]>([]);
    const [loading, setLoading] = useState(true);
    const [crawling, setCrawling] = useState(false);
    const [activeFilter, setActiveFilter] = useState<FilterTab>('all');

    const fetchAlerts = () => {
        setLoading(true);
        fetch('/api/alerts')
            .then(res => res.json())
            .then((d: AlertData[]) => {
                setAlerts(Array.isArray(d) ? d : []);
                setLoading(false);
            })
            .catch(err => {
                console.error('Failed to fetch alerts:', err);
                setLoading(false);
            });
    };

    useEffect(() => {
        fetchAlerts();
    }, []);

    const handleCrawl = async () => {
        setCrawling(true);
        try {
            const res = await fetch('/api/notices/crawl', { method: 'POST' });
            const data = await res.json();
            console.log('Crawl result:', data);
            fetchAlerts();
        } catch (err) {
            console.error('Crawl failed:', err);
        }
        setCrawling(false);
    };

    const colorMap: Record<string, string> = {
        red: 'red',
        primary: 'primary',
        amber: 'amber',
    };

    const filterTabs: { key: FilterTab; label: string; icon: string }[] = [
        { key: 'all', label: 'All', icon: 'dashboard' },
        { key: 'Academic', label: 'Academic', icon: 'school' },
        { key: 'Scholarship', label: 'Scholarship', icon: 'payments' },
        { key: 'Competition', label: 'Competition', icon: 'emoji_events' },
        { key: 'System', label: 'System', icon: 'memory' },
    ];

    const filteredAlerts = activeFilter === 'all'
        ? alerts
        : alerts.filter(a => a.category === activeFilter);

    const getNoticeSource = (message: string) =>
        ['AIX', 'SW', 'u-SAINT'].find(source => message.startsWith(`[${source}]`)) || null;

    return (
        <div className="flex-1 flex flex-col h-full bg-background-light dark:bg-background-dark overflow-y-auto">
            <header className="pt-8 pb-4 px-6 flex justify-between items-center bg-background-dark/90 backdrop-blur-md sticky top-0 z-20 border-b border-card-border">
                <div className="text-left pl-8 lg:pl-0">
                    <h2 className="text-[10px] font-bold text-primary uppercase tracking-widest mb-0.5">Notifications</h2>
                    <h1 className="text-xl font-bold text-white leading-none">Academic Hub Alerts</h1>
                </div>
                <div className="flex items-center gap-2">
                    <button
                        onClick={handleCrawl}
                        disabled={crawling}
                        className={`flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all
                            ${crawling
                                ? 'bg-primary/20 text-primary/60 cursor-wait'
                                : 'bg-primary/10 text-primary hover:bg-primary/20 border border-primary/30'
                            }`}
                    >
                        <span className={`material-icons text-sm ${crawling ? 'animate-spin' : ''}`}>
                            {crawling ? 'sync' : 'refresh'}
                        </span>
                        {crawling ? 'Crawling...' : 'Refresh Notices'}
                    </button>
                </div>
            </header>

            <div className="px-6 pt-3 pb-2 flex gap-1.5 overflow-x-auto bg-background-dark/50 border-b border-card-border/50">
                {filterTabs.map(tab => (
                    <button
                        key={tab.key}
                        onClick={() => setActiveFilter(tab.key)}
                        className={`flex items-center gap-1 px-3 py-1.5 rounded-lg text-[11px] font-medium whitespace-nowrap transition-all
                            ${activeFilter === tab.key
                                ? 'bg-primary/15 text-primary border border-primary/30'
                                : 'text-slate-400 hover:text-slate-300 hover:bg-card-dark border border-transparent'
                            }`}
                    >
                        <span className="material-icons text-xs">{tab.icon}</span>
                        {tab.label}
                    </button>
                ))}
            </div>

            {loading ? (
                <div className="flex-1 flex items-center justify-center">
                    <span className="text-slate-500 text-sm font-mono animate-pulse">Loading alerts...</span>
                </div>
            ) : (
                <div className="p-4 space-y-4 pb-20">
                    <div className="flex items-center justify-between px-1 mb-2">
                        <h3 className="text-xs font-semibold text-slate-400 uppercase tracking-wider">
                            {activeFilter === 'all' ? 'All Alerts' : activeFilter}
                        </h3>
                        <span className="text-[10px] bg-card-dark border border-card-border px-2 py-1 rounded-full text-slate-400">
                            {filteredAlerts.length} {filteredAlerts.length === 1 ? 'Alert' : 'Alerts'}
                        </span>
                    </div>

                    {filteredAlerts.length === 0 && (
                        <div className="text-center py-12 text-slate-500 text-sm">
                            <span className="material-icons text-4xl mb-2 block opacity-30">notifications_none</span>
                            No alerts in this category
                        </div>
                    )}

                    {filteredAlerts.map((alert, idx) => {
                        const c = colorMap[alert.color] || 'slate';
                        const isCritical = alert.severity === 'critical';
                        const noticeSource = getNoticeSource(alert.message);
                        const isNotice = noticeSource !== null;

                        return (
                            <div key={idx} className={`bg-card-dark border ${isCritical ? `border-${c}-500/30 shadow-lg shadow-${c}-500/5` : 'border-card-border'} rounded-xl p-4 relative overflow-hidden group`}>
                                {isCritical && (
                                    <div className="absolute top-0 right-0 p-3 opacity-10 group-hover:opacity-20 transition-opacity">
                                        <span className={`material-icons text-${c}-500 text-6xl`}>warning</span>
                                    </div>
                                )}
                                {!isCritical && <div className={`absolute left-0 top-0 bottom-0 w-1 bg-${c}-500`}></div>}

                                <div className={`flex items-start gap-4 ${!isCritical ? 'pl-2' : ''} relative z-10`}>
                                    {isCritical ? (
                                        <div className="mt-1">
                                            <div className={`relative w-3 h-3 rounded-full bg-${c}-500`}>
                                                <div className={`absolute inset-0 rounded-full bg-${c}-500 animate-ping opacity-75`}></div>
                                            </div>
                                        </div>
                                    ) : (
                                        <div className={`mt-1 w-8 h-8 rounded-lg bg-${c}-500/10 flex items-center justify-center shrink-0`}>
                                            <span className={`material-icons text-${c}-500 text-sm`}>
                                                {alert.category === 'Academic' ? 'school'
                                                    : alert.category === 'Scholarship' ? 'payments'
                                                        : alert.category === 'Competition' ? 'emoji_events'
                                                            : alert.category === 'Research' ? 'science'
                                                                : 'memory'}
                                            </span>
                                        </div>
                                    )}
                                    <div className="flex-1">
                                        <div className="flex justify-between items-start mb-1">
                                            <div className="flex items-center gap-2">
                                                <span className={`text-[10px] font-bold text-${c}-500 bg-${c}-500/10 px-2 py-0.5 rounded border border-${c}-500/20 uppercase tracking-wide`}>
                                                    {alert.category}{isCritical ? ' - Critical' : ''}
                                                </span>
                                                {isNotice && noticeSource && (
                                                    <span className="text-[10px] font-medium text-blue-400 bg-blue-500/10 px-1.5 py-0.5 rounded border border-blue-500/20">
                                                        {noticeSource}
                                                    </span>
                                                )}
                                            </div>
                                            <span className="text-[10px] font-mono text-slate-400">{alert.time}</span>
                                        </div>
                                        <h4 className={`${isCritical ? 'text-base font-bold' : 'text-sm font-semibold'} text-${isCritical ? 'white' : 'slate-200'} mb-1`}>
                                            {alert.title}
                                        </h4>
                                        <p className={`text-${isCritical ? 'sm' : 'xs'} text-slate-400 mb-${isCritical ? '3' : '2'} ${isCritical ? 'leading-snug' : ''}`}>
                                            {alert.message}
                                        </p>
                                        <button className={`text-xs font-medium text-${c}-${c === 'red' ? '400 hover:text-red-300' : c === 'primary' ? '' : '500 hover:text-' + c + '-400'} ${c === 'primary' ? 'hover:text-primary/80' : ''} flex items-center gap-1 transition-colors`}>
                                            View Details <span className="material-icons text-[12px]">arrow_forward</span>
                                        </button>
                                    </div>
                                </div>
                            </div>
                        );
                    })}
                </div>
            )}
        </div>
    );
};

