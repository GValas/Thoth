# TODO — Dashboard web Thoth (monorepo `pricer/` + `web/`)

> Spécifications de la future GUI web. **Aucune implémentation pour l'instant** — document de
> cadrage à exécuter plus tard.

## Context

Le pricer Thoth est fonctionnel mais ne s'utilise qu'en CLI / serveur HTTP avec des books
YAML écrits à la main. On veut une **GUI web (dashboard)** pour :

1. **Éditer les market data** (equity, fx, rates, vol, correl…) via des formulaires.
2. **Calculer des grilles de prix** : sur un produit choisi (ex. vanille), tableau de prix
   par **strike × maturité**, ventilé **par sous-jacent** et **par type (call/put)**, heatmap + Greeks.

Et au passage, **restructurer le repo en monorepo** : tout l'actuel C++ va dans `pricer/`,
au même niveau qu'un nouveau `web/`.

Découvertes d'exploration qui structurent l'archi :

- Le moteur **expose déjà** `thoth -server <port>` (cpp-httplib) : `POST /price` (YAML in →
  YAML out, header optionnel `X-Exec-Name`, réponse **chunked toujours HTTP 200**, erreur =
  corps commençant par `error: `), `GET /health`, `GET /progress`. Voir `run_server.cpp`.
- **Aucune modif de logique C++** : on garde `thoth -server` comme microservice de pricing
  (seuls des chemins/scripts changent du fait du déplacement).
- Pricing **sérialisé par mutex global** → une grille = **un seul book** de N contrats en
  **une seule requête** `/price`, puis relecture des `<name>_premium` par cellule
  (cf. `pricer.cpp`, sample `random_vanillas.yaml`). C'est aussi le plus efficace.
- Format **YAML à tags locaux** (`!equity`…). **JSON Schema auto-descriptif**
  (`schema/thoth.schema.json`) → pilote la génération/validation des formulaires.

**Décisions** : monorepo **`pricer/` + `web/`** · stack **NestJS + Angular** (full-TypeScript)
· persistance **SQLite** (ORM NestJS) · v1 = **les deux onglets** + auth/admin.

## Restructuration monorepo (à faire en premier, `git mv` pour garder l'historique)

Basculent dans **`pricer/`** (tout le C++ + son tooling) :
`CMakeLists.txt`, `Dockerfile`, `.dockerignore`, `.clang-format`, `.clang-tidy`, `TODO.md`,
`build_run.sh`, `format.sh`, `run_docker_*.sh`, `run_local_client_matrix.sh`,
et les dossiers `src/`, `tests/`, `samples/`, `schema/`, `docs/`. L'actuel `README.md`
devient **`pricer/README.md`** (doc détaillée du moteur).

Restent **à la racine** : `.git/`, `.gitignore`, `.github/`, `.devcontainer/`, `.vscode/`,
`.claude/`, `CLAUDE.md`. Nouveaux à la racine : `web/`, `docker-compose.yml`, et un
**`README.md` monorepo** léger (carte du dépôt → pointe vers `pricer/README.md` et `web/`).
Les dossiers de build (`build*/`, gitignored) ne sont pas déplacés, juste régénérés sous
`pricer/`.

**Références de chemins à corriger après le `git mv`** :
- `.github/` workflows (CI) : gates `clang-format --check` et `ctest` → préfixer `pricer/`
  (ou `working-directory: pricer`).
- `.devcontainer/` et `.vscode/` : tâches/chemins build pointant vers la racine → `pricer/`.
- `.gitignore` racine : règles de build → `pricer/build*/`, plus `web/**/node_modules`,
  `web/bff/data/*.sqlite`, `web/frontend/dist`.
- Scripts `run_docker_*.sh` / `build_run.sh` : chemins relatifs (samples, Dockerfile) à
  revérifier une fois sous `pricer/`.
- **`CLAUDE.md`** : sections *Build & test* et *Layout* (les commandes deviennent
  `cmake -B pricer/build …`, et `src/ tests/ samples/` → `pricer/src/ …`). Le **mandat
  README** vise désormais `pricer/README.md` pour les évolutions du moteur ; ajouter une
  ligne pour le `README.md` racine côté web.

## Architecture cible

