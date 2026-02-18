#!/usr/bin/env sh
#
# Post-build script for well-monitor-2-bootloader: from an ELF, produce a
# binary with metadata, compute SHA256, write digest, emit *_validated.srec
# and *_nonvalidated.srec.
# Uses only: sh, printf, dd, head, wc, tr, awk, cut; sha256sum or openssl;
# arm-none-eabi-objcopy. No xxd required.
#
# Usage: bootloader_postbuild.sh <path-to-.elf>
# Output: <dirname>/<basename>_validated.srec, <dirname>/<basename>_nonvalidated.srec
#
# Metadata layout (last 88 bytes of image):
#   [0..7]   magic
#   [8..15]  inverted magic
#   [16..23] name (8-byte, "WSM_BL")
#   [24..31] version (8-byte, "1.0.0")
#   [32..35] dest_address (LE) - sector 4 = 0x08010000
#   [36..39] size (LE) - bootloader size including metadata
#   [40..47] validation   <- 0xffffffff00000000 (validated) or 0xffffffffffffffff (nonvalidated)
#   [48..55] invalidation
#   [56..87] SHA256 digest <- hash of bytes 0..(size-33) inclusive
#

set -e

if [ $# -ne 1 ]; then
  echo "Usage: $0 <path-to-.elf>" >&2
  exit 1
fi

ELF="$1"
if [ ! -f "$ELF" ]; then
  echo "Error: ELF file not found: $ELF" >&2
  exit 1
fi

DIR=$(dirname "$ELF")
BASE=$(basename "$ELF" .elf)
BIN="${DIR}/${BASE}.bin"
SREC="${DIR}/${BASE}_nonvalidated.srec"
SREC_VALIDATED="${DIR}/${BASE}_validated.srec"
DIGEST_BIN="${DIR}/${BASE}_digest.bin"

# Flash base for sector 4 (bootloader; must match linker script)
FLASH_BASE=0x08010000

# Metadata: 88 bytes at end
METADATA_SIZE=88
DEST_ADDR_OFFSET_IN_META=32
SIZE_OFFSET_IN_META=36
VALIDATION_OFFSET_IN_META=40
SHA256_OFFSET_IN_META=56
SHA256_SIZE=32

# 1) Create binary from ELF
arm-none-eabi-objcopy -O binary "$ELF" "$BIN"
SIZE=$(wc -c < "$BIN" | tr -d ' ')

if [ "$SIZE" -lt "$METADATA_SIZE" ]; then
  echo "Error: Binary too small for metadata ($SIZE bytes)." >&2
  exit 1
fi

# Offsets from end of file (last METADATA_SIZE bytes)
VALIDATION_SEEK=$((SIZE - METADATA_SIZE + VALIDATION_OFFSET_IN_META))
DEST_ADDR_SEEK=$((SIZE - METADATA_SIZE + DEST_ADDR_OFFSET_IN_META))
SIZE_SEEK=$((SIZE - METADATA_SIZE + SIZE_OFFSET_IN_META))
SHA256_SEEK=$((SIZE - METADATA_SIZE + SHA256_OFFSET_IN_META))
BYTES_TO_HASH=$((SIZE - SHA256_SIZE))

# 2) Write dest_address (LE) and size (LE) into metadata before hashing
printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((FLASH_BASE & 0xff)) $(((FLASH_BASE >> 8) & 0xff)) $(((FLASH_BASE >> 16) & 0xff)) $(((FLASH_BASE >> 24) & 0xff)))" | dd of="$BIN" bs=1 seek=$DEST_ADDR_SEEK conv=notrunc status=none 2>/dev/null
printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((SIZE & 0xff)) $(((SIZE >> 8) & 0xff)) $(((SIZE >> 16) & 0xff)) $(((SIZE >> 24) & 0xff)))" | dd of="$BIN" bs=1 seek=$SIZE_SEEK conv=notrunc status=none 2>/dev/null

# 3) SHA256 of firmware from start up to (but not including) the digest field
if command -v sha256sum >/dev/null 2>&1; then
  DIGEST_HEX=$(head -c "$BYTES_TO_HASH" "$BIN" | sha256sum -b)
else
  DIGEST_HEX=$(head -c "$BYTES_TO_HASH" "$BIN" | openssl dgst -sha256 -r 2>/dev/null)
fi
DIGEST_HEX=$(echo "$DIGEST_HEX" | awk '{print $1}' | tr -d '\n')
if [ ${#DIGEST_HEX} -ne 64 ]; then
  echo "Error: SHA256 output unexpected (got ${#DIGEST_HEX} hex chars)." >&2
  exit 1
fi

# 4) Convert 64-char hex to 32-byte binary and write at SHA256_SEEK
awk -v h="$DIGEST_HEX" 'BEGIN {
  for (i = 0; i < 32; i++) {
    pair = substr(h, i*2+1, 2)
    printf "%c", ("0x" pair) + 0
  }
}' > "$DIGEST_BIN" 2>/dev/null

if [ ! -f "$DIGEST_BIN" ] || [ $(wc -c < "$DIGEST_BIN" | tr -d ' ') -ne 32 ]; then
  # Fallback: build digest with printf
  : > "$DIGEST_BIN"
  i=0
  while [ $i -lt 32 ]; do
    pos=$((i * 2 + 1))
    pair=$(echo "$DIGEST_HEX" | cut -c${pos}-$((pos + 1)))
    printf "\\x$pair" >> "$DIGEST_BIN"
    i=$((i + 1))
  done
fi
if [ ! -f "$DIGEST_BIN" ] || [ $(wc -c < "$DIGEST_BIN" | tr -d ' ') -ne 32 ]; then
  echo "Error: Failed to create digest binary." >&2
  rm -f "$DIGEST_BIN"
  exit 1
fi

dd if="$DIGEST_BIN" of="$BIN" bs=1 seek=$SHA256_SEEK conv=notrunc status=none 2>/dev/null || \
  dd if="$DIGEST_BIN" of="$BIN" bs=1 seek=$SHA256_SEEK conv=notrunc
rm -f "$DIGEST_BIN"

# 5) Set validation to 0xffffffff00000000 (ready) and create _validated.srec
printf '\377\377\377\377\000\000\000\000' | dd of="$BIN" bs=1 seek=$VALIDATION_SEEK conv=notrunc status=none 2>/dev/null || \
  printf '\377\377\377\377\000\000\000\000' | dd of="$BIN" bs=1 seek=$VALIDATION_SEEK conv=notrunc
arm-none-eabi-objcopy -I binary -O srec --change-section-address .data=$FLASH_BASE "$BIN" "$SREC_VALIDATED"

# 6) Set validation to 0xffffffffffffffff (download) and create _nonvalidated.srec
printf '\377\377\377\377\377\377\377\377' | dd of="$BIN" bs=1 seek=$VALIDATION_SEEK conv=notrunc status=none 2>/dev/null || \
  printf '\377\377\377\377\377\377\377\377' | dd of="$BIN" bs=1 seek=$VALIDATION_SEEK conv=notrunc
arm-none-eabi-objcopy -I binary -O srec --change-section-address .data=$FLASH_BASE "$BIN" "$SREC"

echo "Created: $SREC_VALIDATED, $SREC (from $BIN, size $SIZE bytes)"
