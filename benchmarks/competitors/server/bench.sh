#!/bin/bash

set -e

SOURCE=benchmarks/competitors/server
LOGS=${LOGS:-/tmp/competitors-server}

PORT=8100
THREADS=4
CONNECTIONS=256
REQUESTS=${REQUESTS:-2097152}
WARM_REQUESTS=$(( REQUESTS / 8 ))
RUNS=${RUNS:-5}
READY_SECONDS=16
STOP_SECONDS=16
WRK_SECONDS=4

WORKERS=(${WORKERS:-1 4})
PIPELINES=(${PIPELINES:-1 16})
CASES=(${CASES:-plaintext json})
IMPLEMENTATIONS=(${IMPLEMENTATIONS:-cerver-uring cerver-epoll cerver-pool nginx h2o})

CLIENT_THREADS=$(( $(nproc) / 2 ))

source test/process.sh

export LD_LIBRARY_PATH=./bin

mkdir -p "$LOGS"

configuration () {
	local file="$1" workers="$2"

	sed -e "s/WORKERS/$workers/" -e "s/PORT/$PORT/" -e "s#FILES#$PWD/$SOURCE/files#" "$SOURCE/$file" > "$LOGS/$file"
}

cerver_start () {
	local backend="$1" handlers="$2" workers="$3"

	BENCH_BACKEND=$backend BENCH_HANDLERS=$handlers BENCH_KEEP_ALIVE_REQUESTS=0 BENCH_QUIET=1 \
		./benchmarks/bin/server $PORT $THREADS events "$workers" > "$LOGS/server.log" 2>&1 &
}

server_start () {
	local implementation="$1" workers="$2"

	case "$implementation" in
		cerver-uring)
			cerver_start uring loop "$workers"
			;;
		cerver-epoll)
			cerver_start epoll loop "$workers"
			;;
		cerver-pool)
			cerver_start uring pool "$workers"
			;;
		nginx)
			configuration nginx.conf "$workers"
			"$COMPETITORS/nginx/sbin/nginx" -c "$LOGS/nginx.conf" > "$LOGS/server.log" 2>&1 &
			;;
		h2o)
			configuration h2o.conf "$workers"
			"$COMPETITORS/h2o/bin/h2o" -c "$LOGS/h2o.conf" > "$LOGS/server.log" 2>&1 &
			;;
		*)
			echo "$implementation is not an implementation of the server case" >&2
			exit 1
			;;
	esac

	SERVER_PID=$!

	wait_until $READY_SECONDS is_listening $PORT \
		|| { echo "$implementation did not start" >&2; tail -n 8 "$LOGS/server.log" >&2; exit 1; }
}

server_stop () {
	kill -TERM $SERVER_PID
	wait_until $STOP_SECONDS is_finished $SERVER_PID || kill -KILL $SERVER_PID
	wait $SERVER_PID 2> /dev/null || true
}

field () {
	echo "$1" | sed -E "s/.* - $2 ([0-9.]+).*/\1/"
}

flood () {
	local pipeline="$1" requests="$2" name="$3"

	FLOOD_PIPELINE=$pipeline ./benchmarks/bin/flood $PORT $CONNECTIONS "$requests" GET "/$name"
}

wrk_per_second () {
	local pipeline="$1" name="$2"

	if [ "$pipeline" -gt 1 ]; then
		echo "-"
		return
	fi

	"$COMPETITORS/wrk/wrk" -t $CLIENT_THREADS -c $CONNECTIONS -d ${WRK_SECONDS}s "http://127.0.0.1:$PORT/$name" \
		| sed -nE 's/^Requests\/sec: +([0-9]+).*/\1/p'
}

measure () {
	local implementation="$1" workers="$2" pipeline="$3" name="$4"
	local runs=() result

	flood "$pipeline" $WARM_REQUESTS "$name" > /dev/null \
		|| { echo "$implementation $name: the warm up failed" >&2; return 1; }

	for (( run = 0; run < RUNS; run++ )); do
		result=$(flood "$pipeline" $REQUESTS "$name") \
			|| { echo "$implementation $name: the load failed: $result" >&2; return 1; }

		runs+=("$(echo "$result" | sed -E 's/.* - ([0-9]+) req\/s.*/\1/') $(field "$result" p50) $(field "$result" p99)")
	done

	local sorted=$(printf '%s\n' "${runs[@]}" | sort -n)
	local median=$(echo "$sorted" | sed -n "$(( (RUNS + 1) / 2 ))p")
	local lowest=$(echo "$sorted" | head -n 1 | cut -d ' ' -f 1)
	local highest=$(echo "$sorted" | tail -n 1 | cut -d ' ' -f 1)

	printf "%-9s %-10s %-18s %7s %8s %12s %10s %10s %8s %8s %10s\n" \
		server "$name" "$implementation" "$workers" "$pipeline" \
		"$(echo "$median" | cut -d ' ' -f 1)" "$lowest" "$highest" \
		"$(echo "$median" | cut -d ' ' -f 2)" "$(echo "$median" | cut -d ' ' -f 3)" \
		"$(wrk_per_second "$pipeline" "$name")"
}

echo "# server: GET /plaintext and GET /json, $CONNECTIONS kept connections from $CLIENT_THREADS threads of benchmarks/flood in the same machine"
echo "# server: the median of $RUNS loads of $REQUESTS requests, with the slowest and the fastest of them, and its latencies; wrk $WRK_VERSION without pipelining"
echo "# server: cerver-uring and cerver-epoll run their handlers in their event loops, cerver-pool in a pool of $THREADS threads; workers are the I/O threads"
echo "# server: nginx $NGINX_VERSION answers both with return; h2o $H2O_COMMIT serves two files; only cerver serializes its JSON in each request; no server logs its requests"
printf "%-9s %-10s %-18s %7s %8s %12s %10s %10s %8s %8s %10s\n" \
	module case implementation workers pipeline req/s lowest highest p50-ms p99-ms wrk-req/s

for implementation in "${IMPLEMENTATIONS[@]}"; do
	for workers in "${WORKERS[@]}"; do
		server_start "$implementation" "$workers"

		for name in "${CASES[@]}"; do
			for pipeline in "${PIPELINES[@]}"; do
				measure "$implementation" "$workers" "$pipeline" "$name" || { server_stop; exit 1; }
			done
		done

		server_stop
	done
done
