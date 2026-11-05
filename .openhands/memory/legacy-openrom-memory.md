# Project memory (OpenROM Marketplace)

Repo: `M5Devs/OpenROM-Plugins-Store` (working dir name `OpenROM-Plugins-Store`).
Astro 5 SSR storefront on **Cloudflare Pages + D1**, Tailwind 4, no UI framework.
Full-stack: every route is `prerender = false`; the catalog is read live from the
D1 binding `DB` at request time.

## Build / test
- `npm install` (the container resets between sessions, so re-install).
- `npm run build` — baseline passes; the adapter prints two harmless startup
  lines ("Enabling sessions with Cloudflare KV…") that are **pre-existing** and
  appear even on the untouched repo (the adapter auto-configures a SESSION KV
  binding; this project does not use `Astro.session`).
- `npm test` — `node --experimental-strip-types --test "tests/*.test.ts"`. Node
  22/24 strip TS types natively, so no test dependency is needed. `node --test`
  with a bare directory does **not** discover `.ts` files; pass a glob.
- `npx tsc --noEmit -p tsconfig.json` — `astro.config.mjs` reports a
  pre-existing vite-plugin type error (two vite copies); ignore it. `src/` is clean.

## Local Cloudflare runtime (how to actually exercise D1 + env vars)
- `wrangler.toml` holds the D1 binding; `astro.config.mjs` `platformProxy`
  emulates it for `astro dev`. Put secrets in `.dev.vars` (git-ignored):
  `GITHUB_CLIENT_ID`, `GITHUB_CLIENT_SECRET`, `SESSION_SECRET`.
- `npx wrangler d1 migrations apply openrom-store --local` (non-interactive
  prompts auto-answer yes). Seed: `npm run db:seed:local`.
- To test the **built** worker: `npm run build && npx wrangler pages dev dist
  --port 8788`. This is the closest to production and reads `.dev.vars`.
- Quirk: Cloudflare Pages rejects a POST with **no `Origin` header** with
  `403 "Cross-site POST form submissions are forbidden"`. curl must send a
  matching `Origin` to exercise POST endpoints (e.g. logout); browsers do this
  automatically. Not a bug in our code.
- D1 has no `publishers` row until one is created; `--local` state persists in
  `.wrangler/state/v3`.

## Auth architecture (added 2026-11-03)
- Migration `migrations/0002_auth_and_publishers.sql`: `users` (github_id TEXT
  UNIQUE = stable key) + `publishers` (TEXT PK slug, `owner_github_id` FK,
  `verified` BOOLEAN).
- `src/lib/session.ts`: **stateless HMAC-SHA256 signed cookie** (Web Crypto, not
  a KV session). Payload is public profile JSON + `iat`; signature is what
  matters. Cookie `openrom_session`, HttpOnly + SameSite=Lax + Secure on https,
  30-day Max-Age. Secret = `SESSION_SECRET` fallback `GITHUB_CLIENT_SECRET`.
  Verify uses constant-time compare; rejects forged payload, forged signature,
  expired `iat`, malformed tokens.
- `src/lib/github.ts`: authorize URL (`scope=read:user`, `state`), token
  exchange (POST JSON), `/user` profile. GitHub API needs a `User-Agent`.
- `src/lib/users.ts` / `src/lib/publishers.ts`: D1 access + pure helpers
  (`validatePublisherId`, `isVerifiableFor`, `toPublisher`).
- Routes: `GET /api/auth/login` (302 + `state` cookie), `GET /api/auth/callback`
  (validates state, upserts user, issues session, 302 `/dashboard`),
  `POST /api/auth/logout` (303, clears cookie), `POST /api/publishers/create`.
- **Auto-verify rule**: publisher slug matching the owner's GitHub username
  (case-insensitive) → `verified = 1`, else 0. Enforced server-side.
- `Navbar.astro` reads the session server-side (HttpOnly → server-only) and
  renders avatar + dropdown; `/dashboard` redirects to login when anonymous.
- Tests: `npm test` → `node --experimental-strip-types --test "tests/*.test.ts"`
  (14 tests, no deps needed). Covers session sign/verify/tamper/expiry and
  publisher slug validation + auto-verify. `tests/` is excluded from the app
  `tsconfig.json` (needs Node types the app omits).
