#!/bin/bash

set -e

OUTPUT=benchmarks/competitors/bin

echo "# cerver $(cat version.txt) against its competitors"
echo "# date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "# architecture: $(uname -m), cpus: $(nproc), kernel: $(uname -r)"
echo "# cpu: $(lscpu 2>/dev/null | sed -n 's/^Model name: *//p' | head -1)"
echo "# compiler: $(gcc --version | head -1), -O3 -march=native"
echo "# yyjson $YYJSON_VERSION, simdjson $SIMDJSON_VERSION, jansson $JANSSON_VERSION, rapidjson $RAPIDJSON_COMMIT"
echo "# libjwt $LIBJWT_VERSION, l8w8jwt $L8W8JWT_VERSION, jwt-cpp $JWT_CPP_VERSION"
echo "# llhttp $LLHTTP_VERSION, picohttpparser $PICOHTTPPARSER_COMMIT, http_parser $HTTP_PARSER_VERSION, multipart-parser-c $MULTIPART_PARSER_COMMIT"
echo "# the median of 5 runs of 0.25 s, with the slowest and the fastest of them; allocations of one item; rss of the process until then"
echo "# picohttpparser reads the head, its body is the Content-Length after it or a copy decoded by phr_decode_chunked ()"
printf "%-9s %-10s %-18s %-10s %14s %10s %10s %10s %10s %10s\n" module case implementation operation items/s MB/s lowest highest allocations rss-KiB

for implementation in cerver jansson yyjson simdjson rapidjson; do
	"$OUTPUT/json-$implementation"
done

for implementation in cerver libjwt l8w8jwt jwtcpp; do
	"$OUTPUT/jwt-$implementation"
done

for implementation in cerver llhttp picohttpparser joyent; do
	"$OUTPUT/parser-$implementation"
done

for implementation in cerver legacy; do
	"$OUTPUT/multipart-$implementation"
done

bash benchmarks/competitors/server/bench.sh