```
Browser ──HTTP/JSON──▶ NestJS BFF (TypeScript) ──POST /price (YAML)──▶ thoth -server (C++, inchangé)
 Angular SPA           - DTO + validation (class-validator)            GET /health, GET /progress
 (Material/AG Grid)    - SQLite (TypeORM) : workspaces/objects/runs
                       - matérialise un book YAML (tags préservés)
                       - génère le book de grille (N vanilles)
                       - valide via pricer/schema/thoth.schema.json (ajv)
                       - OpenAPI /api/docs ; sert le SPA buildé en prod
```

3 process : moteur (8080) ← NestJS (3000) ← Angular (`ng serve` 4200, proxy `/api`).

## Arborescence cible du repo

```
Thoth/
├── README.md                   # NOUVEAU : carte monorepo (→ pricer/ + web/)
├── docker-compose.yml          # NOUVEAU : prod — 3 services (pricer, bff, web)
├── CLAUDE.md                   # MAJ chemins build/layout + mandat README
├── .devcontainer/              # NOUVEAU : dev — 3 conteneurs (1 techno chacun)
│   ├── devcontainer.json       #   runServices: [pricer, bff, web] ; attach configurable
│   └── docker-compose.yml      #   dev : sources montées + commandes watch
├── .github/  .vscode/  .claude/                    # racine (chemins MAJ vers pricer/)
│
├── pricer/                     # ── MOTEUR C++ (déplacé tel quel, git mv) ──
│   ├── CMakeLists.txt  Dockerfile  .clang-format  .clang-tidy  TODO.md
│   ├── README.md               # doc détaillée du moteur (ex-README racine)
│   ├── build_run.sh  format.sh  run_docker_*.sh  run_local_client_matrix.sh
│   ├── src/
│   │   ├── thoth.cpp           # main : -batch | -server | -client | -cluster
│   │   ├── run_server.cpp      # POST /price, GET /health, GET /progress ← contrat BFF
│   │   ├── core/ contracts/ marketdata/ vol_correl/ underlyings/ nodes/ tasks/ fixings/ helpers/
│   ├── tests/                  # doctest
│   ├── samples/                # books YAML de référence (sources d'import)
│   ├── schema/thoth.schema.json# ← pilote forms + validation côté web
│   └── docs/
│
└── web/                        # ── NOUVEAU : dashboard ──
    ├── bff/                    # NestJS (TypeScript)
    │   ├── src/
    │   │   ├── main.ts  app.module.ts
    │   │   ├── auth/           # AuthModule : login/refresh/logout, JwtStrategy, JwtAuthGuard, RolesGuard
    │   │   ├── users/          # UsersModule : CRUD users + rôles (admin-only) — alimente l'onglet Admin
    │   │   ├── engine/         # EngineModule : client thoth -server (postPrice/health/progress)
    │   │   ├── workspaces/     # WorkspacesModule : CRUD books (SQLite)
    │   │   ├── marketdata/     # MarketDataModule : CRUD objets par kind + validation schéma
    │   │   ├── grid/           # GridModule : build book de grille + parse résultats/cellule
    │   │   ├── yaml/           # YamlService : JSON⇄YAML en PRÉSERVANT les tags !xxx
    │   │   ├── schema/         # SchemaService : charge pricer/schema/*, ajv, expose $defs
    │   │   └── persistence/    # entities TypeORM : User, Workspace, ObjectEntity, GridRun
    │   ├── data/thoth.sqlite   # SQLite (gitignored ; migrations versionnées)
    │   ├── Dockerfile          # image NestJS (prod) ; dev = node + watch via compose
    │   ├── nest-cli.json  package.json  tsconfig.json
    └── frontend/               # Angular (TypeScript)
        ├── src/app/
        │   ├── core/           # ApiService, AuthService, JWT interceptor, AuthGuard/RoleGuard
        │   ├── auth/           # page login
        │   ├── market-data/    # onglet éditeur (formly + formly-json-schema)
        │   ├── pricing-grid/   # onglet grille (AG Grid + heatmap + Greeks)
        │   └── admin/          # onglet Admin (gestion users + rôles) — visible role admin
        ├── Dockerfile          # prod : build Angular → nginx ; dev = ng serve via compose
        ├── nginx.conf          # prod : sert dist/ + proxy /api → bff:3000
        ├── angular.json  package.json  tsconfig.json  proxy.conf.json
```

