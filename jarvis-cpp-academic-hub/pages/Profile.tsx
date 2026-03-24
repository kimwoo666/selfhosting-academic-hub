import React, { useState, useEffect } from 'react';

interface ProfileData {
    name: string;
    role: string;
    id: string;
    level: string;
    avatar_url: string;
    term: string;
    status: string;
    semester_progress: number;
    security_score: number;
    version: string;
    university: string;
    email: string;
    student_id: string;
    devices: { name: string; icon: string; last_active: string; online: boolean }[];
}

export const Profile: React.FC = () => {
    const [profile, setProfile] = useState<ProfileData | null>(null);
    const [loading, setLoading] = useState(true);
    const [editing, setEditing] = useState(false);
    const [saving, setSaving] = useState(false);
    const [saveMsg, setSaveMsg] = useState('');
    const [form, setForm] = useState<Partial<ProfileData>>({});

    // LMS Login state
    const [lmsId, setLmsId] = useState('');
    const [lmsPw, setLmsPw] = useState('');
    const [lmsLoading, setLmsLoading] = useState(false);
    const [lmsStatus, setLmsStatus] = useState<'idle' | 'success' | 'error'>('idle');
    const [lmsMessage, setLmsMessage] = useState('');

    const fetchLmsStatus = () => {
        fetch('/api/lms/status')
            .then(res => res.json())
            .then((d: { logged_in?: boolean; student_id?: string }) => {
                if (d?.logged_in) {
                    setLmsStatus('success');
                    setLmsMessage('이미 로그인된 세션이 유지되고 있습니다.');
                    if (d.student_id) setLmsId(d.student_id);
                } else {
                    setLmsStatus('idle');
                }
            })
            .catch(() => { });
    };

    const fetchProfile = () => {
        setLoading(true);
        fetch('/api/profile')
            .then(res => res.json())
            .then((d: ProfileData) => {
                setProfile(d);
                setForm(d);
                setLoading(false);
            })
            .catch(() => setLoading(false));
    };

    useEffect(() => {
        fetchProfile();
        fetchLmsStatus();
    }, []);

    const startEdit = () => {
        setForm({ ...profile });
        setEditing(true);
    };

    const cancelEdit = () => {
        setForm({ ...profile });
        setEditing(false);
    };

    const saveProfile = async () => {
        setSaving(true);
        try {
            const res = await fetch('/api/profile', {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(form)
            });
            const data = await res.json();
            if (data.success) {
                setSaveMsg('Profile saved successfully');
                setEditing(false);
                fetchProfile();
                if (typeof window !== 'undefined') {
                    window.dispatchEvent(new Event('jarvis-profile-updated'));
                }
            } else {
                setSaveMsg('Failed to save profile');
            }
        } catch {
            setSaveMsg('Network error');
        }
        setSaving(false);
        setTimeout(() => setSaveMsg(''), 3000);
    };

    const updateForm = (key: string, value: string) => {
        setForm(prev => ({ ...prev, [key]: value }));
    };

    const loginLms = async () => {
        if (!lmsId || !lmsPw) return;
        setLmsLoading(true);
        setLmsStatus('idle');
        setLmsMessage('');
        try {
            const res = await fetch('/api/lms/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ student_id: lmsId, password: lmsPw })
            });
            const data = await res.json();
            if (data.success) {
                setLmsStatus('success');
                setLmsMessage(data.message || 'LMS 로그인 성공');
                setLmsPw('');
            } else {
                setLmsStatus('error');
                setLmsMessage(data.message || 'LMS 로그인 실패');
            }
        } catch {
            setLmsStatus('error');
            setLmsMessage('네트워크 오류');
        }
        setLmsLoading(false);
    };

    if (loading || !profile) {
        return (
            <div className="flex flex-1 items-center justify-center h-full bg-background-dark">
                <span className="text-slate-500 text-sm font-mono animate-pulse">Loading profile...</span>
            </div>
        );
    }

    const renderField = (label: string, field: string, value: string, icon?: string) => (
        <div>
            <label className="text-[10px] uppercase text-slate-500 font-bold tracking-wider block mb-1">{label}</label>
            {editing ? (
                <input
                    value={(form as any)[field] || ''}
                    onChange={e => updateForm(field, e.target.value)}
                    className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-sm text-white focus:border-primary focus:outline-none transition-colors"
                />
            ) : (
                <div className="flex items-center gap-2 text-slate-200 text-sm">
                    {icon && <span className="material-symbols-outlined text-slate-500 text-sm">{icon}</span>}
                    <span>{value || <span className="text-slate-600 italic">Not set</span>}</span>
                </div>
            )}
        </div>
    );

    return (
        <div className="flex flex-1 flex-col h-full bg-background-dark text-slate-200 relative overflow-hidden">
            <header className="flex-none px-6 pt-5 pb-4 flex justify-between items-center bg-card-dark/50 backdrop-blur-md border-b border-card-border/50 z-20">
                <h1 className="text-lg font-semibold tracking-wide text-white pl-8 lg:pl-0">Profile</h1>
                <div className="flex items-center gap-3">
                    {saveMsg && (
                        <span className={`text-xs font-medium px-3 py-1 rounded-full border ${saveMsg.includes('success') ? 'text-emerald-400 bg-emerald-500/10 border-emerald-500/20' : 'text-red-400 bg-red-500/10 border-red-500/20'}`}>
                            {saveMsg}
                        </span>
                    )}
                    {!editing ? (
                        <button onClick={startEdit} className="px-4 py-1.5 rounded-lg bg-primary/10 border border-primary/30 text-primary text-xs font-medium hover:bg-primary/20 transition-colors flex items-center gap-1">
                            <span className="material-icons text-sm">edit</span> Edit
                        </button>
                    ) : (
                        <div className="flex gap-2">
                            <button onClick={cancelEdit} className="px-4 py-1.5 rounded-lg border border-slate-700 text-slate-300 text-xs font-medium hover:bg-slate-800 transition-colors">
                                Cancel
                            </button>
                            <button onClick={saveProfile} disabled={saving}
                                className="px-4 py-1.5 rounded-lg bg-primary text-white text-xs font-medium hover:bg-primary/90 transition-colors disabled:opacity-50 shadow-lg shadow-primary/20 flex items-center gap-1">
                                {saving ? (
                                    <span className="material-icons text-sm animate-spin">sync</span>
                                ) : (
                                    <span className="material-icons text-sm">save</span>
                                )}
                                Save
                            </button>
                        </div>
                    )}
                </div>
            </header>

            <main className="flex-1 overflow-y-auto pb-8">
                {/* Profile Card */}
                <div className="max-w-3xl mx-auto p-6 space-y-6">
                    {/* Avatar & Name */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6 flex items-center gap-6 relative overflow-hidden">
                        <div className="absolute inset-0 bg-gradient-to-r from-primary/5 to-transparent"></div>
                        <div className="relative z-10 flex-none">
                            <img
                                src={profile.avatar_url}
                                alt={profile.name}
                                className="w-20 h-20 rounded-2xl border-2 border-primary/30 shadow-lg shadow-primary/10"
                            />
                            <div className={`absolute -bottom-1 -right-1 w-5 h-5 rounded-full border-2 border-card-dark ${profile.status === 'ACTIVE' ? 'bg-emerald-400' : 'bg-slate-500'}`}></div>
                        </div>
                        <div className="relative z-10 flex-1 min-w-0">
                            {editing ? (
                                <div className="space-y-2">
                                    <input value={form.name || ''} onChange={e => updateForm('name', e.target.value)}
                                        className="bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-lg text-white font-bold focus:border-primary focus:outline-none w-full" />
                                    <input value={form.role || ''} onChange={e => updateForm('role', e.target.value)}
                                        className="bg-slate-800 border border-slate-700 rounded-lg px-3 py-1.5 text-xs text-slate-300 focus:border-primary focus:outline-none w-full" />
                                </div>
                            ) : (
                                <>
                                    <h2 className="text-xl font-bold text-white mb-0.5">{profile.name}</h2>
                                    <p className="text-xs text-slate-400">{profile.role}</p>
                                </>
                            )}
                            <div className="flex gap-2 mt-2">
                                <span className="px-2 py-0.5 rounded text-[10px] font-bold uppercase bg-primary/10 text-primary border border-primary/20">{profile.status}</span>
                                <span className="px-2 py-0.5 rounded text-[10px] font-bold uppercase bg-card-dark text-slate-400 border border-card-border">{profile.id}</span>
                            </div>
                        </div>
                    </div>

                    {/* Personal Info */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6">
                        <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-4 flex items-center gap-2">
                            <span className="material-symbols-outlined text-sm text-primary">person</span>
                            Personal Information
                        </h3>
                        <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                            {renderField('Full Name', 'name', profile.name, 'badge')}
                            {renderField('Email', 'email', profile.email, 'mail')}
                            {renderField('University', 'university', profile.university, 'school')}
                            {renderField('Student ID', 'student_id', profile.student_id, 'fingerprint')}
                            {renderField('Term', 'term', profile.term, 'calendar_month')}
                            {renderField('Role', 'role', profile.role, 'admin_panel_settings')}
                        </div>
                    </div>

                    {/* Security & System */}
                    <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
                        <div className="bg-card-dark border border-card-border rounded-2xl p-5">
                            <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-3 flex items-center gap-2">
                                <span className="material-symbols-outlined text-sm text-emerald-400">shield</span>
                                Security Score
                            </h3>
                            <div className="flex items-end gap-2">
                                <span className="text-4xl font-bold text-white">{profile.security_score}</span>
                                <span className="text-sm text-slate-500 mb-1">/ 100</span>
                            </div>
                            <div className="mt-3 bg-slate-800 rounded-full h-2 overflow-hidden">
                                <div className="bg-gradient-to-r from-emerald-400 to-primary h-full rounded-full transition-all duration-1000 ease-out"
                                    style={{ width: `${profile.security_score}%` }}></div>
                            </div>
                        </div>

                        <div className="bg-card-dark border border-card-border rounded-2xl p-5">
                            <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-3 flex items-center gap-2">
                                <span className="material-symbols-outlined text-sm text-accent-orange">trending_up</span>
                                Semester Progress
                            </h3>
                            <div className="flex items-end gap-2">
                                <span className="text-4xl font-bold text-white">{profile.semester_progress}</span>
                                <span className="text-sm text-slate-500 mb-1">%</span>
                            </div>
                            <div className="mt-3 bg-slate-800 rounded-full h-2 overflow-hidden">
                                <div className="bg-gradient-to-r from-accent-orange to-amber-400 h-full rounded-full transition-all duration-1000 ease-out"
                                    style={{ width: `${profile.semester_progress}%` }}></div>
                            </div>
                        </div>
                    </div>

                    {/* LMS Login Section */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-6 relative overflow-hidden">
                        <div className="absolute inset-0 bg-gradient-to-br from-blue-500/5 to-transparent pointer-events-none"></div>
                        <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-4 flex items-center gap-2 relative z-10">
                            <span className="material-symbols-outlined text-sm text-blue-400">school</span>
                            SSU LMS 연동
                        </h3>
                        {lmsStatus === 'success' ? (
                            <div className="relative z-10">
                                <div className="flex items-center gap-3 p-4 bg-emerald-500/10 border border-emerald-500/20 rounded-xl">
                                    <span className="material-symbols-outlined text-emerald-400 text-2xl">check_circle</span>
                                    <div className="flex-1">
                                        <p className="text-sm font-medium text-emerald-300">LMS 로그인됨</p>
                                        <p className="text-xs text-slate-400 mt-0.5">학번: {lmsId}</p>
                                    </div>
                                    <button onClick={() => { setLmsStatus('idle'); setLmsId(''); setLmsMessage(''); }}
                                        className="px-3 py-1.5 rounded-lg border border-slate-700 text-slate-400 text-xs font-medium hover:bg-slate-800 transition-colors">
                                        로그아웃
                                    </button>
                                </div>
                                <p className="text-[10px] text-slate-500 mt-2">서버 재시작 시 세션은 초기화됩니다.</p>
                            </div>
                        ) : (
                            <div className="relative z-10 space-y-4">
                                <p className="text-xs text-slate-400 leading-relaxed">
                                    Signing in to SSU LMS also syncs u-SAINT grade summary (Current GPA, Class Rank).
                                </p>
                                <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                                    <div>
                                        <label className="text-[10px] uppercase text-slate-500 font-bold tracking-wider block mb-1">학번 (Student ID)</label>
                                        <input value={lmsId} onChange={e => setLmsId(e.target.value)}
                                            className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-sm text-white focus:border-blue-400 focus:outline-none transition-colors"
                                            placeholder="20XXXXXXXX" />
                                    </div>
                                    <div>
                                        <label className="text-[10px] uppercase text-slate-500 font-bold tracking-wider block mb-1">비밀번호 (Password)</label>
                                        <input type="password" value={lmsPw} onChange={e => setLmsPw(e.target.value)}
                                            className="w-full bg-slate-800 border border-slate-700 rounded-lg px-3 py-2 text-sm text-white focus:border-blue-400 focus:outline-none transition-colors"
                                            placeholder="********"
                                            onKeyDown={e => e.key === 'Enter' && loginLms()} />
                                    </div>
                                </div>
                                {lmsStatus === 'error' && (
                                    <div className="flex items-center gap-2 p-2 bg-red-500/10 border border-red-500/20 rounded-lg">
                                        <span className="material-symbols-outlined text-red-400 text-sm">error</span>
                                        <span className="text-xs text-red-400">{lmsMessage}</span>
                                    </div>
                                )}
                                <button onClick={loginLms} disabled={lmsLoading || !lmsId || !lmsPw}
                                    className="w-full py-2.5 rounded-lg bg-blue-500 text-white text-sm font-medium hover:bg-blue-600 transition-colors disabled:opacity-50 disabled:cursor-not-allowed shadow-lg shadow-blue-500/20 flex items-center justify-center gap-2">
                                    {lmsLoading ? (
                                        <>
                                            <span className="material-icons text-sm animate-spin">sync</span>
                                            로그인 중...
                                        </>
                                    ) : (
                                        <>
                                            <span className="material-symbols-outlined text-sm">login</span>
                                            LMS 로그인
                                        </>
                                    )}
                                </button>
                                <p className="text-[10px] text-slate-600 text-center">
                                    인증 정보는 서버 메모리에만 일시 저장되며 재시작 시 자동 삭제됩니다.
                                </p>
                            </div>
                        )}
                    </div>

                    {/* Connected Devices */}
                    {profile.devices.length > 0 && (
                        <div className="bg-card-dark border border-card-border rounded-2xl p-6">
                            <h3 className="text-xs uppercase text-slate-500 font-bold tracking-widest mb-4 flex items-center gap-2">
                                <span className="material-symbols-outlined text-sm text-primary">devices</span>
                                Connected Devices
                            </h3>
                            <div className="space-y-3">
                                {profile.devices.map((dev, i) => (
                                    <div key={i} className="flex items-center gap-3 p-3 bg-slate-800/50 rounded-xl border border-slate-700/50">
                                        <span className="material-symbols-outlined text-slate-400">{dev.icon}</span>
                                        <div className="flex-1">
                                            <p className="text-sm font-medium text-slate-200">{dev.name}</p>
                                            <p className="text-xs text-slate-500">{dev.last_active}</p>
                                        </div>
                                        <span className={`h-2.5 w-2.5 rounded-full ${dev.online ? 'bg-emerald-400' : 'bg-slate-600'}`}></span>
                                    </div>
                                ))}
                            </div>
                        </div>
                    )}


                    {/* System Info */}
                    <div className="bg-card-dark border border-card-border rounded-2xl p-5 flex justify-between items-center">
                        <div className="flex items-center gap-3 text-xs text-slate-500 font-mono">
                            <span className="material-symbols-outlined text-sm text-slate-600">terminal</span>
                            {profile.version}
                        </div>
                        <div className="text-[10px] uppercase tracking-widest font-bold text-slate-600">
                            Jarvis Systems
                        </div>
                    </div>
                </div>
            </main>
        </div>
    );
};
