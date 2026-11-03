#!/usr/bin/env bash
# Desk Sprite - Mosquitto TLS 自签名证书快速生成脚本 (SPEC-001, SPEC-004, R08)

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/certs"
mkdir -p "$DIR"

DAYS=3650
SUBJ_BASE="/C=CN/ST=Guangdong/L=Shenzhen/O=DeskSprite"

echo "[1/3] Generating CA key and certificate..."
openssl req -new -x509 -days $DAYS -extensions v3_ca \
    -keyout "$DIR/ca.key" -out "$DIR/ca.crt" \
    -nodes -subj "$SUBJ_BASE/CN=DeskSpriteCA"

echo "[2/3] Generating Server key and certificate..."
openssl req -new -nodes \
    -out "$DIR/server.csr" -keyout "$DIR/server.key" \
    -subj "$SUBJ_BASE/CN=localhost"

openssl x509 -req -in "$DIR/server.csr" -CA "$DIR/ca.crt" -CAkey "$DIR/ca.key" \
    -CAcreateserial -out "$DIR/server.crt" -days $DAYS

chmod 600 "$DIR/server.key" "$DIR/ca.key"
chmod 644 "$DIR/server.crt" "$DIR/ca.crt"

echo "[3/3] Certificates generated successfully in $DIR"