Les 3 images (pricer C++, bff Node, web Angular/nginx) sont partagées entre le devcontainer
(dev, sources montées) et le `docker-compose.yml` racine (prod, images buildées). Le
`pricer/Dockerfile` existant devient multi-stage : un stage *toolchain* (build/dev) et un
stage *runtime* (`thoth -server`).

## Backend NestJS

**EngineModule** — client moteur : `postPrice(yaml, execName="root")` → POST
`${THOTH_ENGINE_URL}/price`, `Content-Type: application/x-yaml`, header `X-Exec-Name`.
**Erreur détectée par corps commençant par `error: `** (chunked toujours HTTP 200) → 422.
`health()`, `progress()`. `THOTH_ENGINE_URL` config (défaut `:8080`).

**YamlService — POINT DUR : préserver les tags `!xxx` au round-trip.** `js-yaml` ignore ces
tags locaux. Schéma `js-yaml` custom avec `Type` générique multi-tag (wrap des mappings
taggés en `{ __tag, ...fields }` au load, ré-émission `!__tag` au dump). **Tests round-trip**
sur `pricer/samples/simple_call.yaml` et `big_option.yaml`. Repli si fragile : stocker objet
JSON + tag, n'émettre le YAML qu'au pricing.

**Persistance SQLite (TypeORM)** — modèle **semi-structuré** (évite un schéma relationnel par
kind) : `User(id, email, passwordHash, role, enabled, …)` · `Workspace(id, name, today,
currency, ownerId?, …)` · `ObjectEntity(id, workspaceId, name, kind, payload JSON)` ·
`GridRun(id, workspaceId, userId, request JSON, result JSON, execMs, …)`.
`MarketDataModule` matérialise un **book YAML** depuis les objets du workspace pour le pricing.

**SchemaService** — charge `pricer/schema/thoth.schema.json`, expose les `$defs` par kind au
front, **valide** (ajv) avant `postPrice`.

**GridModule** — DTO `{ workspaceId, product, underlyings[], types[call|put], strikes[],
maturities[], indicators[] }` → génère **un `!vanilla` par cellule** (`cell__<u>__<type>__<i>__<j>`)
dans **un seul `!book`** (patron `random_vanillas.yaml`) → `postPrice` une fois → parse
`<cell>_premium` (+Greeks) → pivote en matrices `{ underlying, type, rows, cols, values,
greeks }`. Persiste un `GridRun`.

**Routes** (OpenAPI auto, **toutes protégées par `JwtAuthGuard` global sauf `/api/auth/*` et
`/api/health`**) : `POST /api/auth/login|refresh|logout`, `GET /api/auth/me` ;
`GET/POST/PUT/DELETE /api/admin/users` (**`RolesGuard` admin**) ;
`GET/POST/PUT /api/workspaces[/:id]`, `GET/PUT /api/workspaces/:id/objects`, `GET /api/schema`,
`POST /api/grid`, `GET /api/progress`. En prod, le service `web` (nginx) sert le SPA Angular.

## Authentification JWT & gestion des utilisateurs

- **AuthModule** : `@nestjs/jwt` + `@nestjs/passport` + `passport-jwt`. Login (email + mot de
  passe, hash **argon2/bcrypt**) → **access token** JWT court (15 min, en mémoire côté SPA) +
  **refresh token** rotatif en **cookie httpOnly secure** → `POST /api/auth/refresh` renouvelle.
  `JwtAuthGuard` enregistré en **guard global** (`APP_GUARD`) ; routes publiques marquées
  `@Public()`. Secret/TTL via env (`JWT_SECRET`, …).
- **Rôles (RBAC)** : `role ∈ {admin, user}` sur `User` ; `@Roles('admin')` + `RolesGuard` pour
  l'espace admin. **Seeding** : un admin initial créé au bootstrap depuis l'env
  (`ADMIN_EMAIL` / `ADMIN_PASSWORD`) si la table users est vide.
- **UsersModule** (admin-only) : lister / créer / activer-désactiver / changer rôle / reset
  mot de passe — c'est le back de l'onglet **Admin**.
- **Frontend Angular** : `AuthService` (login, stockage access token en mémoire, refresh
  silencieux), **HttpInterceptor** (ajoute `Authorization: Bearer …`, retry sur 401 via
  refresh), `AuthGuard` (routes privées) + `RoleGuard` (onglet Admin masqué hors role admin),
  page **login**. L'onglet Admin propose le tableau des users et les actions CRUD.

