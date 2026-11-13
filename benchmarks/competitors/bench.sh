#!/bin/bash

set -e

OUTPUT=benchmarks/competitors/bin

echo "# cerver $(cat version.txt) against its competitors"
echo "# date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# architecture: $(uname -m), cpus: $(nproc), kernel: $(uname -r)"
echo "# cpu: $(lscpu 2>/dev/null | sed -n 's/^Model name: *//p' | head -1)"
echo "# compiler: $(gcc --version | head -1), -O3 -march=native"
echo "# yyjson $YYJSON_VERSION, simdjson $SIMDJSON_VERSION, jansson $JANSSON_VERSION, rapidjson $RAPIDJSON_COMMIT"
echo "# llhttp $LLHTTP_VERSION, picohttpparser $PICOHTTPPARSER_COMMIT"
echo "# the median of 5 runs of 0.25 s; allocations of one item; rss of the process until then"
printf "%-8s %-10s %-16s %-10s %14s %10s %10s %10s\n" module case implementation operation items/s MB/s allocations rss-KiB

for implementation in cerver jansson yyjson simdjson rapidjson; do
	"$OUTPUT/json-$implementation"
done

for implementation in cerver llhttp picohttpparser; do
	"$OUTPUT/parser-$implementation"
done
