#!/usr/bin/env bash
# Tests get_file() retry behavior. Mock wget via PATH; no network required.
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ORIGINAL_PATH=$PATH
PASS=0
FAIL=0

assert_eq() {
    local actual="$1"
    local expected="$2"
    local msg="$3"
    if [[ "$actual" == "$expected" ]]
    then
        echo "PASS: $msg"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $msg (expected '$expected', got '$actual')" >&2
        FAIL=$((FAIL + 1))
    fi
}

make_mock_wget() {
    local mock_dir="$1"
    local fail_times="$2"
    mkdir -p "$mock_dir"
    cat > "$mock_dir/wget" << EOF
#!/usr/bin/env bash
count_file="\$WGET_COUNT_FILE"
n=\$(cat "\$count_file" 2>/dev/null || echo 0)
n=\$((n + 1))
echo "\$n" > "\$count_file"
out=""
while [[ \$# -gt 0 ]]
do
    if [[ "\$1" == "-O" ]]
    then
        out="\$2"
        shift 2
        continue
    fi
    shift
done
if [[ \$n -le $fail_times ]]
then
    echo "wget: unable to resolve host address ‘github.com’" >&2
    exit 4
fi
printf 'hello\\n' > "\$out"
exit 0
EOF
    chmod +x "$mock_dir/wget"
}

PAYLOAD_SUM=$(printf 'hello\n' | shasum -a256 | awk '{print $1}')

# --- retry until wget succeeds ---
WORKDIR=$(mktemp -d)
MOCK=$(mktemp -d)
make_mock_wget "$MOCK" 2
export PATH="$MOCK:$ORIGINAL_PATH"
export WGET_COUNT_FILE="$MOCK/count"
hash -r
export DOWNLOAD_RETRY_SLEEP=0
# shellcheck source=app/deps/_init
. "$SCRIPT_DIR/_init"
cd "$WORKDIR"
if get_file "https://example.invalid/libusb.tar.gz" "libusb.tar.gz" "$PAYLOAD_SUM"
then
    attempts=$(cat "$MOCK/count")
    assert_eq "$attempts" "3" "get_file retries wget until it succeeds"
    if [[ -f libusb.tar.gz ]]
    then
        echo "PASS: get_file wrote the downloaded file"
        PASS=$((PASS + 1))
    else
        echo "FAIL: get_file did not write the downloaded file" >&2
        FAIL=$((FAIL + 1))
    fi
else
    echo "FAIL: get_file should succeed after transient wget failures" >&2
    FAIL=$((FAIL + 1))
fi
rm -rf "$WORKDIR" "$MOCK"

# --- all attempts fail ---
WORKDIR=$(mktemp -d)
MOCK=$(mktemp -d)
make_mock_wget "$MOCK" 99
export PATH="$MOCK:$ORIGINAL_PATH"
export WGET_COUNT_FILE="$MOCK/count"
hash -r
export DOWNLOAD_RETRY_SLEEP=0
cd "$WORKDIR"
if get_file "https://example.invalid/libusb.tar.gz" "libusb.tar.gz" "$PAYLOAD_SUM"
then
    echo "FAIL: get_file should fail when wget never succeeds" >&2
    FAIL=$((FAIL + 1))
else
    attempts=$(cat "$MOCK/count")
    assert_eq "$attempts" "5" "get_file gives up after 5 wget attempts"
fi
rm -rf "$WORKDIR" "$MOCK"

echo
echo "$PASS passed, $FAIL failed"
if [[ "$FAIL" -ne 0 ]]
then
    exit 1
fi
