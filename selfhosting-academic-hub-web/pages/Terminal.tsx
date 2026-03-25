import React, { useState, useEffect, useRef, useCallback } from 'react';

interface Message {
    sender: 'operator' | 'assistant';
    time: string;
    message: string;
    processing_time_ms?: number;
    tokens?: number;
}

// Lightweight markdown-to-HTML converter for terminal messages
function renderMarkdown(text: string): string {
    if (!text) return '';
    let html = text
        // Escape HTML
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

    // Code blocks: ```lang\n...\n```
    html = html.replace(/```(\w*)\n([\s\S]*?)```/g, (_m, lang, code) => {
        return `<pre class="terminal-code-block"><code class="lang-${lang}">${code.trim()}</code></pre>`;
    });
    // Inline code: `...`
    html = html.replace(/`([^`]+)`/g, '<code class="terminal-inline-code">$1</code>');
    // Bold: **...**
    html = html.replace(/\*\*([^*]+)\*\*/g, '<strong class="text-white">$1</strong>');
    // Italic: *...*
    html = html.replace(/(?<!\*)\*([^*]+)\*(?!\*)/g, '<em class="text-slate-300">$1</em>');
    // Headers: ## ...
    html = html.replace(/^### (.+)$/gm, '<div class="text-sm font-bold text-primary mt-3 mb-1">$1</div>');
    html = html.replace(/^## (.+)$/gm, '<div class="text-sm font-bold text-accent-orange mt-3 mb-1">$1</div>');
    // Bullet lists: - item
    html = html.replace(/^- (.+)$/gm, '<div class="flex gap-2 ml-2"><span class="text-primary flex-none">&bull;</span><span>$1</span></div>');
    // Numbered lists
    html = html.replace(/^(\d+)\. (.+)$/gm, '<div class="flex gap-2 ml-2"><span class="text-primary flex-none">$1.</span><span>$2</span></div>');
    // Newlines to <br>
    html = html.replace(/\n/g, '<br/>');

    return html;
}

export const Terminal: React.FC = () => {
    const [messages, setMessages] = useState<Message[]>([]);
    const [input, setInput] = useState('');
    const [ws, setWs] = useState<WebSocket | null>(null);
    const [connected, setConnected] = useState(false);
    const [aiLoad, setAiLoad] = useState(0);
    const [isThinking, setIsThinking] = useState(false);
    const messagesEndRef = useRef<HTMLDivElement>(null);
    const inputRef = useRef<HTMLInputElement>(null);

    const scrollToBottom = useCallback(() => {
        messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' });
    }, []);

    useEffect(() => { scrollToBottom(); }, [messages, scrollToBottom]);

    // Connect WebSocket
    useEffect(() => {
        const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const socket = new WebSocket(`${protocol}//${location.host}/ws/shell`);
        socket.onopen = () => {
            setConnected(true);
            setAiLoad(Math.floor(Math.random() * 30) + 10);
        };
        socket.onmessage = (event) => {
            try {
                const msg: Message = JSON.parse(event.data);
                msg.time = new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
                setMessages(prev => [...prev, msg]);
                setIsThinking(false);
                if (msg.processing_time_ms) {
                    setAiLoad(Math.min(95, Math.floor(msg.processing_time_ms / 50) + 20));
                }
            } catch { /* ignore */ }
        };
        socket.onclose = () => { setConnected(false); };
        socket.onerror = () => { setConnected(false); };
        setWs(socket);
        return () => { socket.close(); };
    }, []);

    const send = () => {
        if (!input.trim() || !ws || ws.readyState !== WebSocket.OPEN) return;
        const msg: Message = {
            sender: 'operator',
            time: new Date().toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }),
            message: input
        };
        setMessages(prev => [...prev, msg]);
        ws.send(JSON.stringify({ message: input }));
        setIsThinking(true);
        setInput('');
        inputRef.current?.focus();
    };

    return (
        <div className="flex flex-1 flex-col h-full bg-background-dark text-slate-200 relative overflow-hidden">
            <header className="flex-none px-6 pt-5 pb-4 flex justify-between items-center bg-card-dark/50 backdrop-blur-md border-b border-card-border/50 z-20">
                <h1 className="text-lg font-semibold tracking-wide text-white pl-8 lg:pl-0">Terminal</h1>
                <div className="flex items-center gap-4">
                    <div className="flex items-center gap-2">
                        <span className={`h-2 w-2 rounded-full ${connected ? 'bg-emerald-400 animate-pulse' : 'bg-red-400'}`}></span>
                        <span className="text-[10px] uppercase tracking-widest text-slate-500 font-medium">{connected ? 'Connected' : 'Offline'}</span>
                    </div>
                    <div className="text-xs text-slate-500 font-mono">
                        AI Load: <span className={`font-bold ${aiLoad > 70 ? 'text-accent-orange' : aiLoad > 40 ? 'text-amber-400' : 'text-emerald-400'}`}>{aiLoad}%</span>
                    </div>
                </div>
            </header>

            <main className="flex-1 overflow-y-auto px-5 py-4 space-y-4 font-mono text-sm" onClick={() => inputRef.current?.focus()}>
                {messages.map((msg, idx) => (
                    <div key={idx} className={`flex gap-3 ${msg.sender === 'operator' ? 'justify-end' : ''}`}>
                        {msg.sender === 'assistant' && (
                            <div className="flex-none w-8 h-8 rounded-lg bg-primary/10 border border-primary/20 flex items-center justify-center mt-1">
                                <span className="text-primary text-xs font-bold">A</span>
                            </div>
                        )}
                        <div className={`${msg.sender === 'operator'
                            ? 'bg-primary/10 border-primary/20 max-w-[70%]'
                            : 'bg-card-dark border-card-border max-w-[85%]'
                            } border rounded-xl px-4 py-3 relative overflow-hidden`}>
                            {msg.sender === 'assistant' && <div className="absolute left-0 top-0 bottom-0 w-1 bg-primary"></div>}
                            <div className="flex justify-between items-center mb-2">
                                <span className={`text-[10px] uppercase tracking-widest font-bold ${msg.sender === 'assistant' ? 'text-primary' : 'text-slate-400'}`}>
                                    {msg.sender === 'assistant' ? 'ASSISTANT' : 'OPERATOR'}
                                </span>
                                <span className="text-[10px] text-slate-600 font-mono">{msg.time}</span>
                            </div>
                            {msg.sender === 'assistant' ? (
                                <div className="text-sm text-slate-300 leading-relaxed terminal-markdown"
                                    dangerouslySetInnerHTML={{ __html: renderMarkdown(msg.message) }} />
                            ) : (
                                <p className="text-sm text-slate-200 whitespace-pre-wrap">{msg.message}</p>
                            )}
                            {msg.processing_time_ms && (
                                <div className="flex justify-end mt-2 gap-3">
                                    <span className="text-[10px] text-slate-600">{msg.processing_time_ms}ms</span>
                                    {msg.tokens && <span className="text-[10px] text-slate-600">{msg.tokens} tokens</span>}
                                </div>
                            )}
                        </div>
                        {msg.sender === 'operator' && (
                            <div className="flex-none w-8 h-8 rounded-lg bg-slate-700/50 border border-slate-600 flex items-center justify-center mt-1">
                                <span className="text-slate-400 text-xs font-bold">U</span>
                            </div>
                        )}
                    </div>
                ))}

                {isThinking && (
                    <div className="flex gap-3">
                        <div className="flex-none w-8 h-8 rounded-lg bg-primary/10 border border-primary/20 flex items-center justify-center mt-1">
                            <span className="text-primary text-xs font-bold">A</span>
                        </div>
                        <div className="bg-card-dark border border-card-border rounded-xl px-4 py-3">
                            <div className="flex gap-1 items-center">
                                <div className="w-2 h-2 rounded-full bg-primary animate-bounce" style={{ animationDelay: '0ms' }}></div>
                                <div className="w-2 h-2 rounded-full bg-primary animate-bounce" style={{ animationDelay: '150ms' }}></div>
                                <div className="w-2 h-2 rounded-full bg-primary animate-bounce" style={{ animationDelay: '300ms' }}></div>
                                <span className="text-[10px] text-slate-500 ml-2">Processing...</span>
                            </div>
                        </div>
                    </div>
                )}

                <div ref={messagesEndRef} />
            </main>

            <footer className="flex-none px-5 py-4 bg-card-dark/50 backdrop-blur-md border-t border-card-border/50">
                <div className="flex items-center gap-3">
                    <span className="text-primary font-mono text-sm font-bold">&gt;</span>
                    <input
                        ref={inputRef}
                        type="text"
                        value={input}
                        onChange={e => setInput(e.target.value)}
                        onKeyDown={e => e.key === 'Enter' && send()}
                        placeholder="Enter command or ask the assistant..."
                        disabled={!connected}
                        className="flex-1 bg-transparent text-sm text-slate-200 placeholder-slate-600 focus:outline-none font-mono disabled:opacity-40"
                    />
                    <button onClick={send} disabled={!connected || !input.trim()}
                        className="p-2 rounded-lg bg-primary/10 border border-primary/20 text-primary hover:bg-primary/20 transition-colors disabled:opacity-30 disabled:cursor-not-allowed">
                        <span className="material-symbols-outlined text-lg">send</span>
                    </button>
                </div>
            </footer>

            <style>{`
                .terminal-code-block {
                    background: #0d1117;
                    border: 1px solid #1e293b;
                    border-radius: 8px;
                    padding: 12px 14px;
                    margin: 8px 0;
                    overflow-x: auto;
                    font-family: 'JetBrains Mono', monospace;
                    font-size: 12px;
                    line-height: 1.5;
                    color: #e2e8f0;
                }
                .terminal-inline-code {
                    background: #1e293b;
                    border: 1px solid #334155;
                    border-radius: 4px;
                    padding: 1px 5px;
                    font-family: 'JetBrains Mono', monospace;
                    font-size: 12px;
                    color: #38bdf8;
                }
            `}</style>
        </div>
    );
};
