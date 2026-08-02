#!/bin/sh
# Publish the DK firmware to the product repo — THE one way firmware reaches makers.
#
#   scripts/publish-hex.sh            # build A target, copy hex + version, commit+push product
#
# Why this exists: the hosted hex is a build artifact committed to the product
# repo, so it does not follow the firmware sources by itself. It went stale twice
# in one day (three weeks once, three hours once), and both times a maker running
# the installer got an OLDER brain than the bench board. The pre-push hook
# (scripts/hooks/pre-push) refuses to push firmware changes that were not
# published, so forgetting is no longer possible.
#
# Builds the A target because it is the one that runs on BOTH chip variants
# (a B image does not boot on an A chip).
set -eu

RUNTIME_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT_DIR="${TEXTOCHIP_PRODUCT_DIR:-$HOME/projects/textochip}"
TC="${TEXTOCHIP_NCS_TOOLCHAIN:-$HOME/ncs/toolchains/fbf7391cab}"

[ -d "$PRODUCT_DIR/public/firmware" ] || {
  echo "product repo not found at $PRODUCT_DIR (set TEXTOCHIP_PRODUCT_DIR)"; exit 1; }
[ -d "$TC" ] || {
  echo "NCS toolchain not found at $TC (set TEXTOCHIP_NCS_TOOLCHAIN)"; exit 1; }

HASH="$(git -C "$RUNTIME_DIR" rev-parse --short HEAD)"
if ! git -C "$RUNTIME_DIR" diff --quiet HEAD -- src zephyr; then
  echo "!! uncommitted firmware changes — commit first, so the published build id"
  echo "   names a commit anyone can check out."
  exit 1
fi

echo "-> Building the A target ($HASH)..."
export PATH="$TC/usr/local/bin:$TC/bin:$TC/usr/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR="$TC/opt/zephyr-sdk"
export ZEPHYR_BASE="$HOME/ncs/v3.4.0/zephyr"
export PYTHONPATH="$TC/usr/local/lib/python3.12/site-packages"
cd "$RUNTIME_DIR"
"$TC/usr/local/bin/python3.12" -m west build -b nrf54lm20dk/nrf54lm20a/cpuapp \
  zephyr -d build_dk_a >/dev/null

HEX="$RUNTIME_DIR/build_dk_a/zephyr/zephyr/zephyr.hex"
DEST="$PRODUCT_DIR/public/firmware/textochip-nrf54lm20dk.hex"
cp "$HEX" "$DEST"
printf '%s\n' "$HASH" > "$PRODUCT_DIR/public/firmware/textochip-nrf54lm20dk.version"

cd "$PRODUCT_DIR"
git add public/firmware/textochip-nrf54lm20dk.hex \
        public/firmware/textochip-nrf54lm20dk.version
if git diff --cached --quiet; then
  echo "-> Hosted hex already at $HASH — nothing to publish."
  exit 0
fi
git commit -q -m "firmware: hosted DK hex at runtime $HASH (scripts/publish-hex.sh)"
git push -q origin main
echo "-> Published $HASH to textochip.com (deploys with the site)."
