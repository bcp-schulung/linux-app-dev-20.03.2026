#!/usr/bin/env bash
set -euo pipefail

# Simple multi-VM deployment for Ubuntu hosts.
# Installs code-server (browser IDE), creates a dedicated user,
# and configures password auth per VM.

SSH_USER="root"
SSH_KEY_PATH="${HOME}/.ssh/id_rsa"
VMS_FILE="vms.txt"
PASSWORDS_FILE="vm_passwords.csv"
IDE_USER="devuser"
IDE_PORT="8080"
HARNESS_REPO_URL="https://github.com/it-scholar/linux-application-development.git"
HARNESS_REPO_SUBDIR="test-harness"
HARNESS_SRC_DIR="/opt/linux-application-development"
HARNESS_BIN_PATH="/usr/local/bin/harness"
HARNESS_SYMLINK_PATH="/usr/local/bin/test-harness"
GITHUB_TOKEN="${GITHUB_TOKEN:-}"
DRY_RUN="false"
PARALLEL="false"
PARALLEL_JOBS="3"
VERIFY="false"

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --dry-run         Print what would be done, without making changes
  --parallel        Deploy to multiple VMs in parallel
  --jobs N          Number of parallel jobs (default: 3, used with --parallel)
  --verify          Verify service health after deployment
  -h, --help        Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN="true"
      shift
      ;;
    --parallel)
      PARALLEL="true"
      shift
      ;;
    --verify)
      VERIFY="true"
      shift
      ;;
    --jobs)
      if [[ $# -lt 2 ]]; then
        echo "[ERROR] --jobs requires a number"
        exit 1
      fi
      PARALLEL_JOBS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

if ! [[ "$PARALLEL_JOBS" =~ ^[0-9]+$ ]] || [[ "$PARALLEL_JOBS" -lt 1 ]]; then
  echo "[ERROR] --jobs must be a positive integer"
  exit 1
fi

if [[ ! -f "$VMS_FILE" ]]; then
  echo "[ERROR] Missing $VMS_FILE"
  exit 1
fi

if [[ ! -f "$PASSWORDS_FILE" ]]; then
  echo "[ERROR] Missing $PASSWORDS_FILE"
  echo "Create it as: ip,password"
  exit 1
fi

# Auto-correct if a public key path was provided by mistake.
if [[ "$SSH_KEY_PATH" == *.pub ]]; then
  maybe_private="${SSH_KEY_PATH%.pub}"
  if [[ -f "$maybe_private" ]]; then
    SSH_KEY_PATH="$maybe_private"
  fi
fi

if [[ ! -f "$SSH_KEY_PATH" ]]; then
  echo "[ERROR] SSH private key not found: $SSH_KEY_PATH"
  exit 1
fi

get_password_for_ip() {
  local ip="$1"
  awk -F',' -v vm_ip="$ip" '
    BEGIN { found=0 }
    /^[[:space:]]*#/ { next }
    NF >= 2 {
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $1)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
      if ($1 == vm_ip) {
        print $2
        found=1
        exit
      }
    }
    END { if (!found) exit 1 }
  ' "$PASSWORDS_FILE"
}

deploy_one_vm() {
  local ip="$1"
  local vm_password="$2"

  if [[ "$DRY_RUN" == "true" ]]; then
    echo "[DRY-RUN] $ip"
    echo "  - SSH as $SSH_USER using key $SSH_KEY_PATH"
    echo "  - Create/update user: $IDE_USER"
    echo "  - Install code-server if missing"
    echo "  - Configure password auth on port $IDE_PORT"
    echo "  - Enable service: code-server@$IDE_USER"
    echo "  - Open firewall port: $IDE_PORT/tcp (if ufw exists)"
    echo "  - Install Go + build deps (if missing)"
    echo "  - Clone/update: $HARNESS_REPO_URL"
    echo "  - Use GITHUB_TOKEN for private repo auth (if provided)"
    echo "  - Install latest test harness via Go and symlink to test-harness"
    return 0
  fi

  echo "[INFO] Deploying on $ip ..."

  if ! ssh -i "$SSH_KEY_PATH" \
    -o BatchMode=yes \
    -o ConnectTimeout=15 \
    -o StrictHostKeyChecking=accept-new \
    "$SSH_USER@$ip" \
    "IDE_USER='$IDE_USER' IDE_PORT='$IDE_PORT' IDE_PASS='$vm_password' HARNESS_REPO_URL='$HARNESS_REPO_URL' HARNESS_REPO_SUBDIR='$HARNESS_REPO_SUBDIR' HARNESS_SRC_DIR='$HARNESS_SRC_DIR' HARNESS_BIN_PATH='$HARNESS_BIN_PATH' HARNESS_SYMLINK_PATH='$HARNESS_SYMLINK_PATH' GITHUB_TOKEN='$GITHUB_TOKEN' bash -s" <<'REMOTE_SCRIPT'
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "[ERROR] This script currently supports Ubuntu/Debian (apt-get)."
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y curl sudo git ca-certificates golang-go build-essential

if ! id -u "$IDE_USER" >/dev/null 2>&1; then
  useradd -m -s /bin/bash "$IDE_USER"
fi

usermod -aG sudo "$IDE_USER"
echo "$IDE_USER ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/$IDE_USER"
chmod 440 "/etc/sudoers.d/$IDE_USER"

echo "$IDE_USER:$IDE_PASS" | chpasswd

if ! command -v code-server >/dev/null 2>&1; then
  curl -fsSL https://code-server.dev/install.sh | sh
fi

install -d -m 755 "$HARNESS_SRC_DIR"

repo_fetch_url="$HARNESS_REPO_URL"
if [[ -n "${GITHUB_TOKEN:-}" && "$HARNESS_REPO_URL" =~ ^https://github.com/ ]]; then
  repo_fetch_url="https://x-access-token:${GITHUB_TOKEN}@${HARNESS_REPO_URL#https://}"
fi

if [[ ! -d "$HARNESS_SRC_DIR/.git" ]]; then
  git clone "$repo_fetch_url" "$HARNESS_SRC_DIR" || {
    echo "[ERROR] Failed to clone harness repository: $HARNESS_REPO_URL"
    exit 1
  }
else
  git -C "$HARNESS_SRC_DIR" remote set-url origin "$repo_fetch_url" || {
    echo "[ERROR] Failed to set remote origin URL"
    exit 1
  }
fi

git -C "$HARNESS_SRC_DIR" fetch --tags origin || {
  echo "[ERROR] Failed to fetch repository updates"
  exit 1
}

latest_tag="$(git -C "$HARNESS_SRC_DIR" tag -l 'v*' --sort=-version:refname | head -n 1)"
if [[ -n "$latest_tag" ]]; then
  git -C "$HARNESS_SRC_DIR" checkout -q "$latest_tag"
  harness_ref="$latest_tag"
else
  default_branch="$(git -C "$HARNESS_SRC_DIR" remote show origin | awk '/HEAD branch/ {print $NF}')"
  if [[ -z "$default_branch" ]]; then
    default_branch="main"
  fi
  git -C "$HARNESS_SRC_DIR" fetch origin "$default_branch"
  git -C "$HARNESS_SRC_DIR" checkout -q -B "$default_branch" "origin/$default_branch"
  harness_ref="origin/$default_branch"
fi

# Avoid persisting tokenized URL on disk.
git -C "$HARNESS_SRC_DIR" remote set-url origin "$HARNESS_REPO_URL" || true

if [[ ! -d "$HARNESS_SRC_DIR/$HARNESS_REPO_SUBDIR" ]]; then
  echo "[ERROR] Harness path not found: $HARNESS_SRC_DIR/$HARNESS_REPO_SUBDIR"
  exit 1
fi

cd "$HARNESS_SRC_DIR/$HARNESS_REPO_SUBDIR"
export GOBIN="/usr/local/bin"
go install ./cmd/harness

if [[ ! -x "$HARNESS_BIN_PATH" ]]; then
  echo "[ERROR] Harness binary was not installed at $HARNESS_BIN_PATH"
  exit 1
fi

ln -sf "$HARNESS_BIN_PATH" "$HARNESS_SYMLINK_PATH"

install -d -m 700 -o "$IDE_USER" -g "$IDE_USER" "/home/$IDE_USER/.config/code-server"
cat > "/home/$IDE_USER/.config/code-server/config.yaml" <<EOF
bind-addr: 0.0.0.0:$IDE_PORT
auth: password
password: $IDE_PASS
cert: false
EOF
chown "$IDE_USER:$IDE_USER" "/home/$IDE_USER/.config/code-server/config.yaml"

systemctl daemon-reload
systemctl enable --now "code-server@$IDE_USER"

if command -v ufw >/dev/null 2>&1; then
  ufw allow "$IDE_PORT/tcp" || true
fi

echo "[OK] Installed harness from $HARNESS_REPO_URL ($harness_ref)"
echo "[OK] Ready on http://$(hostname -I | awk '{print $1}'):$IDE_PORT"
REMOTE_SCRIPT
  then
    echo "[ERROR] Remote setup failed on $ip"
    return 1
  fi

  echo "[DONE] $ip -> http://$ip:$IDE_PORT (user: $IDE_USER)"
}

process_ip() {
  local ip="$1"
  local vm_password=""

  if ! vm_password="$(get_password_for_ip "$ip")"; then
    echo "[WARN] No password found for $ip in $PASSWORDS_FILE. Skipping."
    return 0
  fi

  if [[ -z "$vm_password" ]]; then
    echo "[WARN] Empty password for $ip. Skipping."
    return 0
  fi

  if ! deploy_one_vm "$ip" "$vm_password"; then
    echo "[ERROR] Deployment failed on $ip"
    return 1
  fi
}

verify_one_vm() {
  local ip="$1"

  if [[ "$DRY_RUN" == "true" ]]; then
    echo "[DRY-RUN] verify $ip"
    echo "  - Check service: code-server@$IDE_USER is active"
    echo "  - Check local HTTP on 127.0.0.1:$IDE_PORT"
    echo "  - Check command: test-harness --help"
    return 0
  fi

  if ssh -i "$SSH_KEY_PATH" \
    -o BatchMode=yes \
    -o ConnectTimeout=15 \
    -o StrictHostKeyChecking=accept-new \
    "$SSH_USER@$ip" \
    "systemctl is-active --quiet 'code-server@$IDE_USER' && curl -fsS --max-time 5 'http://127.0.0.1:$IDE_PORT' >/dev/null && command -v test-harness >/dev/null 2>&1 && test-harness --help >/dev/null 2>&1"; then
    echo "[VERIFY-OK] $ip"
    return 0
  fi

  echo "[VERIFY-ERROR] $ip"
  return 1
}

FAILURES_FILE="$(mktemp)"
VERIFY_FAILURES_FILE="$(mktemp)"
cleanup() {
  rm -f "$FAILURES_FILE"
  rm -f "$VERIFY_FAILURES_FILE"
}
trap cleanup EXIT

while IFS= read -r raw_ip || [[ -n "$raw_ip" ]]; do
  ip="$(echo "$raw_ip" | tr -d '[:space:]')"
  [[ -z "$ip" ]] && continue

  if [[ "$PARALLEL" == "true" ]]; then
    (
      if ! process_ip "$ip"; then
        echo "$ip" >> "$FAILURES_FILE"
      fi
    ) &

    while [[ "$(jobs -rp | wc -l | tr -d ' ')" -ge "$PARALLEL_JOBS" ]]; do
      sleep 0.2
    done
  else
    if ! process_ip "$ip"; then
      echo "$ip" >> "$FAILURES_FILE"
    fi
  fi
done < "$VMS_FILE"

if [[ "$PARALLEL" == "true" ]]; then
  wait
fi

overall_exit=0

if [[ -s "$FAILURES_FILE" ]]; then
  echo "[WARN] Some VMs failed:"
  sort -u "$FAILURES_FILE" | sed 's/^/  - /'
  overall_exit=1
fi

if [[ "$VERIFY" == "true" ]]; then
  echo "[INFO] Starting verification phase ..."

  while IFS= read -r raw_ip || [[ -n "$raw_ip" ]]; do
    ip="$(echo "$raw_ip" | tr -d '[:space:]')"
    [[ -z "$ip" ]] && continue

    if [[ "$PARALLEL" == "true" ]]; then
      (
        if ! verify_one_vm "$ip"; then
          echo "$ip" >> "$VERIFY_FAILURES_FILE"
        fi
      ) &

      while [[ "$(jobs -rp | wc -l | tr -d ' ')" -ge "$PARALLEL_JOBS" ]]; do
        sleep 0.2
      done
    else
      if ! verify_one_vm "$ip"; then
        echo "$ip" >> "$VERIFY_FAILURES_FILE"
      fi
    fi
  done < "$VMS_FILE"

  if [[ "$PARALLEL" == "true" ]]; then
    wait
  fi

  if [[ -s "$VERIFY_FAILURES_FILE" ]]; then
    echo "[WARN] Verification failed on:"
    sort -u "$VERIFY_FAILURES_FILE" | sed 's/^/  - /'
    overall_exit=1
  else
    echo "[INFO] Verification passed on all VMs."
  fi
fi

if [[ "$overall_exit" -ne 0 ]]; then
  echo "[INFO] Deployment run finished with errors."
  exit 1
fi

echo "[INFO] Deployment run finished."