- **Gotcha (learned the hard way):** Astro + the Cloudflare adapter already
  append `cookies.set`/`cookies.delete` onto whatever response the handler
  returns (`App.setCookieHeaders` → `handler.js`). So an endpoint must return a
  plain `context.redirect(...)` and must NOT copy `cookies.headers()` itself —
  doing so emits a duplicate `Set-Cookie`. Use `context.redirect`, not
  `Response.redirect` (the latter discards the attached cookies).
- Real network path verified: with fake creds the callback hits GitHub and
  returns 502 cleanly; the full success path was validated against a local mock
  by temporarily repointing the two URLs in `src/lib/github.ts` (revert after!).

## Publisher profiles (added 2026-11-03, task 2)
- Migration `migrations/0003_publisher_profiles.sql` adds the branding columns to
  `publishers` (`bio`, `avatar_url`, `banner_url`, `website_url`, `github_url`,
  `discord_url`, `twitter_url`; all TEXT DEFAULT '') **and** `plugins.publisher_id`
  TEXT + index. Important: 0001 never linked plugins to publishers (only the
  denormalized `publisher_name`), so `publisher_id` is what makes the profile
  aggregation and clickable names possible. 0003 backfills it for the seed.
- `db.ts` now selects plugins with a `LEFT JOIN publishers` and
  `COALESCE(pub.name, p.publisher_name)` / `COALESCE(pub.verified,
  p.publisher_verified)`, so renaming a publisher updates every card live and
  legacy rows still render. `publisherId` was added to `Plugin`.
- `PUBLISHER_MATCH` = `(p.publisher_id = ?1 OR (p.publisher_id IS NULL AND
  p.publisher_name = ?2))` — used by `listPluginsByPublisher` / `getPublisherStats`.
- New: `src/lib/publisher-profile.ts` (`getPublisherProfile`), `GET`/`PATCH
  /api/publishers/[id].ts`, `src/pages/publisher/[id].astro`,
  `src/pages/dashboard/publisher/[id].astro`.
- PATCH is owner-only (403 otherwise) and only honours an allow-list of fields;
  `id`/`verified`/`owner_github_id` from the body are ignored. Optional URLs must
  be absolute http(s) (blocks `javascript:`/`data:`).
- `DetailDrawer.astro` owned its own client script (open/close/tabs/?ext=); it
  was removed from `index.astro` so the publisher page reused it unchanged.
  **Superseded**: the drawer was deleted entirely in favour of `/plugin/[id]`.
- Tests: `tests/publisher-profile.test.ts` (9 more; suite now 23).
- Shipped as **PR #9**, squash-merged to `main` as `92d6231` (2026-11-03). CI:
  Cloudflare Pages + Build Astro site + Validate manifests all pass; CodeRabbit
  is non-blocking. Merge via the async API: `gh api -X PUT
  repos/OWNER/REPO/pulls/N/merge-async -f merge_method=squash -f sha=<head>`,
  then poll `.../merge-async/<uuid>` until status is `merged`.

## User profiles + image uploads (added 2026-11-03, task 3)
- Migration `0004_user_profiles.sql` adds `users.profile_customized INTEGER NOT NULL
  DEFAULT 0`. Reason: `upsertUser` refreshes `name`/`avatar_url` from GitHub on every
  login, which would wipe a custom avatar. The upsert now uses
  `CASE WHEN users.profile_customized = 1 THEN users.<col> ELSE excluded.<col> END`.
- **The custom avatar lives in D1, never in the session cookie.** The session is a
  stateless HMAC cookie (~4 KB limit); a base64 avatar blows past it. `toUserProfile`
  (in `users.ts`) overlays the D1 row on the session at render time. `Layout.astro`
  loads it (wrapped in try/catch so a DB hiccup falls back to the session), and
  `Navbar`'s prop type is now a local `NavbarUser` interface, not `SessionUser`.
