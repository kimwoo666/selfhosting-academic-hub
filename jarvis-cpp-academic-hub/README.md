# Jarvis Frontend

React + TypeScript frontend for the Jarvis-Cpp academic dashboard.

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
- Local frontend-only secrets should go in `.env.local`
- Do not commit `.env.local`

## API Assumption

During normal development the app expects the backend APIs under `/api/*`.
