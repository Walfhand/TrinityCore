# TrinityCore 3.3.5 Docker Dev

This folder keeps the local Docker runtime separate from the source tree.

## Layout

- `docker-compose.yml`: MySQL, authserver, and worldserver.
- `configs/`: generated TrinityCore configuration files.
- `data/`: extracted 3.3.5 client data (`dbc`, `maps`, `vmaps`, `mmaps`).
- `mysql/data/`: persistent MySQL files.
- `mysql/init/`: SQL files executed when MySQL is initialized from an empty data directory.
- `logs/`: server logs.

## First Run

Generate local config files:

```bash
./docker/prepare-configs.sh
```

Start MySQL:

```bash
docker compose -f docker/docker-compose.yml up -d mysql
```

MySQL is exposed on the host as `127.0.0.1:3307`; containers use `mysql:3306`.
The image uses MySQL 8.4 because current TrinityCore refuses MySQL servers
older than 8.0.34.

Import base databases after MySQL is ready:

```bash
docker compose -f docker/docker-compose.yml exec -T mysql \
  mysql -uroot -ptrinityroot auth < sql/base/auth_database.sql

docker compose -f docker/docker-compose.yml exec -T mysql \
  mysql -uroot -ptrinityroot characters < sql/base/characters_database.sql
```

The `world` database also needs a full TDB 3.3.5 world SQL dump before the
worldserver can boot cleanly. Put the extracted client data in `docker/data/`.

You can import the base DBs and the official TDB release with:

```bash
./docker/import-databases.sh
```

Extract client data from a local WoW 3.3.5a client:

```bash
./docker/extract-client-data.sh "/path/to/World of Warcraft 3.3.5a"
```

For a faster first run without movement maps:

```bash
SKIP_MMAPS=1 ./docker/extract-client-data.sh "/path/to/World of Warcraft 3.3.5a"
```

Start the servers:

```bash
docker compose -f docker/docker-compose.yml up authserver worldserver
```

## Make Helpers

From the repository root, prefer these wrappers during local development:

```bash
make config
make db-import
make data-fast
make up
make ps
make logs
```

Use `make data-fast` for the first boot. It extracts `dbc`, `maps`, and `vmaps`
but skips `mmaps`. Use `make data` later when you want full movement maps.

## Notes

This currently uses the official vanilla image:

```text
trinitycore/trinitycore:3.3.5
```

When the MOBA C++ changes start, replace the service `image` with a custom
image built from this fork/branch.
