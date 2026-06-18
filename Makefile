COMPOSE := docker compose -f docker/docker-compose.yml
CLIENT_DIR ?= docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a

.PHONY: help config db-up db-import data data-fast up up-auth up-world down stop logs logs-auth logs-world ps clean-runtime

help:
	@echo "TrinityCore 3.3.5 Docker helpers"
	@echo
	@echo "  make config       Generate authserver/worldserver configs"
	@echo "  make db-up        Start MySQL"
	@echo "  make db-import    Import auth, characters, and TDB world"
	@echo "  make data-fast    Extract dbc/maps/vmaps, skip mmaps"
	@echo "  make data         Extract dbc/maps/vmaps/mmaps"
	@echo "  make up           Start MySQL, authserver, and worldserver"
	@echo "  make up-auth      Start MySQL and authserver"
	@echo "  make up-world     Start MySQL and worldserver"
	@echo "  make stop         Stop containers"
	@echo "  make down         Stop and remove containers/network"
	@echo "  make logs         Follow all logs"
	@echo "  make ps           Show container status"
	@echo
	@echo "Override client path with:"
	@echo "  make data-fast CLIENT_DIR=/path/to/WoW-3.3.5a"

config:
	./docker/prepare-configs.sh

db-up:
	$(COMPOSE) up -d mysql

db-import: config
	./docker/import-databases.sh

data-fast:
	SKIP_MMAPS=1 ./docker/extract-client-data.sh "$(CLIENT_DIR)"

data:
	./docker/extract-client-data.sh "$(CLIENT_DIR)"

up: config
	$(COMPOSE) up -d mysql authserver worldserver

up-auth: config
	$(COMPOSE) up -d mysql authserver

up-world: config
	$(COMPOSE) up -d mysql worldserver

stop:
	$(COMPOSE) stop

down:
	$(COMPOSE) down

logs:
	$(COMPOSE) logs -f

logs-auth:
	$(COMPOSE) logs -f authserver

logs-world:
	$(COMPOSE) logs -f worldserver

ps:
	$(COMPOSE) ps

clean-runtime:
	$(COMPOSE) down
	find docker/mysql/data docker/data docker/logs -mindepth 1 ! -name .gitkeep -exec rm -rf {} +
