import { useEffect, useMemo, useRef, useState } from 'react';
import { api } from './api';
import { OptionChainView } from './OptionChain';
import type {
  Engine,
  Exercise,
  GridMatrix,
  GridMeta,
  OptionChain,
  OptionType,
  Workspace,
  WsObject,
} from './types';

const ENGINES: Engine[] = ['ana', 'pde', 'mcl', 'mcl_gpu'];
const GREEK_INDICATORS = ['delta', 'gamma', 'vega', 'rho', 'theta'];
const UNDERLYING_KINDS = ['equity', 'basket', 'forex', 'rainbow', 'composite'];

//! add `months` calendar months to a YYYY-MM-DD date, clamping the day to month length.
function addMonths(iso: string, months: number): string {
  const [y, m, d] = iso.split('-').map(Number);
  const target = m - 1 + months;
  const year = y + Math.floor(target / 12);
  const month = ((target % 12) + 12) % 12;
  const lastDay = new Date(Date.UTC(year, month + 1, 0)).getUTCDate();
  const day = Math.min(d, lastDay);
  const p = (n: number, w = 2) => String(n).padStart(w, '0');
  return `${p(year, 4)}-${p(month + 1)}-${p(day)}`;
}

function parseNumbers(text: string): number[] {
  return text
    .split(/[\s,]+/)
    .map((s) => Number(s.trim()))
    .filter((n) => Number.isFinite(n));
}

//! pivot the flat (underlying, type) matrices into one option chain per underlying.
function toChains(matrices: GridMatrix[]): OptionChain[] {
  const by = new Map<string, OptionChain>();
  for (const m of matrices) {
    let c = by.get(m.underlying);
    if (!c) {
      c = { underlying: m.underlying, currency: m.currency, strikes: m.strikes, maturities: m.maturities };
      by.set(m.underlying, c);
    }
    if (m.type === 'call') c.call = m;
    else c.put = m;
  }
  return [...by.values()];
}

function fmtDuration(ms: number | undefined): string {
  if (ms == null || !Number.isFinite(ms)) return '';
  if (ms < 1) return `${Math.round(ms * 1000)} µs`;
  if (ms < 1000) return `${ms < 10 ? ms.toFixed(1) : Math.round(ms)} ms`;
  const s = ms / 1000;
  if (s < 60) return `${s < 10 ? s.toFixed(2) : s.toFixed(1)} s`;
  const min = Math.floor(s / 60);
  const rem = Math.round(s % 60);
  return rem ? `${min} min ${rem} s` : `${min} min`;
}

