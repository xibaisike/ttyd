import { useState, useEffect, useCallback, useRef } from 'react';
import type { ITerminalOptions } from '@xterm/xterm';

import { Terminal } from '../terminal';
import type { ClientOptions, FlowControl } from '../terminal/xterm';
import { SessionList } from './SessionList';
import type { Session } from './SessionList';
import './session.scss';

interface Props {
  wsBase: string;
  tokenUrl: string;
  clientOptions: ClientOptions;
  termOptions: ITerminalOptions;
  flowControl: FlowControl;
}

export function SessionLayout({ wsBase, tokenUrl, clientOptions, termOptions, flowControl }: Props) {
  const [sessions, setSessions] = useState<Session[]>([]);
  const [openTabs, setOpenTabs] = useState<string[]>([]);
  const [activeTab, setActiveTab] = useState<string | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval>>();

  const fetchSessions = useCallback(async () => {
    try {
      const res = await fetch('/api/sessions?pageSize=100&pageNumber=1');
      if (res.ok) {
        const data = await res.json();
        setSessions(data.sessions || []);
      }
    } catch (e) {
      console.error('[ttyd] failed to fetch sessions:', e);
    }
  }, []);

  useEffect(() => {
    fetchSessions();
    intervalRef.current = setInterval(fetchSessions, 5000);
    return () => clearInterval(intervalRef.current);
  }, [fetchSessions]);

  const handleSelect = (name: string) => {
    if (!openTabs.includes(name)) {
      setOpenTabs(prev => [...prev, name]);
    }
    setActiveTab(name);
  };

  const handleCloseTab = (name: string) => {
    setOpenTabs(prev => {
      const next = prev.filter(t => t !== name);
      if (activeTab === name) {
        const idx = prev.indexOf(name);
        const newActive = next[Math.min(idx, next.length - 1)] || null;
        setActiveTab(newActive);
      }
      return next;
    });
  };

  const handleCreate = async (name: string) => {
    try {
      const res = await fetch('/api/sessions', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name }),
      });
      if (res.ok || res.status === 409) {
        await fetchSessions();
        handleSelect(name);
      }
    } catch (e) {
      console.error('[ttyd] failed to create session:', e);
    }
  };

  const handleDelete = async (name: string) => {
    try {
      await fetch(`/api/sessions/${encodeURIComponent(name)}`, { method: 'DELETE' });
      handleCloseTab(name);
      await fetchSessions();
    } catch (e) {
      console.error('[ttyd] failed to delete session:', e);
    }
  };

  return (
    <div className="session-layout">
      <div className="session-sidebar">
        <SessionList
          sessions={sessions}
          activeSession={activeTab}
          onSelect={handleSelect}
          onCreate={handleCreate}
          onDelete={handleDelete}
        />
      </div>
      <div className="session-main">
        {openTabs.length > 0 && (
          <div className="session-tabs">
            {openTabs.map(name => (
              <div
                key={name}
                className={`session-tab${name === activeTab ? ' active' : ''}`}
                onClick={() => setActiveTab(name)}
              >
                <span className="session-tab-name">{name}</span>
                <button
                  className="session-tab-close"
                  onClick={e => {
                    e.stopPropagation();
                    handleCloseTab(name);
                  }}
                >
                  &times;
                </button>
              </div>
            ))}
          </div>
        )}
        <div className="session-terminal">
          {openTabs.length === 0 ? (
            <div className="session-placeholder">Select or create a session</div>
          ) : (
            openTabs.map(name => (
              <div
                key={name}
                className="session-terminal-pane"
                style={{ display: name === activeTab ? 'block' : 'none' }}
              >
                <Terminal
                  id={`terminal-${name}`}
                  wsUrl={`${wsBase}?pid=${encodeURIComponent(name)}`}
                  tokenUrl={tokenUrl}
                  clientOptions={clientOptions}
                  termOptions={termOptions}
                  flowControl={flowControl}
                />
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