- Image uploads are **client-side only — no `/api/upload` endpoint**:
  - `src/lib/image.ts` — pure, shared by browser + worker. `MAX_IMAGE_BYTES = 1.5e6`
    (D1's hard row limit is 2 MB). Parses PNG (IHDR), JPEG (SOF marker walk), WebP
    (VP8/VP8L/VP8X). `validateImageDataUrl` checks magic bytes vs declared MIME, size,
    dimensions. Avatars cap 512px, banners 1600px. Decoded cap `MAX_IMAGE_BYTES`
    is 600 KB **and** `MAX_ENCODED_BYTES` (800 KB) bounds the encoded string — what
    D1 actually stores is base64 text, ~4/3 the decoded size, and a publisher row
    holds both an avatar and a banner under D1's 2 MB row limit.
  - `src/lib/image-client.ts` — browser-only (`<script>` imports only, uses DOM).
    Decodes → downscales → `canvas.toDataURL('image/webp', q)` → shrinks scale until it
    fits. Result is both the preview and the submitted value.
  - `src/lib/media.ts` — `validateImageOrUrl` routes `data:` → image validator, else
    → `validateOptionalUrl`.
  - `src/components/ImageField.astro` — file input + URL box + preview + hidden value;
    its own client `<script>` calls `initImageField`.
- **Gotcha:** Node's `--experimental-strip-types` ESM loader does NOT resolve
  extensionless relative imports. `src/lib/media.ts` imports `./image.ts`/`./publishers.ts`
  **with extensions** so `tests/user-profile.test.ts` can import it. The Astro/Vite build
  accepts the explicit `.ts` extension fine.
- Routes: `PATCH /api/user/profile` (401 anon; name ≤80; avatar image-or-URL). Publisher
  `PATCH /api/publishers/[id]` now validates avatar/banner via `validateImageOrUrl`
  (social links stay URL-only).
- New pages: `/dashboard/settings` (name + avatar), navbar "Account settings" link.
- Tests: `tests/user-profile.test.ts` (18 tests; suite now **41**). Uses a real 1×1 PNG
  and synthetic RIFF/VP8X WebP headers.
- **Hard-won bug (fixed in #10):** the auth callback must build the session cookie
  from the *GitHub profile*, never from the row `upsertUser` returns — that row can
  hold a multi-MB avatar data URL, which blew the cookie to ~1 MB and browsers reject
  anything over ~4 KB, silently breaking re-login after an avatar upload. Use
  `sessionUserFromGitHubProfile(profile)`. Custom values live in D1 and are overlaid
  at render time by `toUserProfile`.
- **Merge convention:** repo history is squash-merged PRs titled `... (#N)`. `gh` is
  NOT authenticated in this container; the remote's embedded token is expired. Push and
  call the API with `GITHUB_TOKEN` explicitly (never store it in the remote config).
  Merge via `PUT /pulls/N/merge-async` with the **full 40-char** head SHA (short SHA →
  400 "head branch was modified").

## /publisher/[id] 500 — root cause & fix (2026-11-03)
- Symptom: `/publisher/<id>` returned an unhandled **500**; `/` stayed 200.
- Cause: the page called `getPublisherProfile()` with no `try/catch`. When the
  `publishers` table is missing (an unapplied remote migration), D1 throws
  `D1_ERROR: no such table: publishers: SQLITE_ERROR`, the exception escapes the
  frontmatter, and Cloudflare returns 500. Production also 404'd (empty DB), so the
  500 was never visible from here — the empty-data 404 masked it.
- **Underlying operational gap:** nothing in `.github/workflows/` applies migrations
  to remote D1. Every schema change needs a manual `npm run db:migrate:remote`.
- Fix: `loadPublisherProfile()` in `src/lib/publisher-profile.ts` returns a tagged
  result `{status:'ok'|'not-found'|'error'}` instead of collapsing every failure to
  `null`. Consumers render 503 (error) / 404 (not-found) / 200, with a fallback page.
- Repo-wide convention: **every** D1 read is wrapped — `index.astro`,
  `dashboard.astro`, `plugins.json.ts`, `download/[id].ts` already did this. Keep it.
- Reproducing it locally: `npx wrangler d1 execute openrom-store --local --command
  "DROP TABLE publishers;"` then hit the route. Note re-applying migrations will NOT
  recreate the table (it is already recorded in `d1_migrations`), so recreate the
  table by hand or delete `.wrangler/state/v3/d1` and re-migrate/seed.
- `src/lib/publisher-profile.ts` now imports `./db.ts` / `./publishers.ts` **with
  extensions** so `tests/publisher-profile-page.test.ts` can load it under Node's
  `--experimental-strip-types` (same quirk as `media.ts`).
- **`getDB(locals)` itself throws** when the `DB` binding is missing. Guarding the
  *call* is not enough: `loadPublisherProfile(getDB(locals), id)` evaluates `getDB`
  as an argument, i.e. BEFORE the function's `try` — a missing binding still 500s.
  Pass a getter: `loadPublisherProfile(() => getDB(locals), id)`. General rule for
  this repo: anything that can throw must be evaluated *inside* the guarded region,
  which means lazy getters, not pre-resolved values. Caught by CodeRabbit on PR #11.
- Local repro of a missing binding: comment out the `[[d1_databases]]` block in
  `wrangler.toml`, rebuild, `wrangler pages dev` — then restore the file.
- PR #11 (`fix/publisher-route-500`): this fix. All D1 failure modes now return
  503 with a fallback rather than 500.

## Legacy static-file cleanup (2026-11-03)
- Removed `verified_publishers.json`, `publishers/`, `packages/` (only a `.gitkeep`),
  and `scripts/package_plugins.py`. Commit `8a9024a` on `chore/remove-legacy-static-files`.
- Follow-up (commit `8c58381`): removed the last static sources — `manifests/`,
  `scripts/validate_plugin.py`, `tests/test_security.py` — and dropped the
  `Validate manifests` CI job. (Earlier note calling `validate_plugin.py`
  non-legacy is now superseded: it validated static manifests, which no longer
  exist.) The CI command set is now just `npm run build` and `npm test`.
- Repo git convention: branch off `origin/main`, squash-merge, PR title carries `(#N)`.

## SourceForge storage (learned 2026-11-04, from live probing)
- Public download host: `https://downloads.sourceforge.net/project/openrom-plugins/`.
  It serves **files only** — a *directory* URL always 404s, even for projects
  that do have files (verified against sevenzip). Never treat that 404 as an
  outage; use `https://sourceforge.net/projects/<p>/files/` as the health check.
- FRS upload: `$SF_USER@frs.sourceforge.net:/home/frs/project/openrom-plugins/`.
  This dir exists and is writable; `/home/frs/project/openrom-binaries/` also
  exists but has **no `plugins/` subdir** (old task paths that pointed there
  fail). `openrom-binaries` holds android/linux/macos/windows dirs.
- Auth: SourceForge offers `publickey,keyboard-interactive`; the password flow is
  **keyboard-interactive**. `ssh -o BatchMode=yes` therefore fails ("Permission
  denied (publickey,keyboard-interactive)"). Use
  `sshpass -e ssh -o PreferredAuthentications=keyboard-interactive -o PubkeyAuthentication=no`.
- FRS **rejects hidden dotfiles** (`.sf_probe`): upload fails with
  `rename ... Bad address (14)`. Use a normal filename (probe uses
  `sf_probe_healthcheck.txt`).
- Remote rsync server is old (3.1.4dev) — no `--mkpath`. Target dirs must exist.
- Safe single-file delete: `rsync --delete-missing-args --ignore-missing-args`
  with the local source removed first. **Never** `--delete-excluded` (it deleted
  a pre-existing remote `README (5).md` — restored from the byte-identical repo
  README.md). Remote `ssh <cmd>` is blocked by the restricted shell.
- `scripts/test_sourceforge_storage.sh` (alias `test-sourceforge.sh`) implements
  all of the above. `rsync` may need manual install if the snapshot mirror 503s
  (grab the .deb from deb.debian.org/pool/main/r/rsync/).
- **Creating new FRS directories**: the restricted shell has no `mkdir`, but
  `rsync -a emptydir/ user@frs:.../parent/newdir/` creates `newdir` (one level;
  parent must exist). Build a deep path **one segment at a time, top-down**
  (`<secret-hidden>` → `<secret-hidden>/nkit` → `.../3.2.0`), otherwise the later rsyncs fail.
- The public mirror serves a freshly uploaded file **immediately** (no
  propagation lag observed) — a 200 right after upload is expected, not luck.

## NKit plugin packaging (2026-11-04)
- Upstream `Nanook/NKit` is **C#/.NET** (not C++); releases are GitHub-Actions-built
  **per-platform CLI zips** (`NKit_<os>_CLI_<ver>.zip`), each containing a
  self-contained `nkit` executable + `defaults/`, `nkit.yaml`, `ConfigInfo.txt`.
  The binary runs **standalone** for `convert` without those siblings (it falls
  back to "system mode" and creates config on first run), so the bundle ships
  only the four executables. MIT-licensed, Copyright (c) 2018 Nanook.
- Linux binary needs **libicu** or `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1`
  (the container lacks libicu) — a runtime-env issue, not a bundle defect.
- `scripts/package_orplug.py` writes a **deterministic** ZIP (sorted entries,
  fixed 1980 timestamp, exec bit preserved) so rebuilding reproduces the digest.
  Entries sit at the **archive root** (`plugin.json`, `bin/...`).
- NKit 3.2.0 bundle sha256 `bd62db8e…f717`; published at
  `downloads.sourceforge.net/project/openrom-plugins/<secret-hidden>/nkit/3.2.0/nkit-3.2.0.orplug`.
- Registration is `migrations/0005_nkit_plugin.sql`: upserts the plugin, the
  `<secret-hidden>` publisher, and the owning user (`ClausValcaTD` / github_id
  `193546948`), updating every column **except `downloads`** so re-applying never
  resets the counter.
- **No Cloudflare credentials exist in this environment** — `wrangler whoami`
  says not authenticated. Remote D1 writes are impossible here; the maintainer
  must run `npm run db:migrate:remote`. Only `--local` D1 is usable.
- Storefront detail pages are **server-rendered at `/plugin/[id]`**
  (`src/pages/plugin/[id].astro`, uses `getPlugin()` — a parameterized single-row
  lookup). The old client-side `DetailDrawer` (?ext=) was **removed**: it cloned a
  panel per plugin into `<template>` tags and animated a fixed overlay, which
  froze touch nav and locked scroll on mobile. `?ext=`/`?id=` now 301-redirect to
  the page. Missing id → 404, D1 failure → 503 (mirrors the publisher page).
- `Card.astro` makes the whole card a link via a stretched `after:absolute`
  pseudo-element on the title anchor; the publisher link and action buttons carry
  `relative z-10` so they stay independently clickable (avoids nested `<a>`).
- **1-click install** is a custom-protocol deep link `openrom://plugin/<id>` on the
  primary "Install in OpenROM" button (`zap` icon, added to `Icon.astro`).
  `Download .orplug` + `Copy CLI Install` are the fallbacks. The deep link **and**
  the download button are both withheld when `sha256` is empty (no dead install).

## ROMeo mascot artwork (added 2026-11-04)
- Assets live in `public/images/`: `romeo-logo.png` (square, brand/favicon),
  `romeo-banner.png` (16:9, hero + `og:image`), `romeo-search.png` (empty states),
  `romeo-error.png` (404 / unavailable).
- **Quirk**: the square source icons came on a **pure-white background with a ~50px
  margin** (18% of pixels). Never drop them straight in — the site is `zinc-950`,
  so they'd render as white tiles. Prep = trim + **border flood-fill** white→alpha
  (a naive "white→transparent" pass punches holes through ROMeo's white eyes/gloves),
  colour-bleed the AA edge to avoid a white halo, downscale to 512² (squares) /
  1200w (banner). ~4.2MB → ~1.9MB.
- Wired: `Layout.astro` favicon + apple-touch + og:image; `Navbar.astro` brand img;
  `index.astro` two-column hero + both empty states (server `total===0` block and the
  client `[data-no-results]` panel); **`src/pages/404.astro` is new** (sets
  `Astro.response.status = 404`); `plugin/[id].astro` + `publisher/[id].astro`
  not-found fallbacks.
- **Two distinct fallbacks** on detail pages: missing id → "Whoops! ROMeo got lost in
  the bitstream (404)" + 404; genuine D1 failure → "Plugin/Publisher unavailable" +
  **503**. ROMeo art shows on both.
- **Favicon**: `romeo-logo.png` is a **dark navy badge** (77% near-black, dominant
  `rgb(1,5,43)`, luminance ~7) — pointed at directly as `rel="icon"` it is
  **invisible on a dark tab bar** (at 16px, 0% of pixels exceed luminance 140).
  Fixed by wrapping the art in a cyan→purple rounded frame:
  `public/favicon.ico` (16/24/32/48/64) + `public/images/romeo-favicon.png` (512,
  transparent) + `public/images/romeo-touch-icon.png` (180, **opaque** — iOS
  composites transparency onto black). Always sanity-check favicon legibility by
  compositing on BOTH white and `#202124` and measuring std/bright% before shipping.
- `public/favicon.svg` is now unreferenced (left in place).

## Conventions
- `.dev.vars`, `.wrangler/`, `node_modules/`, `dist/` are git-ignored.
- `.openhands/memory/legacy-openmotion-memory.md` is stale content from an
  unrelated C++ project that previously occupied this path; ignore it.
- Static manifest/publisher sources are gone: `manifests/`, `publishers/`,
  `packages/`, `verified_publishers.json`, `scripts/validate_plugin.py`,
  `tests/test_security.py` were all removed (D1 + SourceForge replaced them).
  CI (`.github/workflows/build_catalog.yml`) now only builds; no validate job.
