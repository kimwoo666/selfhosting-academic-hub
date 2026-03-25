# Contributing

## Before You Start

- Keep secrets in `config.local.json`
- Do not commit `config.local.json`, `academic_hub.db`, or frontend `.env.local`
- Use `config.example.json` as the public template

## Local Checks

### Frontend

```bash
cd selfhosting-academic-hub-web
npm ci
npm run build
```

### Python crawler

```bash
python -m py_compile scripts/crawl_grades.py
python -m unittest discover -s tests/python -p "test_*.py"
```

### Backend

```bash
cmake -S . -B build_cpp -DCMAKE_BUILD_TYPE=Release
cmake --build build_cpp --parallel
ctest --test-dir build_cpp --output-on-failure
```

Current CTest coverage includes:

- config resolution smoke test
- database grade round-trip smoke test

## Pull Request Expectations

- Keep changes scoped and explain the user-visible impact
- Update docs when changing setup, config, or deployment behavior
- Preserve the separation between public templates and local-only config
- Avoid adding new hardcoded credentials, URLs, or personal data
