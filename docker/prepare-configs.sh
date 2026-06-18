#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_DIR="$ROOT_DIR/docker/configs"

mkdir -p "$CONFIG_DIR"

cp "$ROOT_DIR/src/server/authserver/authserver.conf.dist" "$CONFIG_DIR/authserver.conf"
cp "$ROOT_DIR/src/server/worldserver/worldserver.conf.dist" "$CONFIG_DIR/worldserver.conf"

sed -i \
  -e 's#LoginDatabaseInfo = "127.0.0.1;3306;trinity;trinity;auth"#LoginDatabaseInfo = "mysql;3306;trinity;trinity;auth"#' \
  -e 's#LogsDir = ""#LogsDir = "/home/circleci/logs"#' \
  -e 's#SourceDirectory  = ""#SourceDirectory  = "/trinity/source"#' \
  "$CONFIG_DIR/authserver.conf"

sed -i \
  -e 's#DataDir = "."#DataDir = "/trinity/data"#' \
  -e 's#LogsDir = ""#LogsDir = "/home/circleci/logs"#' \
  -e 's#LoginDatabaseInfo     = "127.0.0.1;3306;trinity;trinity;auth"#LoginDatabaseInfo     = "mysql;3306;trinity;trinity;auth"#' \
  -e 's#WorldDatabaseInfo     = "127.0.0.1;3306;trinity;trinity;world"#WorldDatabaseInfo     = "mysql;3306;trinity;trinity;world"#' \
  -e 's#CharacterDatabaseInfo = "127.0.0.1;3306;trinity;trinity;characters"#CharacterDatabaseInfo = "mysql;3306;trinity;trinity;characters"#' \
  -e 's#SourceDirectory  = ""#SourceDirectory  = "/trinity/source"#' \
  "$CONFIG_DIR/worldserver.conf"

echo "Generated:"
echo "  $CONFIG_DIR/authserver.conf"
echo "  $CONFIG_DIR/worldserver.conf"
