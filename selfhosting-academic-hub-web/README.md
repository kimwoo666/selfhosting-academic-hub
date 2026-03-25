# Self-Hosting Academic Hub Frontend

React + TypeScript frontend for the Self-Hosting Academic Hub academic dashboard.

## Commands

```bash
npm install
npm run dev
npm run build
npm run preview
```

## Notes

- Production output is written to `dist/`
- The compiled frontend is served by the C++ backend
- Default local setup is driven from the root `config.local.json`
- `.env.local` is not required for the default development flow

## API Assumption

During normal development the app expects the backend APIs under `/api/*`.
