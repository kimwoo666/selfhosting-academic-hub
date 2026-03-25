import React, { useState } from 'react';
import { Sidebar } from './components/Sidebar';
import { Dashboard } from './pages/Dashboard';
import { Calendar } from './pages/Calendar';
import { Nodes } from './pages/Nodes';
import { Terminal } from './pages/Terminal';
import { Grades } from './pages/Grades';
import { Alerts } from './pages/Alerts';
import { Profile } from './pages/Profile';
import { Settings } from './pages/Settings';
import { PageView } from './types';

function App() {
  const [currentPage, setCurrentPage] = useState<PageView>('dashboard');
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

  React.useEffect(() => {
    // Load theme on startup
    fetch('/api/settings')
      .then(res => res.json())
      .then(data => {
        if (data.theme === 'dark') {
          document.documentElement.classList.add('dark');
        } else {
          document.documentElement.classList.remove('dark');
        }
      })
      .catch(() => { });
  }, []);

  const renderPage = () => {
    switch (currentPage) {
      case 'dashboard': return <Dashboard onNavigate={setCurrentPage} />;
      case 'calendar': return <Calendar />;
      case 'nodes': return <Nodes />;
      case 'terminal': return <Terminal />;
      case 'grades': return <Grades />;
      case 'alerts': return <Alerts />;
      case 'profile': return <Profile />;
      case 'settings': return <Settings />;
      default: return <Dashboard onNavigate={setCurrentPage} />;
    }
  };

  return (
    <div className="flex h-screen bg-bg-base text-slate-200 overflow-hidden font-display selection:bg-primary/30">
      <Sidebar
        currentPage={currentPage}
        onNavigate={setCurrentPage}
        isOpen={sidebarOpen}
        onClose={() => setSidebarOpen(false)}
        collapsed={sidebarCollapsed}
        onToggleCollapse={() => setSidebarCollapsed(!sidebarCollapsed)}
      />

      <div className="flex-1 flex flex-col min-w-0 relative">
        {/* Mobile Header Toggle */}
        <button
          className="lg:hidden fixed top-5 left-4 z-40 p-2 rounded-lg bg-card-dark border border-card-border shadow-lg text-slate-300 hover:bg-white/5 active:bg-white/10 transition-colors"
          onClick={() => setSidebarOpen(true)}
        >
          <span className="material-icons">menu</span>
        </button>

        {renderPage()}
      </div>
    </div>
  );
}

export default App;