## Frontend Angular

- Angular Material + **AG Grid** (grille/heatmap). Formulaires market data via **`@ngx-formly`
  + `@ngx-formly/json-schema`** alimentés par les `$defs` (branche par **kind**, pas l'`anyOf`).
- **Onglet Market Data** : workspace → objets par kind → formulaire schema-driven. Rappels
  métier : **taux & vols en pourcent**, curves = listes `dates`/`values` même longueur, refs
  par **nom**, correl matrix. Save → `PUT` (SQLite).
- **Onglet Pricing Grid** : produit (vanille v1) + workspace ; axes (strikes, maturités),
  call/put + sous-jacents → `POST /api/grid` → **tableau strike×maturité** par (sous-jacent,
  type), **heatmap** AG Grid, toggle Greeks, export CSV, progression via `/api/progress`.

## Charte graphique & UX

Outil pro, clair, dense et lisible. **Design tokens centralisés** (CSS variables +
thème Angular Material *light* + thème AG Grid `quartz` accordé) :

- **Fond** blanc `#FFFFFF` ; surfaces/cartes gris très clair `#F7F9FC` ; bordures `#E5E9F0`.
- **Primaire** bleu professionnel `#2563EB` (actions, liens, en-têtes, calls/positif).
- **Négatif / alertes** rouge `#DC2626` (erreurs, puts, valeurs négatives).
- **Texte** ardoise `#1F2937` (primaire), `#6B7280` (secondaire). Contraste AA.
- **Police** sans-serif pro **Inter** (repli IBM Plex Sans / Roboto). **Chiffres en
  `font-variant-numeric: tabular-nums`** — alignement des colonnes de prix/Greeks (clé en
  finance).
- **Heatmap** échelle **divergente bleu → blanc → rouge** (bas → haut) ; pour les Greeks
  signés, bleu = positif / rouge = négatif. Palette douce (pas de saturation agressive).
- **Densité** : tables compactes, marges maîtrisées, peu de chrome, hiérarchie typographique
  nette, affichage concis et efficace ; nombres formatés (décimales fixes, séparateurs).

## Serveur MCP (exposer le pricer à un agent LLM) — différé

Une **tête supplémentaire** sur le même cœur de pricing, comme le BFF : un serveur **MCP en
TypeScript** (`@modelcontextprotocol/sdk`) qui **wrappe `thoth -server`** (aucune modif C++).

```
Claude Desktop / Claude Code ──MCP (stdio ou HTTP)──▶ Thoth MCP server (TS)
                                                            │ POST /price (YAML)
                                                            ▼  réutilise EngineClient + YamlService
                                                       thoth -server (C++, inchangé)
```

- **Emplacement** : `web/mcp/`, à côté de `web/bff/`. **Factoriser le code commun** (client
  moteur `postPrice`, `YamlService` préservant les tags `!xxx`, builder de grille) dans un
  package partagé **`web/shared/`** importé par le BFF *et* le MCP (zéro duplication).
- **Outils MCP** (schémas **zod** typés, pas de YAML brut côté agent) :
  - `price_vanilla` — `{spot, vol%, rate%, strike, maturity, type, exercise, method}` → mini-book
    façon `simple_call.yaml` → `premium` + Greeks en `structuredContent`.
  - `price_grid` — `strikes[] × maturities[]` (+ `type`, `underlyings[]`) → builder de grille
    (un seul book, une seule requête) → matrices.
  - `price_book` — *escape hatch* : book YAML complet → résultat YAML + JSON parsé (barrières,
    baskets, Heston…).
- **Resources** : `thoth://schema` (sert `pricer/schema/thoth.schema.json`),
  `thoth://samples/{name}` (books de `pricer/samples/` comme templates).
- **Transport** : **stdio** d'abord (usage local Claude Desktop/Code ; lit `THOTH_ENGINE_URL`,
  défaut `http://localhost:8080`). **Streamable HTTP** si partage remote/multi-client → devient
  un **4ᵉ service `mcp`** du `docker-compose` (réseau commun avec `pricer`, + auth Bearer).
- **Setup** : package **ESM** (`"type":"module"`), Node ≥ 18 ; en stdio **logger sur stderr
  uniquement** (`console.error`, jamais `console.log` → corrompt le flux JSON-RPC).
