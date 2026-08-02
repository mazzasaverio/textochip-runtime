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
# Publishes TWO images, both from the same commit:
#   textochip-nrf54lm20dk.hex    — the A target: runs on BOTH chip variants,
#                                  colour vision + TFLM voice. The universal
#                                  fallback every installer path can deliver.
#   textochip-nrf54lm20dk-b.hex  — the B target: person detection + voice on
#                                  the Axon NPU. The installer picks it when it
#                                  can READ the chip (nrfutil device-info says
#                                  NRF54LM20B); it does not boot on an A chip.
# One .version file covers both — same commit, same id.
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
# --cmake forces a reconfigure so the VER id embedded in the image is THIS
# commit's — an incremental build would keep the hash cached at the previous
# configure, and the IDE would then flag a perfectly current board as stale.
"$TC/usr/local/bin/python3.12" -m west build --cmake -b nrf54lm20dk/nrf54lm20a/cpuapp \
  zephyr -d build_dk_a >/dev/null

HEX="$RUNTIME_DIR/build_dk_a/zephyr/zephyr/zephyr.hex"
DEST="$PRODUCT_DIR/public/firmware/textochip-nrf54lm20dk.hex"
cp "$HEX" "$DEST"

echo "-> Building the B target (person detection on the Axon, $HASH)..."
EDGEAI_DIR="${TEXTOCHIP_EDGEAI_DIR:-$HOME/projects/sdk-edge-ai}"
[ -d "$EDGEAI_DIR" ] || {
  echo "sdk-edge-ai not found at $EDGEAI_DIR (set TEXTOCHIP_EDGEAI_DIR)"; exit 1; }
"$TC/usr/local/bin/python3.12" -m west build --cmake -b nrf54lm20dk/nrf54lm20b/cpuapp \
  zephyr -d build_dk_b_pub -- \
  -DEXTRA_ZEPHYR_MODULES="$EDGEAI_DIR" \
  -DCONFIG_TEXTOCHIP_NRF_PERSONDET=y \
  -DCONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE=229376 >/dev/null
cp "$RUNTIME_DIR/build_dk_b_pub/zephyr/zephyr/zephyr.hex" \
   "$PRODUCT_DIR/public/firmware/textochip-nrf54lm20dk-b.hex"

printf '%s\n' "$HASH" > "$PRODUCT_DIR/public/firmware/textochip-nrf54lm20dk.version"

cd "$PRODUCT_DIR"
git add public/firmware/textochip-nrf54lm20dk.hex \
        public/firmware/textochip-nrf54lm20dk-b.hex \
        public/firmware/textochip-nrf54lm20dk.version
if git diff --cached --quiet; then
  echo "-> Hosted hex already at $HASH — nothing to publish."
  exit 0
fi
git commit -q -m "firmware: hosted DK hex at runtime $HASH (scripts/publish-hex.sh)"
git push -q origin main
echo "-> Published $HASH to textochip.com (deploys with the site)."
