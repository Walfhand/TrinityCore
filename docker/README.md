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
make image
make db-import
make db-custom
make data-fast
make up
make client
make ps
make logs
```

Use `make data-fast` for the first boot. It extracts `dbc`, `maps`, and `vmaps`
but skips `mmaps`. Use `make data` later when you want full movement maps.
Use `make client` to launch the local WoW client through the Bottles Flatpak
Wine runtime.
Use `make image` after C++ changes so Docker runs the locally modified core.
Use `make db-custom` after SQL changes in `sql/custom`.

## MOBA Prototype

The first custom gameplay slice adds a lobby NPC:

- Entry: `900000`
- Script: `npc_moba_lobby`
- Spawn: GM Island, map `1`, around `16222 16266 13`

Find the current lobby guid after `make db-custom` with:

```bash
docker compose -f docker/docker-compose.yml exec -T mysql \
  mysql -uroot -ptrinityroot world \
  -e "SELECT guid,id,map,position_x,position_y,position_z FROM creature WHERE id=900000;"
```

It also adds a first win-condition objective:

- Entry: `900001`
- Script: `npc_moba_nexus`
- Spawn: dynamic summon in the solo test instance, map `36`
- Prototype behavior: using `Tag solo - Test Nexus` teleports the player into
  the solo test, summons the Nexus, and killing it announces victory before
  returning the player to the lobby.

After changing custom C++ or SQL, run:

```bash
make image
make db-custom
docker compose -f docker/docker-compose.yml up -d --force-recreate authserver worldserver
```

In game, a GM can jump to the selector with:

```text
.go creature <lobby_guid>
```

The current selector applies prototype hero kits on top of the existing
character class. It can also queue a solo Nexus test so we can validate the
match entry flow, spell-set feel, and the basic victory loop before changing
character creation or client data.

## Notes

This setup runs a locally built image:

```text
trinitycore-335-moba:local
```

The image is built from the current source tree with `make image`.
