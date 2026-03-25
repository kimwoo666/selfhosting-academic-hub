import React, { useEffect, useState } from 'react';

interface AlertData {
    severity: string;
    category: string;
    title: string;
    message: string;
    time: string;
    color: string;
}

interface NoticeSource {
    id: number;
    name: string;
    url: string;
    title_selector: string;
    enabled: boolean;
    created_at: string;
}

interface NoticeSourceForm {
    name: string;
    url: string;
    title_selector: string;
    enabled: boolean;
}

type FilterTab = 'all' | 'Academic' | 'Scholarship' | 'Competition' | 'System' | 'Research' | 'General';

const emptyForm: NoticeSourceForm = {
    name: '',
    url: '',
    title_selector: '',
    enabled: true,
};

export const Alerts: React.FC = () => {
    const [alerts, setAlerts] = useState<AlertData[]>([]);
    const [sources, setSources] = useState<NoticeSource[]>([]);
    const [loading, setLoading] = useState(true);
    const [crawling, setCrawling] = useState(false);
    const [savingSource, setSavingSource] = useState(false);
    const [editingSourceId, setEditingSourceId] = useState<number | null>(null);
    const [activeFilter, setActiveFilter] = useState<FilterTab>('all');
    const [form, setForm] = useState<NoticeSourceForm>(emptyForm);
    const [sourceStatus, setSourceStatus] = useState('');
    const [sourceStatusTone, setSourceStatusTone] = useState<'info' | 'success' | 'error'>('info');

    const fetchAlerts = () => {
        setLoading(true);
        fetch('/api/alerts')
            .then(res => res.json())
            .then((data: AlertData[]) => {
                setAlerts(Array.isArray(data) ? data : []);
                setLoading(false);
            })
            .catch(err => {
                console.error('Failed to fetch alerts:', err);
                setLoading(false);
            });
    };

    const fetchSources = () => {
        fetch('/api/notice-sources')
            .then(res => res.json())
            .then((data: NoticeSource[]) => {
                setSources(Array.isArray(data) ? data : []);
            })
            .catch(err => {
                console.error('Failed to fetch notice sources:', err);
            });
    };

    useEffect(() => {
        fetchAlerts();
        fetchSources();
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

    const resetForm = () => {
        setEditingSourceId(null);
        setForm(emptyForm);
    };

    const handleSourceSubmit = async (event: React.FormEvent) => {
        event.preventDefault();
        if (!form.name.trim() || !form.url.trim() || !form.title_selector.trim()) {
            setSourceStatusTone('error');
            setSourceStatus('Name, URL, and title selector are required.');
            return;
        }

        setSavingSource(true);
        try {
            const res = await fetch(
                editingSourceId === null ? '/api/notice-sources' : `/api/notice-sources/${editingSourceId}`,
                {
                    method: editingSourceId === null ? 'POST' : 'PUT',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        name: form.name.trim(),
                        url: form.url.trim(),
                        title_selector: form.title_selector.trim(),
                        enabled: form.enabled,
                    }),
                }
            );

            const data = await res.json().catch(() => ({}));
            if (!res.ok || data.error) {
                throw new Error(data.error || 'Failed to save notice source');
            }

            setSourceStatusTone('success');
            setSourceStatus(editingSourceId === null ? 'Notice source added.' : 'Notice source updated.');
            resetForm();
            fetchSources();
        } catch (err) {
            console.error(err);
            setSourceStatusTone('error');
            setSourceStatus(err instanceof Error ? err.message : 'Failed to save notice source.');
        } finally {
            setSavingSource(false);
        }
    };

    const handleEdit = (source: NoticeSource) => {
        setEditingSourceId(source.id);
        setForm({
            name: source.name,
            url: source.url,
            title_selector: source.title_selector,
            enabled: source.enabled,
        });
        setSourceStatus('');
    };

    const handleDelete = async (sourceId: number) => {
        try {
            const res = await fetch(`/api/notice-sources/${sourceId}`, { method: 'DELETE' });
            const data = await res.json().catch(() => ({}));
            if (!res.ok || data.error) {
                throw new Error(data.error || 'Failed to delete notice source');
            }

            if (editingSourceId === sourceId) resetForm();
            setSourceStatusTone('success');
            setSourceStatus('Notice source deleted.');
            fetchSources();
        } catch (err) {
            console.error(err);
            setSourceStatusTone('error');
            setSourceStatus(err instanceof Error ? err.message : 'Failed to delete notice source.');
        }
    };

    const handleToggleEnabled = async (source: NoticeSource) => {
        try {
            const res = await fetch(`/api/notice-sources/${source.id}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    name: source.name,
                    url: source.url,
                    title_selector: source.title_selector,
                    enabled: !source.enabled,
                }),
            });
            const data = await res.json().catch(() => ({}));
            if (!res.ok || data.error) {
                throw new Error(data.error || 'Failed to update notice source');
            }

            setSourceStatusTone('success');
            setSourceStatus(`Notice source ${!source.enabled ? 'enabled' : 'disabled'}.`);
            fetchSources();
        } catch (err) {
            console.error(err);
            setSourceStatusTone('error');
            setSourceStatus(err instanceof Error ? err.message : 'Failed to update notice source.');
        }
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
        { key: 'Research', label: 'Research', icon: 'science' },
        { key: 'General', label: 'General', icon: 'article' },
        { key: 'System', label: 'System', icon: 'memory' },
    ];

    const filteredAlerts = activeFilter === 'all'
        ? alerts
        : alerts.filter(a => a.category === activeFilter);

    const getNoticeSource = (message: string) => {
        const match = message.match(/^\[([^\]]+)\]/);
        return match ? match[1] : null;
    };

    const statusClass = sourceStatusTone === 'success'
        ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300'
        : sourceStatusTone === 'error'
            ? 'bg-red-500/10 border-red-500/30 text-red-300'
            : 'bg-primary/10 border-primary/30 text-primary';

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

            <div className="p-4 space-y-4 pb-20">
                <div className="grid grid-cols-1 xl:grid-cols-[1.2fr_0.8fr] gap-4">
                    <section className="bg-card-dark border border-card-border rounded-2xl p-5 space-y-4">
                        <div className="flex items-start justify-between gap-3">
                            <div>
                                <h3 className="text-sm font-semibold text-white">Custom Notice Watchers</h3>
                                <p className="text-xs text-slate-400 mt-1">
                                    Add any university or department notice page by entering the page URL and the CSS selector that matches notice titles.
                                </p>
                            </div>
                            <span className="text-[10px] uppercase tracking-widest text-primary font-semibold">
                                Generic Monitoring
                            </span>
                        </div>

                        {sourceStatus && (
                            <div className={`rounded-lg border px-3 py-2 text-xs font-medium ${statusClass}`}>
                                {sourceStatus}
                            </div>
                        )}

                        <form onSubmit={handleSourceSubmit} className="space-y-3">
                            <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                                <label className="space-y-1.5">
                                    <span className="text-[11px] uppercase tracking-wide text-slate-500 font-semibold">Display Name</span>
                                    <input
                                        value={form.name}
                                        onChange={event => setForm(prev => ({ ...prev, name: event.target.value }))}
                                        placeholder="Seoul National CS Notices"
                                        className="w-full rounded-lg border border-card-border bg-slate-950/40 px-3 py-2.5 text-sm text-white outline-none focus:border-primary/40 focus:ring-2 focus:ring-primary/15"
                                    />
                                </label>
                                <label className="space-y-1.5">
                                    <span className="text-[11px] uppercase tracking-wide text-slate-500 font-semibold">Notice Page URL</span>
                                    <input
                                        value={form.url}
                                        onChange={event => setForm(prev => ({ ...prev, url: event.target.value }))}
                                        placeholder="https://example.edu/notices"
                                        className="w-full rounded-lg border border-card-border bg-slate-950/40 px-3 py-2.5 text-sm text-white outline-none focus:border-primary/40 focus:ring-2 focus:ring-primary/15"
                                    />
                                </label>
                            </div>

                            <label className="space-y-1.5 block">
                                <span className="text-[11px] uppercase tracking-wide text-slate-500 font-semibold">Title Selector</span>
                                <input
                                    value={form.title_selector}
                                    onChange={event => setForm(prev => ({ ...prev, title_selector: event.target.value }))}
                                    placeholder=".board-list a.subject"
                                    className="w-full rounded-lg border border-card-border bg-slate-950/40 px-3 py-2.5 text-sm text-white outline-none focus:border-primary/40 focus:ring-2 focus:ring-primary/15"
                                />
                            </label>

                            <div className="flex flex-wrap items-center gap-2 pt-1">
                                <label className="flex items-center gap-2 text-xs text-slate-400 mr-2">
                                    <input
                                        type="checkbox"
                                        checked={form.enabled}
                                        onChange={event => setForm(prev => ({ ...prev, enabled: event.target.checked }))}
                                        className="rounded border-card-border bg-slate-950/40"
                                    />
                                    Enabled
                                </label>
                                <button
                                    type="submit"
                                    disabled={savingSource}
                                    className="px-4 py-2 rounded-lg bg-primary text-white text-sm font-semibold hover:bg-primary/90 transition-colors disabled:opacity-60"
                                >
                                    {savingSource ? 'Saving...' : editingSourceId === null ? 'Add Watcher' : 'Save Changes'}
                                </button>
                                <button
                                    type="button"
                                    onClick={resetForm}
                                    className="px-4 py-2 rounded-lg border border-card-border text-sm font-medium text-slate-300 hover:bg-white/5 transition-colors"
                                >
                                    Clear
                                </button>
                            </div>
                        </form>
                    </section>

                    <section className="bg-card-dark border border-card-border rounded-2xl p-5 space-y-4">
                        <div>
                            <h3 className="text-sm font-semibold text-white">Selector Guide</h3>
                            <p className="text-xs text-slate-400 mt-1">
                                Use the selector that directly points at notice title elements. If the selected element is inside a link, the crawler will follow the nearest anchor automatically.
                            </p>
                        </div>

                        <div className="space-y-2 text-xs text-slate-300">
                            <div className="rounded-lg border border-card-border bg-slate-950/30 px-3 py-2">
                                <span className="text-primary font-semibold">Examples</span>
                                <div className="mt-2 space-y-1 text-slate-400 font-mono">
                                    <div>`.board-list a`</div>
                                    <div>`table.notice td.title a`</div>
                                    <div>`#noticeWrap .item .subject`</div>
                                </div>
                            </div>
                            <div className="rounded-lg border border-card-border bg-slate-950/30 px-3 py-2 text-slate-400">
                                Tip: Open the notice page, inspect a title element in the browser, then copy a CSS selector from DevTools.
                            </div>
                        </div>
                    </section>
                </div>

                <section className="bg-card-dark border border-card-border rounded-2xl p-5 space-y-4">
                    <div className="flex items-center justify-between gap-3">
                        <div>
                            <h3 className="text-sm font-semibold text-white">Tracked Notice Sources</h3>
                            <p className="text-xs text-slate-400 mt-1">
                                These sources are checked during manual refresh and the background notice crawl.
                            </p>
                        </div>
                        <span className="text-[10px] bg-background-dark border border-card-border px-2 py-1 rounded-full text-slate-400">
                            {sources.length} {sources.length === 1 ? 'source' : 'sources'}
                        </span>
                    </div>

                    {sources.length === 0 ? (
                        <div className="text-sm text-slate-500 border border-dashed border-card-border rounded-xl px-4 py-8 text-center">
                            No custom notice watchers yet.
                        </div>
                    ) : (
                        <div className="grid grid-cols-1 lg:grid-cols-2 gap-3">
                            {sources.map(source => (
                                <div key={source.id} className="rounded-xl border border-card-border bg-background-dark/40 p-4 space-y-3">
                                    <div className="flex items-start justify-between gap-3">
                                        <div className="min-w-0">
                                            <div className="flex items-center gap-2">
                                                <h4 className="text-sm font-semibold text-white truncate">{source.name}</h4>
                                                <span className={`text-[10px] px-2 py-0.5 rounded-full border ${source.enabled ? 'text-emerald-300 bg-emerald-500/10 border-emerald-500/20' : 'text-slate-400 bg-slate-700/20 border-slate-600/40'}`}>
                                                    {source.enabled ? 'Enabled' : 'Paused'}
                                                </span>
                                            </div>
                                            <p className="text-[11px] text-slate-500 break-all mt-1">{source.url}</p>
                                        </div>
                                    </div>
                                    <div className="rounded-lg bg-slate-950/40 border border-card-border px-3 py-2">
                                        <div className="text-[10px] uppercase tracking-wide text-slate-500 font-semibold mb-1">Selector</div>
                                        <div className="text-xs text-slate-300 font-mono break-all">{source.title_selector}</div>
                                    </div>
                                    <div className="flex flex-wrap gap-2">
                                        <button
                                            onClick={() => handleToggleEnabled(source)}
                                            className="px-3 py-1.5 rounded-lg border border-card-border text-xs font-medium text-slate-300 hover:bg-white/5 transition-colors"
                                        >
                                            {source.enabled ? 'Disable' : 'Enable'}
                                        </button>
                                        <button
                                            onClick={() => handleEdit(source)}
                                            className="px-3 py-1.5 rounded-lg border border-primary/30 bg-primary/10 text-xs font-medium text-primary hover:bg-primary/20 transition-colors"
                                        >
                                            Edit
                                        </button>
                                        <button
                                            onClick={() => handleDelete(source.id)}
                                            className="px-3 py-1.5 rounded-lg border border-red-500/30 bg-red-500/10 text-xs font-medium text-red-300 hover:bg-red-500/20 transition-colors"
                                        >
                                            Delete
                                        </button>
                                    </div>
                                </div>
                            ))}
                        </div>
                    )}
                </section>

                <div className="px-1 flex gap-1.5 overflow-x-auto">
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
                    <div className="flex items-center justify-center py-16">
                        <span className="text-slate-500 text-sm font-mono animate-pulse">Loading alerts...</span>
                    </div>
                ) : (
                    <section className="space-y-4">
                        <div className="flex items-center justify-between px-1">
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
                                            <div className="flex justify-between items-start mb-1 gap-3">
                                                <div className="flex items-center gap-2 flex-wrap">
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
                                        </div>
                                    </div>
                                </div>
                            );
                        })}
                    </section>
                )}
            </div>
        </div>
    );
};
