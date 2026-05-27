#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/mkbdrom-regression.XXXXXX")
MINIMAL_DIR="$WORK_DIR/minimal"
MANY_DIR="$WORK_DIR/many"
MINIMAL_ISO="$WORK_DIR/minimal.iso"
MANY_ISO="$WORK_DIR/many.iso"

cleanup() {
	rm -rf "$WORK_DIR"
}

fail() {
	echo "FAIL: $1" >&2
	exit 1
}

assert_eq() {
	if [ "$1" != "$2" ]; then
		fail "$3 (expected '$2', got '$1')"
	fi
}

assert_gt() {
	if [ "$1" -le "$2" ]; then
		fail "$3 (expected > $2, got $1)"
	fi
}

extract_value() {
	key=$1
	file=$2
	awk -F= -v key="$key" '$1 == key { print $2; exit }' "$file"
}

trap cleanup EXIT INT TERM

mkdir -p "$MINIMAL_DIR/BDMV" "$MINIMAL_DIR/CERTIFICATE"
printf 'test\n' > "$MINIMAL_DIR/BDMV/index.bdmv"
printf 'cert\n' > "$MINIMAL_DIR/CERTIFICATE/id.bdmv"

mkdir -p "$MANY_DIR/BDMV" "$MANY_DIR/CERTIFICATE"
i=1
while [ "$i" -le 200 ]; do
	printf 'x' > "$MANY_DIR/BDMV/file_$i.bin"
	i=$((i + 1))
done

cd "$ROOT_DIR"
make >/dev/null

./mkbdrom/mkbdrom --source "$MINIMAL_DIR" "$MINIMAL_ISO" >/dev/null
./udfinfo/udfinfo "$MINIMAL_ISO" > "$WORK_DIR/minimal.udfinfo"

minimal_blocks=$(extract_value blocks "$WORK_DIR/minimal.udfinfo")
minimal_size=$(stat -f '%z' "$MINIMAL_ISO")
assert_eq "$minimal_size" "$((minimal_blocks * 2048))" "minimal image file size does not match logical block count"

minimal_anchor=$(awk -F'[=, ]+' '/type=ANCHOR$/ { value = $2 } END { print value }' "$WORK_DIR/minimal.udfinfo")
assert_eq "$minimal_anchor" "$((minimal_blocks - 1))" "minimal image final anchor is not at the last block"

./mkbdrom/mkbdrom --source "$MANY_DIR" "$MANY_ISO" >/dev/null
./udfinfo/udfinfo "$MANY_ISO" > "$WORK_DIR/many.udfinfo"

many_blocks=$(extract_value blocks "$WORK_DIR/many.udfinfo")
many_size=$(stat -f '%z' "$MANY_ISO")
assert_eq "$many_size" "$((many_blocks * 2048))" "many-file image file size does not match logical block count"

metadata_size=$(extract_value metadatasize "$WORK_DIR/many.udfinfo")
assert_gt "$metadata_size" 3 "many-file image metadata partition did not grow"

set +e
./mkbdrom/mkbdrom --source "$MINIMAL_DIR" --disc-capacity $((minimal_blocks - 1)) "$WORK_DIR/too-small.iso" >/dev/null 2> "$WORK_DIR/too-small.err"
status=$?
set -e

if [ "$status" -eq 0 ]; then
	fail "mkbdrom accepted a disc capacity smaller than the required block count"
fi

echo "PASS: mkbdrom regression checks"