#!/bin/bash

DIRECTORY=${1:?the folder of the certificates}
shift
NAMES=("$@")

DAYS=${DAYS:-90}
CURVE=${CURVE:-prime256v1}
SERVER_NAME=${SERVER_NAME:-cerver test server}
CLIENT_NAME=${CLIENT_NAME:-cerver test client}

[ ${#NAMES[@]} -gt 0 ] || NAMES=(localhost 127.0.0.1 ::1)

alternative_names () {
	local names=() name
	for name in "${NAMES[@]}"; do
		if [[ "$name" =~ ^[0-9.]+$ ]] || [[ "$name" == *:* ]]; then
			names+=("IP:$name")
		else
			names+=("DNS:$name")
		fi
	done

	local IFS=,
	echo "${names[*]}"
}

key () {
	openssl ecparam -name "$CURVE" -genkey -noout -out "$1" || exit 1
}

signed () {
	local name=$1 subject=$2 extensions=$3

	key "$DIRECTORY/$name.key"
	openssl req -new -key "$DIRECTORY/$name.key" -subj "/CN=$subject" -out "$DIRECTORY/$name.csr" || exit 1
	openssl x509 -req -in "$DIRECTORY/$name.csr" -CA "$DIRECTORY/authority.pem" -CAkey "$DIRECTORY/authority.key" \
		-CAcreateserial -days "$DAYS" -sha256 -extfile <(printf '%b' "$extensions") \
		-out "$DIRECTORY/$name.crt" || exit 1

	cat "$DIRECTORY/$name.crt" "$DIRECTORY/authority.pem" > "$DIRECTORY/$name.pem"
	rm -f "$DIRECTORY/$name.csr" "$DIRECTORY/$name.crt"
}

mkdir -p "$DIRECTORY" || exit 1

key "$DIRECTORY/authority.key"
openssl req -x509 -new -key "$DIRECTORY/authority.key" -subj "/CN=cerver test authority" -days "$DAYS" -sha256 \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" \
	-out "$DIRECTORY/authority.pem" || exit 1

signed server "$SERVER_NAME" "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=$(alternative_names)\n"
signed client "$CLIENT_NAME" "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n"

rm -f "$DIRECTORY/authority.srl"
chmod 644 "$DIRECTORY"/*.pem "$DIRECTORY"/*.key
