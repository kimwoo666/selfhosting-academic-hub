# Security Policy

## Supported Use

This repository is intended for self-hosted deployments. Treat all local integration credentials as sensitive.

## Reporting a Vulnerability

Do not open a public issue with:

- API keys
- Proxmox tokens
- university credentials
- database dumps
- crawler debug snapshots containing personal data

If you find a vulnerability, report it privately to the project owner and rotate any exposed credential immediately.

## Secret Handling Rules

- Store live secrets only in `config.local.json`
- Do not commit `config.local.json`
- Do not commit `.env.local`
- Do not commit `jarvis.db`
- Rotate credentials that were ever placed in a tracked config file
