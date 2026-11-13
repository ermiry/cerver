#!/bin/bash

set -e

SOURCE=benchmarks/competitors
OUTPUT=$SOURCE/bin

FLAGS="-O3 -march=native -D _GNU_SOURCE"
CERVER="-I ./include -Wl,-rpath=/opt/cerver/bin -L ./bin -l cerver"

rm -rf "$OUTPUT"
mkdir -p "$OUTPUT"

gcc $FLAGS -c $SOURCE/measure.c -o $OUTPUT/measure.o
gcc $FLAGS -c $SOURCE/json/driver.c -o $OUTPUT/json.o
gcc $FLAGS -c $SOURCE/parser/driver.c -o $OUTPUT/parser.o

gcc $FLAGS $SOURCE/json/cerver.c $OUTPUT/json.o $OUTPUT/measure.o -o $OUTPUT/json-cerver $CERVER

gcc $FLAGS -I "$COMPETITORS/jansson/include" $SOURCE/json/jansson.c $OUTPUT/json.o $OUTPUT/measure.o \
	-o $OUTPUT/json-jansson -L "$COMPETITORS/jansson/lib" -l:libjansson.a

gcc $FLAGS -I "$COMPETITORS/yyjson/src" $SOURCE/json/yyjson.c "$COMPETITORS/yyjson/src/yyjson.c" \
	$OUTPUT/json.o $OUTPUT/measure.o -o $OUTPUT/json-yyjson

g++ $FLAGS -std=c++17 -I "$COMPETITORS/simdjson/singleheader" $SOURCE/json/simdjson.cpp \
	"$COMPETITORS/simdjson/singleheader/simdjson.cpp" $OUTPUT/json.o $OUTPUT/measure.o -o $OUTPUT/json-simdjson

g++ $FLAGS -std=c++17 -I "$COMPETITORS/rapidjson/include" $SOURCE/json/rapidjson.cpp \
	$OUTPUT/json.o $OUTPUT/measure.o -o $OUTPUT/json-rapidjson

gcc $FLAGS $SOURCE/parser/cerver.c objs/cerver/http/parser/*.o $OUTPUT/parser.o $OUTPUT/measure.o \
	-o $OUTPUT/parser-cerver $CERVER

gcc $FLAGS -I "$COMPETITORS/llhttp/include" $SOURCE/parser/llhttp.c "$COMPETITORS"/llhttp/src/*.c \
	$OUTPUT/parser.o $OUTPUT/measure.o -o $OUTPUT/parser-llhttp

gcc $FLAGS -I "$COMPETITORS/picohttpparser" $SOURCE/parser/picohttpparser.c "$COMPETITORS/picohttpparser/picohttpparser.c" \
	$OUTPUT/parser.o $OUTPUT/measure.o -o $OUTPUT/parser-picohttpparser
