import React, { useState, useEffect, useCallback } from 'react';
import { PageView, NavItem } from '../types';

interface SidebarProps {
  currentPage: PageView;
  onNavigate: (page: PageView) => void;
  isOpen: boolean;
  onClose: () => void;
  collapsed: boolean;
  onToggleCollapse: () => void;
}

const navItems: NavItem[] = [
  { id: 'dashboard', label: 'Dashboard', icon: 'dashboard' },
  { id: 'nodes', label: 'Nodes', icon: 'dns' },
  { id: 'calendar', label: 'Schedule', icon: 'calendar_today' },
  { id: 'terminal', label: 'Assistant', icon: 'smart_toy' },
  { id: 'grades', label: 'Grades', icon: 'school' },
  { id: 'alerts', label: 'Alerts', icon: 'notifications_none', badge: true },
];

export const Sidebar: React.FC<SidebarProps> = ({
  currentPage,
  onNavigate,
  isOpen,
  onClose,
  collapsed,
  onToggleCollapse
}) => {
  const [userName, setUserName] = useState('User');
  const [userRole, setUserRole] = useState('');
  const [userAvatar, setUserAvatar] = useState('');

  const loadProfile = useCallback(() => {
    fetch('/api/profile')
      .then(res => res.json())
      .then(d => {
        if (d.name) setUserName(d.name);
        if (d.role) setUserRole(d.role);
        if (d.avatar_url) setUserAvatar(d.avatar_url);
      })
      .catch(() => { });
  }, []);

  useEffect(() => {
    loadProfile();
  }, [loadProfile, currentPage]);

  useEffect(() => {
    const onProfileUpdated = () => loadProfile();
    window.addEventListener('academic-hub-profile-updated', onProfileUpdated);
    return () => {
      window.removeEventListener('academic-hub-profile-updated', onProfileUpdated);
    };
  }, [loadProfile]);

  const avatarUrl = userAvatar || `https://ui-avatars.com/api/?name=${encodeURIComponent(userName)}&background=13a4ec&color=fff`;

  return (
    <>
      {/* Mobile Backdrop */}
      <div
        className={`fixed inset-0 z-40 bg-black/50 backdrop-blur-sm lg:hidden transition-opacity duration-300 ${isOpen ? 'opacity-100' : 'opacity-0 pointer-events-none'}`}
        onClick={onClose}
      />

      {/* Sidebar Container */}
      <div className={`fixed inset-y-0 left-0 z-50 bg-card-dark border-r border-card-border shadow-2xl transform transition-all duration-300 ease-in-out 
        ${isOpen ? 'translate-x-0' : '-translate-x-full'} 
        lg:translate-x-0 lg:static lg:h-screen 
        ${collapsed ? 'lg:w-20' : 'lg:w-64'} w-64`}
      >
        <div className={`p-6 border-b border-card-border/50 flex items-center ${collapsed ? 'justify-center' : 'justify-between'} h-[73px]`}>
          {!collapsed && (
            <div>
              <h2 className="text-lg font-semibold tracking-tight text-white whitespace-nowrap overflow-hidden">Self-Hosting Academic Hub</h2>
              <p className="text-xs text-slate-500 font-mono whitespace-nowrap overflow-hidden">v4.3.0</p>
              <p className="text-[10px] text-slate-600 font-mono whitespace-nowrap overflow-hidden">Proxmox LXC Runtime</p>
            </div>
          )}
          {collapsed && (
            <span className="font-bold text-primary text-xl">AH</span>
          )}

          {/* Desktop Collapse Toggle */}
          <button
            onClick={onToggleCollapse}
            className={`hidden lg:flex items-center justify-center w-6 h-6 rounded-md hover:bg-white/10 text-slate-400 transition-colors ${collapsed ? 'absolute -right-3 top-8 bg-card-dark border border-card-border shadow-sm' : ''}`}
          >
            <span className="material-icons text-sm">{collapsed ? 'chevron_right' : 'chevron_left'}</span>
          </button>
        </div>

        <nav className="p-3 space-y-1 overflow-y-auto h-[calc(100vh-140px)] overflow-x-hidden">
          {navItems.map((item) => (
            <button
              key={item.id}
              onClick={() => {
                onNavigate(item.id);
                if (window.innerWidth < 1024) onClose();
              }}
              title={collapsed ? item.label : ''}
              className={`w-full flex items-center px-3 py-2 rounded-lg transition-colors group relative
                ${collapsed ? 'justify-center' : 'justify-between'}
                ${currentPage === item.id
                  ? 'bg-primary/10 text-primary'
                  : 'text-slate-400 hover:bg-white/5 hover:text-slate-200'
                }`}
            >
              <div className={`flex items-center ${collapsed ? '' : 'space-x-3'}`}>
                <span className="material-icons text-[24px]">{item.icon}</span>
                {!collapsed && <span className="text-sm font-medium whitespace-nowrap">{item.label}</span>}
              </div>

              {item.badge && (
                <span className={`w-2 h-2 rounded-full bg-red-500 shadow-sm shadow-red-500/50 ${collapsed ? 'absolute top-2 right-2' : ''}`}></span>
              )}
            </button>
          ))}

          <div className="h-px bg-card-border my-4 mx-3"></div>

          <button
            onClick={() => {
              onNavigate('profile');
              if (window.innerWidth < 1024) onClose();
            }}
            title={collapsed ? 'Profile' : ''}
            className={`w-full flex items-center px-3 py-2 rounded-lg transition-colors 
              ${collapsed ? 'justify-center' : 'space-x-3'}
              ${currentPage === 'profile'
                ? 'bg-primary/10 text-primary'
                : 'text-slate-400 hover:bg-white/5 hover:text-slate-200'
              }`}
          >
            <span className="material-icons text-[24px]">person</span>
            {!collapsed && <span className="text-sm font-medium">Profile</span>}
          </button>

          <button
            onClick={() => {
              onNavigate('settings');
              if (window.innerWidth < 1024) onClose();
            }}
            title={collapsed ? 'Settings' : ''}
            className={`w-full flex items-center px-3 py-2 rounded-lg transition-colors ${collapsed ? 'justify-center' : 'space-x-3'}
              ${currentPage === 'settings'
                ? 'bg-primary/10 text-primary'
                : 'text-slate-400 hover:bg-white/5 hover:text-slate-200'
              }`}
          >
            <span className="material-icons text-[24px]">settings</span>
            {!collapsed && <span className="text-sm font-medium">Settings</span>}
          </button>
        </nav>

        {/* User Mini Profile - synced with /api/profile */}
        <div className="absolute bottom-0 left-0 right-0 p-4 bg-card-dark border-t border-card-border">
          <div
            className={`flex items-center cursor-pointer hover:opacity-80 transition-opacity ${collapsed ? 'justify-center' : 'gap-3'}`}
            onClick={() => { onNavigate('profile'); if (window.innerWidth < 1024) onClose(); }}
          >
            <div className="w-8 h-8 rounded-full bg-slate-700 overflow-hidden flex-shrink-0">
              <img src={avatarUrl} alt="User" />
            </div>
            {!collapsed && (
              <div className="overflow-hidden">
                <p className="text-xs font-semibold text-white truncate">{userName}</p>
                <p className="text-[10px] text-slate-500 truncate">{userRole}</p>
              </div>
            )}
          </div>
        </div>
      </div>
    </>
  );
};
