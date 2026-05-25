#!/usr/bin/env bash
set -euo pipefail

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-shilin}"
MYSQL_DB="${MYSQL_DB:-oj}"
# MYSQL_PASSWORD="${MYSQL_PASSWORD:-}"
MYSQL_PASSWORD=123456

if ! command -v mysql >/dev/null 2>&1; then
  echo "mysql client not found"
  exit 1
fi

if [[ -z "$MYSQL_PASSWORD" ]]; then
  echo "Please export MYSQL_PASSWORD before loading seed data."
  exit 1
fi

echo "Loading frontend seed data into ${MYSQL_USER}@${MYSQL_HOST}:${MYSQL_PORT}/${MYSQL_DB} ..."
MYSQL_PWD="$MYSQL_PASSWORD" mysql \
  -h "$MYSQL_HOST" \
  -P "$MYSQL_PORT" \
  -u "$MYSQL_USER" \
  "$MYSQL_DB" < ./seed_frontend_data.sql

echo "Seed data loaded successfully."