- **Enregistrement** :
  - Claude Code : `claude mcp add --transport stdio thoth -- node web/mcp/dist/index.js`
    (option `--env THOTH_ENGINE_URL=…`).
  - Claude Desktop : entrée `mcpServers.thoth` (`command: node`, `args: [.../dist/index.js]`,
    `env`) dans `claude_desktop_config.json`, puis redémarrer.
- **Pièges moteur** : pricing **sérialisé** (un prix à la fois → file d'attente en HTTP
  partagé) ; réponse `/price` **toujours HTTP 200** (chunked), erreur = corps commençant par
  `error: ` → mapper en `isError: true`.
- **Note de version** : caler les signatures exactes (`registerTool`, forme de `inputSchema`)
  sur le README de la version du SDK épinglée dans le `package.json` (l'API bouge).

## Dev containers (3 conteneurs, 1 techno chacun)

`.devcontainer/docker-compose.yml` définit **3 services** sur un même réseau, joignables par
nom de service, sources montées en bind-mount pour l'édition live :

- **`pricer`** — image C++ (toolchain : gcc/cmake/boost/yaml-cpp/cpp-httplib, CUDA optionnel ;
  stage *toolchain* du `pricer/Dockerfile`). Commande dev : build puis `thoth -server 8080`.
- **`bff`** — image Node. Commande dev : `nest start --watch` (3000). `THOTH_ENGINE_URL=
  http://pricer:8080`. Volume nommé pour `data/thoth.sqlite`.
- **`web`** — image Node (dev) : `ng serve --host 0.0.0.0` (4200), `proxy.conf.json` proxifie
  `/api` → `http://bff:3000`. En prod, ce service devient nginx servant `dist/` + proxy `/api`.

`.devcontainer/devcontainer.json` : `dockerComposeFile` pointant ce compose,
`runServices: [pricer, bff, web]` (les 3 montent ensemble). **Note importante** : VS Code
attache l'éditeur à **un seul** service à la fois (`"service"` configurable, défaut `bff`) —
pour le tooling C++ *et* TS, on attache au conteneur sur lequel on travaille, ou on ouvre une
fenêtre VS Code par conteneur (chacun ses extensions via `customizations.vscode`). Le repo
entier est monté dans chaque service, donc l'édition est cohérente quel que soit l'attache.

## Déploiement (prod)

`docker-compose.yml` racine — **mêmes 3 services**, images buildées : `pricer` (stage runtime
du `pricer/Dockerfile`, `thoth -server`), `bff` (NestJS, volume `thoth.sqlite`), `web` (nginx
servant Angular buildé + proxy `/api` → `bff`). `web` est le point d'entrée navigateur ; le
SPA appelle `/api` → `bff` → `pricer`.

## Tests

Round-trip YAML (YamlService) sur samples ; grille comparée à un `postPrice` direct ; smoke
e2e Playwright optionnel.

## README / CLAUDE.md (mandat)

- `pricer/README.md` : reste la doc détaillée du moteur (évolutions moteur y vont).
- `README.md` racine (nouveau) : carte monorepo, lancement des 3 process, section **Web
  dashboard**.
- `CLAUDE.md` : MAJ *Build & test* + *Layout* (chemins `pricer/…`) et le mandat README.

## Vérification end-to-end

1. Après `git mv` : `cmake -B pricer/build pricer && cmake --build pricer/build -j` puis
   `ctest --test-dir pricer/build` et `pricer/format.sh --check` → le moteur builde/teste
   identiquement depuis sa nouvelle place ; CI verte.
2. `thoth -server 8080` + `curl -s -XPOST --data-binary @pricer/samples/simple_call.yaml
   http://localhost:8080/price` → premium ~15.71.
3. **Auth** : `GET /api/workspaces` sans token → 401 ; `POST /api/auth/login` (admin seedé)
   → access token + cookie refresh ; route privée OK avec `Bearer` ; `POST /api/admin/users`
   en tant que `user` → 403 ; refresh renouvelle l'access token.
4. NestJS up : créer workspace, éditer objet, relire → SQLite + round-trip YAML sémantiquement
   identique (tags préservés).
5. `POST /api/grid` (qq strikes × qq maturités, call+put) → matrices cohérentes ; recouper une
   cellule avec un `postPrice` direct.
6. UI : login → onglet Admin visible pour admin / masqué pour user ; éditer spot/vol, sauver,
   relancer la grille → prix bougent dans le sens attendu.
