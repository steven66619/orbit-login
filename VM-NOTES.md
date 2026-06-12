# orbit-test VM

Arch Linux VM with **orbit-login pre-installed and running**.

## Start

```bash
virsh start orbit-test
```

## Connect

**VNC viewer:**
```bash
vncviewer 127.0.0.1:5900
```
You'll see the orbit-greeter login screen.

**SSH:**
```bash
ssh -p 2222 root@localhost
```
or `ssh -p 2222 orbit@localhost`

## orbit-login

- **Daemon:** `/usr/local/sbin/orbitd`
- **Greeter:** `/usr/local/libexec/orbit-greeter`
- **Config:** `/etc/orbit-login.conf`
- **Service:** `orbitd.service` (enabled as display-manager)

Already running on display :7, VT 7.

## VM specs

- 2 vCPUs, 4GB RAM, 20GB disk
- BIOS boot (SeaBIOS)
- Network: user-mode NAT, SSH forwarded to host port **2222**
- VNC: **127.0.0.1:5900**

## Rebuild

```bash
ssh -p 2222 root@localhost
cd /opt/orbit-login
make && make install
systemctl restart orbitd
```

## Login credentials

- `root` / `orbit`
- `orbit` / `orbit`