export function PricingGrid() {
  const [workspace, setWorkspace] = useState<Workspace | null>(null);
  const [objects, setObjects] = useState<WsObject[]>([]);

  // form state
  const [engine, setEngine] = useState<Engine>('ana');
  const [types, setTypes] = useState<OptionType[]>(['call', 'put']);
  const [exercise, setExercise] = useState<Exercise>('european');
  const [currency, setCurrency] = useState('eur');
  const [underlyings, setUnderlyings] = useState<string[]>([]);
  const [strikesText, setStrikesText] = useState('80 90 100 110 120');
  const [maturities, setMaturities] = useState<string[]>([]);
  const [includeGreeks, setIncludeGreeks] = useState(true);

  // results
  const [status, setStatus] = useState<string | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [matrices, setMatrices] = useState<GridMatrix[]>([]);
  const [meta, setMeta] = useState<GridMeta | null>(null);
  const [activeTab, setActiveTab] = useState(0);
  const poll = useRef<ReturnType<typeof setInterval> | null>(null);

  //! load the first workspace + its objects on mount, then prepopulate defaults.
  useEffect(() => {
    (async () => {
      let list = await api.workspaces();
      if (!list.length) list = [await api.createWorkspace('Default')];
      const ws = list[0];
      setWorkspace(ws);
      setObjects(await api.objects(ws.id));
      setMaturities([1, 2, 3, 4, 5].map((m) => addMonths(ws.today, m)));
    })().catch((e) => setError(e instanceof Error ? e.message : String(e)));
    return () => {
      if (poll.current) clearInterval(poll.current);
    };
  }, []);

  const underlyingObjects = useMemo(
    () => objects.filter((o) => UNDERLYING_KINDS.includes(o.kind)),
    [objects],
  );
  const currencyNames = useMemo(
    () => objects.filter((o) => o.kind === 'currency').map((o) => o.name),
    [objects],
  );
  const chains = useMemo(() => toChains(matrices), [matrices]);

  const toggle = <T,>(arr: T[], v: T): T[] => (arr.includes(v) ? arr.filter((x) => x !== v) : [...arr, v]);

  const canSubmit =
    !!workspace && underlyings.length > 0 && types.length > 0 && parseNumbers(strikesText).length > 0 && maturities.length > 0 && !running;

  const submit = async () => {
    if (!workspace) return;
    setError(null);
    setMatrices([]);
    setMeta(null);
    setRunning(true);
    setStatus('queued');
    const indicators = ['premium', ...(includeGreeks ? GREEK_INDICATORS : [])];
    try {
      const { jobId } = await api.submitGrid({
        workspaceId: workspace.id,
        engine,
        underlyings,
        types,
        strikes: parseNumbers(strikesText),
        maturities,
        indicators,
        exercise,
        currency,
      });
      poll.current = setInterval(async () => {
        try {
          const p = await api.gridProgress(jobId);
          setStatus(p.status);
          if (p.status === 'done' || p.status === 'error') {
            if (poll.current) clearInterval(poll.current);
            const run = await api.getGrid(jobId);
            setRunning(false);
            if (run.status === 'error') setError(run.error ?? 'Pricing failed');
            else {
              setMatrices(run.result?.matrices ?? []);
              setMeta(run.result?.meta ?? null);
              setActiveTab(0);
            }
          }
        } catch (e) {
          if (poll.current) clearInterval(poll.current);
          setRunning(false);
          setError(e instanceof Error ? e.message : String(e));
        }
      }, 700);
    } catch (e) {
      setRunning(false);
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const seed = async () => {
    if (!workspace) return;
    await api.seed(workspace.id, Math.floor(Math.random() * 1e9));
    setObjects(await api.objects(workspace.id));
  };

  return (
    <div className="grid-page">
      <h2>Pricing grid</h2>

      <div className="card form">
        <div className="row">
          <div className="field">
            <label className="lbl">Engine</label>
            <div className="toggle-group">
              {ENGINES.map((e) => (
                <button key={e} className={engine === e ? 'on' : ''} onClick={() => setEngine(e)}>
                  {e === 'mcl_gpu' ? 'mcl/gpu' : e}
                </button>
              ))}
            </div>
          </div>
          <div className="field">
            <label className="lbl">Type</label>
            <div className="checks">
              {(['call', 'put'] as OptionType[]).map((t) => (
                <label key={t}>
                  <input type="checkbox" checked={types.includes(t)} onChange={() => setTypes(toggle(types, t))} /> {t}
                </label>
              ))}
            </div>
          </div>
          <div className="field">
            <label className="lbl">Exercise</label>
            <div className="toggle-group">
              {(['european', 'american'] as Exercise[]).map((x) => (
                <button key={x} className={exercise === x ? 'on' : ''} onClick={() => setExercise(x)}>
                  {x}
                </button>
              ))}
            </div>
          </div>
          <div className="field">
            <label className="lbl">Currency</label>
            <select value={currency} onChange={(e) => setCurrency(e.target.value)}>
              {(currencyNames.length ? currencyNames : [currency]).map((c) => (
                <option key={c} value={c}>
                  {c}
                </option>
              ))}
            </select>
          </div>
          <label className="greeks">
            <input type="checkbox" checked={includeGreeks} onChange={(e) => setIncludeGreeks(e.target.checked)} /> Greeks
          </label>
          <span className="spacer" />
          <button className="primary" disabled={!canSubmit} onClick={submit}>
            Price grid
          </button>
        </div>

        <div className="row">
          <div className="field grow">
            <label className="lbl">Underlyings</label>
            <select
              multiple
              size={4}
              value={underlyings}
              onChange={(e) => setUnderlyings([...e.target.selectedOptions].map((o) => o.value))}
            >
              {underlyingObjects.map((o) => (
                <option key={o.name} value={o.name}>
                  {o.name} ({o.kind})
                </option>
              ))}
            </select>
            {underlyingObjects.length === 0 && (
              <button className="link" onClick={seed}>
                No underlyings — generate sample data
              </button>
            )}
          </div>
          <div className="field grow">
            <label className="lbl">Strikes</label>
            <input value={strikesText} onChange={(e) => setStrikesText(e.target.value)} placeholder="80 90 100 110 120" />
            <label className="lbl" style={{ marginTop: 8 }}>
              Maturities (YYYY-MM-DD, comma/space separated)
            </label>
            <input
              value={maturities.join(', ')}
              onChange={(e) => setMaturities(e.target.value.split(/[\s,]+/).filter(Boolean).sort())}
            />
          </div>
        </div>
      </div>

      {running && (
        <p className="muted">
          <span className="dot" /> {status}…
        </p>
      )}
      {error && <p className="error">{error}</p>}
      {meta && (
        <p className="muted small">
          Priced on <code>{meta.server}</code>
          {meta.engineMs != null && ` · engine ${fmtDuration(meta.engineMs)}`}
          {meta.execMs != null && ` · round-trip ${fmtDuration(meta.execMs)}`}
          {meta.engineVersion && ` · ${meta.engineVersion}`}
        </p>
      )}

      {chains.length > 0 && (
        <div className="results">
          <div className="tabs">
            {chains.map((c, i) => (
              <button key={c.underlying} className={i === activeTab ? 'tab on' : 'tab'} onClick={() => setActiveTab(i)}>
                {c.underlying}
              </button>
            ))}
          </div>
          {chains[activeTab] && <OptionChainView chain={chains[activeTab]} />}
        </div>
      )}
    </div>
  );
}
