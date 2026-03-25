# Self-Hosting Academic Hub Deployment Tutorial

This guide covers a clean deployment of Self-Hosting Academic Hub on a Proxmox LXC container running Ubuntu 24.04.

## 1. Create the LXC

Recommended baseline:

- Ubuntu 24.04 standard template
- 2 CPU cores
- 2 GB RAM
- 8 GB disk
- Internet access enabled

Example on the Proxmox host:

```bash
pveam download local ubuntu-24.04-standard_24.04-2_amd64.tar.zst

pct create 200 local:vztmpl/ubuntu-24.04-standard_24.04-2_amd64.tar.zst \
  --hostname academic-hub \
  --cores 2 \
  --memory 2048 \
  --rootfs local-lvm:8 \
  --net0 name=eth0,bridge=vmbr0,ip=dhcp \
  --start 1
```

## 2. Create a Proxmox API Token

Example:

```bash
pveum user token add root@pam academic-hub --privsep=0
```

Store the token securely in `config.local.json`. Do not place live credentials in `config.json`.

## 3. Copy the Project into the Container

Example from a local machine:

```bash
scp -r /path/to/selfhosting-academic-hub root@<LXC_IP>:/opt/selfhosting-academic-hub
```

## 4. Prepare the Local Config

Inside the container:

```bash
cd /opt/selfhosting-academic-hub
cp config.example.json config.local.json
```

Fill in:

- Proxmox host/user/token
- Gemini API key
- Optional LMS and Google Calendar credentials

## 5. Run the Full Build

```bash
cd /opt/selfhosting-academic-hub
chmod +x build.sh
./build.sh
```

What this does:

- installs system dependencies if needed
- installs frontend dependencies
- builds the React frontend
- copies the bundle into `build/`
- configures and builds the C++ backend
- starts `build_cpp/academic-hub-server`

## 6. Run as a Service

Example:

```bash
cp academic-hub.service /etc/systemd/system/academic-hub.service
systemctl daemon-reload
systemctl enable academic-hub
systemctl start academic-hub
systemctl status academic-hub
```

## 7. Post-Deploy Checklist

- confirm the frontend loads on port `8080`
- confirm `/api/summary` responds
- confirm `config.local.json` is present and not committed
- confirm the frontend bundle exists under `build/`
- confirm the database file is created in the expected working directory

## 8. Operational Notes

- The backend resolves config in this order:
  1. `ACADEMIC_HUB_CONFIG`
  2. `config.local.json`
  3. `config.json`
- `u-SAINT` crawling depends on live page structure and may require selector updates over time
- Rotate any credential that was previously stored in a tracked file before publishing the repository
