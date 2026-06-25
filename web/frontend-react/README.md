# Thoth dashboard — React POC

A minimal **Vite + React + TypeScript** proof-of-concept that reimplements the
**Pricing Grid** against the **existing NestJS BFF** — no backend changes. It
demonstrates the full stack works with React instead of Angular:

- JWT login (access token in memory + refresh cookie), same flow as the Angular app
- resolves the first workspace and its market-data objects
- the pricing-grid form (engine `ana / pde / mcl / mcl/gpu`, currency, underlyings,
  strikes, maturities, type, Greeks) with first-run defaults
- submit → poll progress → fetch result
- results as an **option chain per maturity** (calls left / strike / puts right,
  strikes top-to-bottom, foldable Greeks), one **tab per underlying**

It deliberately covers only the Pricing Grid (not the full market-data editors), and
uses plain HTML tables + CSS rather than Angular Material / AG Grid, to keep the POC
small. Wire types are mirrored locally in `src/types.ts` (a real port would consume
`@thoth/shared`).

## Run

The BFF must be running on `:3000` (e.g. `npm --prefix web/bff run start:dev`, or the
docker-compose stack — then point the proxy at it).

```bash
cd web/frontend-react
npm install
npm run dev            # http://localhost:4300  (proxies /api -> :3000)
# BFF elsewhere?  VITE_API_TARGET=http://host:port npm run dev
```

Sign in with the docker-compose dev admin (`admin@thoth.dev` / `change-me-please`,
or your own user). If the workspace has no underlyings, use the **Generate sample
data** link.

## Layout

```
src/
  api.ts          fetch wrapper over /api (Bearer token + refresh cookie)
  types.ts        wire types mirrored from web/shared
  App.tsx         login gate + shell
  PricingGrid.tsx form, submit/poll, tabs
  OptionChain.tsx per-maturity calls|strike|puts table, foldable Greeks
  styles.css
```
