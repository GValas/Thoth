import { useMemo, useState } from 'react';
import { GREEKS, type GridMatrix, type OptionChain } from './types';

const fmt = (v: number | undefined) =>
  v == null || !Number.isFinite(v) ? '' : v.toLocaleString(undefined, { minimumFractionDigits: 4, maximumFractionDigits: 4 });
const fmtStrike = (v: number) => v.toLocaleString(undefined, { maximumFractionDigits: 4 });

//! present per-cell Greeks (same on both wings — same engine).
function presentGreeks(chain: OptionChain): string[] {
  const side = chain.call ?? chain.put;
  return side ? GREEKS.filter((g) => side.greeks?.[g]) : [];
}

function cell(m: GridMatrix | undefined, metric: string, i: number, j: number): number | undefined {
  if (!m) return undefined;
  if (metric === 'premium') return m.premium[i]?.[j];
  return m.greeks[metric]?.[i]?.[j];
}

//! Option-chain view of one underlying: a block per maturity, CALLS left / PUTS right of a
//! central Strike column, strikes ascending top-to-bottom. Greek columns fold behind a toggle.
export function OptionChainView({ chain }: { chain: OptionChain }) {
  const [activeMat, setActiveMat] = useState(0);
  const present = useMemo(() => presentGreeks(chain), [chain]);
  const metrics = ['premium', ...present];
  const callMetrics = [...metrics].reverse(); // premium ends next to the strike
  const order = useMemo(
    () => chain.strikes.map((strike, i) => ({ strike, i })).sort((a, b) => a.strike - b.strike),
    [chain.strikes],
  );

  const label = (m: string) => (m === 'premium' ? 'prem' : m);

  return (
    <div className="chain">
      <div className="tabs sub">
        {chain.maturities.map((mat, j) => (
          <button key={mat} className={j === activeMat ? 'tab on' : 'tab'} onClick={() => setActiveMat(j)}>
            {mat}
          </button>
        ))}
      </div>

      {chain.maturities.map((mat, j) =>
        j !== activeMat ? null : (
          <div className="block" key={mat}>
            <table className="oc">
            <thead>
              <tr className="groups">
                {chain.call && <th className="calls" colSpan={callMetrics.length}>Calls</th>}
                <th className="strike-h" rowSpan={2}>Strike</th>
                {chain.put && <th className="puts" colSpan={metrics.length}>Puts</th>}
              </tr>
              <tr>
                {chain.call && callMetrics.map((m) => <th key={`ch-${m}`}>{label(m)}</th>)}
                {chain.put && metrics.map((m) => <th key={`ph-${m}`}>{label(m)}</th>)}
              </tr>
            </thead>
            <tbody>
              {order.map(({ strike, i }) => (
                <tr key={strike}>
                  {chain.call &&
                    callMetrics.map((m) => (
                      <td key={`c-${m}`} className={m === 'premium' ? 'prem' : ''}>
                        {fmt(cell(chain.call, m, i, j))}
                      </td>
                    ))}
                  <td className="strike">{fmtStrike(strike)}</td>
                  {chain.put &&
                    metrics.map((m) => (
                      <td key={`p-${m}`} className={m === 'premium' ? 'prem' : ''}>
                        {fmt(cell(chain.put, m, i, j))}
                      </td>
                    ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ))}
    </div>
  );
}
