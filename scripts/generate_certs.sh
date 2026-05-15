#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-certs}"
mkdir -p "${OUT_DIR}"

cat > "${OUT_DIR}/openssl_server.cnf" <<'EOF'
[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no

[req_distinguished_name]
CN = localhost

[v3_req]
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF

cat > "${OUT_DIR}/openssl_client.cnf" <<'EOF'
[req]
distinguished_name = req_distinguished_name
prompt = no

[req_distinguished_name]
CN = accel-client
EOF

openssl genrsa -out "${OUT_DIR}/ca.key" 4096
openssl req -x509 -new -nodes -key "${OUT_DIR}/ca.key" -sha256 -days 3650 \
  -subj "/CN=accelerometer-test-ca" \
  -out "${OUT_DIR}/ca.crt"

openssl genrsa -out "${OUT_DIR}/server.key" 2048
openssl req -new -key "${OUT_DIR}/server.key" \
  -out "${OUT_DIR}/server.csr" \
  -config "${OUT_DIR}/openssl_server.cnf"
openssl x509 -req -in "${OUT_DIR}/server.csr" \
  -CA "${OUT_DIR}/ca.crt" -CAkey "${OUT_DIR}/ca.key" -CAcreateserial \
  -out "${OUT_DIR}/server.crt" -days 365 -sha256 \
  -extensions v3_req -extfile "${OUT_DIR}/openssl_server.cnf"

openssl genrsa -out "${OUT_DIR}/client.key" 2048
openssl req -new -key "${OUT_DIR}/client.key" \
  -out "${OUT_DIR}/client.csr" \
  -config "${OUT_DIR}/openssl_client.cnf"
openssl x509 -req -in "${OUT_DIR}/client.csr" \
  -CA "${OUT_DIR}/ca.crt" -CAkey "${OUT_DIR}/ca.key" -CAcreateserial \
  -out "${OUT_DIR}/client.crt" -days 365 -sha256

rm -f "${OUT_DIR}/server.csr" "${OUT_DIR}/client.csr" "${OUT_DIR}/ca.srl"

chmod 600 "${OUT_DIR}"/*.key

echo "Certificates generated in ${OUT_DIR}"
echo "CA:          ${OUT_DIR}/ca.crt"
echo "Server cert: ${OUT_DIR}/server.crt"
echo "Server key:  ${OUT_DIR}/server.key"
echo "Client cert: ${OUT_DIR}/client.crt"
echo "Client key:  ${OUT_DIR}/client.key"
