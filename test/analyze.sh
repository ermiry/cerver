#!/bin/bash

SUPPRESSIONS=test/suppressions.txt
JOBS=$(nproc)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

gcc_analyzer () {
	make clean > /dev/null
	make TYPE=test ANALYZER=1 WERROR=0 -j"$JOBS" 2>&1 \
		| sed -n 's|^\(src/[^:]*\):[0-9]*:[0-9]*: warning: .*\[-W\(analyzer-[a-z-]*\)\]$|gcc \1 \2|p'
}

scan_build () {
	make clean > /dev/null
	scan-build --use-cc=clang -o "$WORK/scan" bash -c "make TYPE=test WERROR=0 CC=\"\$CC\" -j$JOBS" 2>&1 \
		| sed -n 's|^\(src/[^:]*\):[0-9]*:[0-9]*: warning: .*\[\([a-z]*\.[a-zA-Z.]*\)\]$|scan-build \1 \2|p'
}

clang_tidy () {
	find src -name '*.c' | sort \
		| xargs -P "$JOBS" -n 4 bash -c 'clang-tidy --quiet "$@" -- -std=c11 -D_GNU_SOURCE -DCERVER_CRYPTO_OPENSSL -Iinclude -Isrc 2> /dev/null' tidy \
		| sed -n 's|^/*\(.*/\)\{0,1\}\(src/[^:]*\):[0-9]*:[0-9]*: warning: .*\[\([a-z0-9.,-]*\)\]$|clang-tidy \2 \3|p'
}

cppcheck_run () {
	cppcheck -q --enable=warning,portability --inline-suppr --suppress=normalCheckLevelMaxBranches \
		-D_GNU_SOURCE -D__GNUC__ -D CERVER_CRYPTO_OPENSSL -I include -I src --template='cppcheck {file} {id}' src 2>&1
}

{
	gcc_analyzer
	scan_build
	clang_tidy
	cppcheck_run
} | sort -u > "$WORK/findings.txt"

make clean > /dev/null

grep -v '^$' "$SUPPRESSIONS" | sort -u > "$WORK/suppressions.txt"

comm -23 "$WORK/findings.txt" "$WORK/suppressions.txt" > "$WORK/new.txt"
comm -13 "$WORK/findings.txt" "$WORK/suppressions.txt" > "$WORK/stale.txt"

echo "Findings: $(wc -l < "$WORK/findings.txt"), suppressed: $(wc -l < "$WORK/suppressions.txt")"

if [ -s "$WORK/stale.txt" ]; then
	echo "Suppressions without a finding:"
	cat "$WORK/stale.txt"
fi

if [ -s "$WORK/new.txt" ]; then
	echo "New findings:"
	cat "$WORK/new.txt"
	exit 1
fi
