import { useState } from 'react';

export interface Session {
  name: string;
  state: 'running' | 'paused';
  connections: number;
  created_at: number;
}

interface Props {
  sessions: Session[];
  activeSession: string | null;
  onSelect: (name: string) => void;
  onCreate: (name: string) => void;
  onDelete: (name: string) => void;
}

export function SessionList({ sessions, activeSession, onSelect, onCreate, onDelete }: Props) {
  const [newName, setNewName] = useState('');
  const [confirmDelete, setConfirmDelete] = useState<string | null>(null);

  const handleCreate = () => {
    const name = newName.trim();
    if (!name) return;
    onCreate(name);
    setNewName('');
  };

  const handleDelete = (name: string) => {
    if (confirmDelete === name) {
      onDelete(name);
      setConfirmDelete(null);
    } else {
      setConfirmDelete(name);
    }
  };

  return (
    <>
      <div className="sidebar-header">
        <div className="new-session-form">
          <input
            type="text"
            placeholder="Session name"
            value={newName}
            maxLength={63}
            onChange={e => setNewName(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && handleCreate()}
          />
          <button onClick={handleCreate} disabled={!newName.trim()}>
            New
          </button>
        </div>
      </div>
      <div className="session-list">
        {sessions.length === 0 ? (
          <div className="session-empty">No sessions yet</div>
        ) : (
          sessions.map(s => (
            <div
              key={s.name}
              className={`session-item${s.name === activeSession ? ' active' : ''}`}
              onClick={() => onSelect(s.name)}
            >
              <div className="session-info">
                <div className="session-name">{s.name}</div>
                <div className="session-meta">
                  <span className={`session-badge ${s.state}`}>{s.state}</span>
                  <span>{s.connections} conn</span>
                </div>
              </div>
              <div className="session-actions">
                <button
                  className={confirmDelete === s.name ? 'confirm' : ''}
                  onClick={e => {
                    e.stopPropagation();
                    handleDelete(s.name);
                  }}
                  onBlur={() => setConfirmDelete(null)}
                >
                  {confirmDelete === s.name ? 'Confirm' : 'Del'}
                </button>
              </div>
            </div>
          ))
        )}
      </div>
    </>
  );
}
