# Jarvis-Cpp

Jarvis-Cpp is a C++20 backend and React frontend for a personal academic operations dashboard focused on:

- Proxmox infrastructure monitoring and control
- Soongsil University academic integrations (`u-SAINT`, LMS, notices)
- AI-assisted notice triage and academic summaries
- A single local web app served by one backend process

The project is designed for a Proxmox LXC deployment target, but it can also be developed locally.

## Stack

- Backend: C++20, Crow, SQLite, libcurl
- Frontend: React 19, TypeScript, Vite
- Crawlers: Python
- AI integration: Gemini-compatible HTTP client

## Repository Layout

```text
include/                     C++ headers and service modules
src/                         Crow server entrypoint
scripts/                     Python crawlers
tests/                       Parser and backend smoke tests
jarvis-cpp-academic-hub/     React frontend
build.sh                     End-to-end Linux build script
config.example.json          Public config template
config.local.json            Local-only secrets and machine settings
```

## Current Product Scope

- Dashboard summary API for infrastructure and alerts
- `u-SAINT` notice crawling and AI-based prioritization
- LMS login and sync flow
- `u-SAINT` grade sync, semester history persistence, and React dashboard rendering
- Proxmox node summary and control endpoints

## Quick Start

### 1. Prepare backend config

Copy the public template to a local config file:

```bash
cp config.example.json config.local.json
```

Fill in local secrets in `config.local.json`.

`config.local.json` is intentionally ignored by Git. The backend will load it first if present.

### 2. Frontend development

```bash
cd jarvis-cpp-academic-hub
npm install
npm run dev
```

### 3. Frontend production build

```bash
cd jarvis-cpp-academic-hub
npm install
npm run build
```

### 4. Full Linux build

On Ubuntu 24.04 / Proxmox LXC:

```bash
chmod +x build.sh
./build.sh
```

## Configuration

The backend resolves config in this order:

1. `JARVIS_CONFIG` environment variable
2. `config.local.json`
3. `config.json`

Use `config.json` and `config.example.json` as public templates only. Put live tokens and local credentials in `config.local.json`.

## Security Notes

- Never commit `config.local.json`
- Never commit `jarvis.db`
- Never commit frontend `.env.local`
- Rotate any credential that was ever stored in a tracked config file

## Development Notes

- The frontend bundle is served by the C++ backend from `server.build_dir`
- The grade pipeline now persists semester summaries and course history separately
- `u-SAINT` crawling still depends on the live portal DOM, so selector regressions should be expected over time

## Repository Hygiene

- CI is defined in `.github/workflows/ci.yml`
- Contribution expectations are documented in `CONTRIBUTING.md`
- Secret handling rules are documented in `SECURITY.md`
- Parser tests live under `tests/python`
- Backend config and database smoke tests run through CTest

## Known Gaps

- The repository still lacks broad automated backend API tests
- The `u-SAINT` crawler should be moved to an async job model instead of blocking request flow
- The Linux service/deploy story is documented, but a full release pipeline is not added yet
