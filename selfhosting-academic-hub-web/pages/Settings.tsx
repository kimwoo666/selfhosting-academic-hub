import React, { useState } from 'react';

export const Settings: React.FC = () => {
    const [displayPref, setDisplayPref] = useState(
        typeof window !== 'undefined'
            ? (localStorage.getItem('academic_hub_resource_display') || 'percent')
            : 'percent'
    );
    const [theme, setTheme] = useState<'dark' | 'light'>('dark');

    React.useEffect(() => {
        // Fetch initial settings
        fetch('/api/settings')
            .then(res => res.json())
            .then(data => {
                if (data.theme) {
                    setTheme(data.theme);
                    if (data.theme === 'dark') {
                        document.documentElement.classList.add('dark');
                    } else {
                        document.documentElement.classList.remove('dark');
                    }
                }
            })
            .catch(err => console.error('Failed to load settings:', err));
    }, []);

    const toggleDisplayPref = () => {
        const next = displayPref === 'percent' ? 'absolute' : 'percent';
        localStorage.setItem('academic_hub_resource_display', next);
        setDisplayPref(next);
    };

    const toggleTheme = async () => {
        const next = theme === 'dark' ? 'light' : 'dark';
        try {
            const res = await fetch('/api/settings/theme', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ theme: next })
            });
            if (res.ok) {
                setTheme(next);
                if (next === 'dark') {
                    document.documentElement.classList.add('dark');
                } else {
                    document.documentElement.classList.remove('dark');
                }
            }
        } catch (e) {
            console.error('Failed to toggle theme:', e);
        }
    };

    return (
        <div className="flex-1 flex flex-col h-full bg-background-dark text-slate-200 overflow-hidden">
            <header className="flex-none px-6 pt-5 pb-4 flex items-center bg-card-dark/50 backdrop-blur-md border-b border-card-border/50 z-20">
                <h1 className="text-lg font-semibold tracking-wide text-white pl-8 lg:pl-0">Settings</h1>
            </header>

            <main className="flex-1 overflow-y-auto pb-8">
                <div className="max-w-3xl mx-auto p-6 space-y-6">

                    {/* Display Preferences */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6">
                        <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-5 flex items-center gap-2">
                            <span className="material-symbols-outlined text-sm text-primary">tune</span>
                            Display Preferences
                        </h3>
                        <div className="space-y-5">
                            <div className="flex items-center justify-between p-4 bg-slate-900/50 rounded-xl border border-slate-800">
                                <div className="flex items-center gap-3">
                                    <span className="material-symbols-outlined text-lg text-accent-purple">memory</span>
                                    <div>
                                        <p className="text-sm font-medium text-slate-200">Resource Display Mode</p>
                                        <p className="text-[11px] text-slate-500 mt-0.5">
                                            {displayPref === 'absolute'
                                                ? 'Default: used/total ??Hover: percentage'
                                                : 'Default: percentage ??Hover: used/total'}
                                        </p>
                                    </div>
                                </div>
                                <button
                                    onClick={toggleDisplayPref}
                                    className="relative inline-flex h-6 w-11 items-center rounded-full transition-colors duration-200 focus:outline-none"
                                    style={{ backgroundColor: displayPref === 'absolute' ? '#6366f1' : '#374151' }}
                                >
                                    <span
                                        className="inline-block h-4 w-4 transform rounded-full bg-white transition-transform duration-200 shadow-sm"
                                        style={{ transform: displayPref === 'absolute' ? 'translateX(1.375rem)' : 'translateX(0.25rem)' }}
                                    />
                                </button>
                            </div>
                            <p className="text-[10px] text-slate-600 px-1">
                                Controls how RAM and Disk usage are displayed on Dashboard and Nodes pages.
                                Toggle to swap default and hover-over display formats.
                            </p>
                        </div>
                    </div>

                    {/* Appearance */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6">
                        <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-5 flex items-center gap-2">
                            <span className="material-symbols-outlined text-sm text-accent-orange">palette</span>
                            Appearance
                        </h3>
                        <div className="space-y-4">
                            <div className="flex items-center justify-between p-4 bg-slate-900/50 rounded-xl border border-slate-800">
                                <div className="flex items-center gap-3">
                                    <span className="material-symbols-outlined text-lg text-slate-400">dark_mode</span>
                                    <div>
                                        <p className="text-sm font-medium text-slate-200">Dark Mode</p>
                                        <p className="text-[11px] text-slate-500 mt-0.5">{theme === 'dark' ? 'Enabled' : 'Disabled'}</p>
                                    </div>
                                </div>
                                <button
                                    onClick={toggleTheme}
                                    className="relative inline-flex h-6 w-11 items-center rounded-full transition-colors duration-200 focus:outline-none"
                                    style={{ backgroundColor: theme === 'dark' ? '#6366f1' : '#374151' }}
                                >
                                    <span
                                        className="inline-block h-4 w-4 transform rounded-full bg-white transition-transform duration-200 shadow-sm"
                                        style={{ transform: theme === 'dark' ? 'translateX(1.375rem)' : 'translateX(0.25rem)' }}
                                    />
                                </button>
                            </div>
                        </div>
                    </div>

                    {/* System Info */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-5 flex justify-between items-center">
                        <div className="flex items-center gap-3 text-xs text-slate-500 font-mono">
                            <span className="material-symbols-outlined text-sm text-slate-600">terminal</span>
                            Self-Hosting Academic Hub v4.3.0
                        </div>
                        <div className="text-[10px] uppercase tracking-widest font-bold text-slate-600">
                            Academic Hub Systems
                        </div>
                    </div>
                </div>
            </main>
        </div>
    );
};
