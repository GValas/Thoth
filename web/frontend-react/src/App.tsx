import { useState, type FormEvent } from 'react';
import { api, setToken } from './api';
import { PricingGrid } from './PricingGrid';

//! POC shell: a login gate (defaulted to the dev admin from docker-compose) then the
//! Pricing Grid. Auth is access-token-in-memory + refresh cookie, same as the Angular app.
export function App() {
  const [authed, setAuthed] = useState(false);

  return (
    <div className="app">
      <header className="topbar">
        <h1>Thoth</h1>
        <span className="badge">React POC</span>
        <span className="spacer" />
        {authed && (
          <button
            className="link"
            onClick={() => {
              setToken(null);
              setAuthed(false);
            }}
          >
            Sign out
          </button>
        )}
      </header>
      {authed ? <PricingGrid /> : <Login onAuthed={() => setAuthed(true)} />}
    </div>
  );
}

function Login({ onAuthed }: { onAuthed: () => void }) {
  const [email, setEmail] = useState('admin@thoth.dev');
  const [password, setPassword] = useState('change-me-please');
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const { accessToken } = await api.login(email, password);
      setToken(accessToken);
      onAuthed();
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Login failed');
    } finally {
      setBusy(false);
    }
  };

  return (
    <form className="card login" onSubmit={submit}>
      <h2>Sign in</h2>
      <label>
        Email
        <input value={email} onChange={(e) => setEmail(e.target.value)} autoComplete="username" />
      </label>
      <label>
        Password
        <input
          type="password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          autoComplete="current-password"
        />
      </label>
      {error && <p className="error">{error}</p>}
      <button className="primary" disabled={busy}>
        {busy ? 'Signing in…' : 'Sign in'}
      </button>
      <p className="muted small">Defaults to the docker-compose dev admin.</p>
    </form>
  );
}
