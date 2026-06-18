COMPOSE := docker compose -f docker/docker-compose.yml
CLIENT_DIR ?= docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a
WINEPREFIX ?= $(HOME)/.wine-wow335
IMAGE ?= trinitycore-335-moba:local

.PHONY: help image config db-up db-import db-custom data data-fast client up up-auth up-world down stop logs logs-auth logs-world ps clean-runtime

help:
	@echo "TrinityCore 3.3.5 Docker helpers"
	@echo
	@echo "  make image        Build local TrinityCore image from this source tree"
	@echo "  make config       Generate authserver/worldserver configs"
	@echo "  make db-up        Start MySQL"
	@echo "  make db-import    Import auth, characters, and TDB world"
	@echo "  make db-custom    Import sql/custom into the running databases"
	@echo "  make data-fast    Extract dbc/maps/vmaps, skip mmaps"
	@echo "  make data         Extract dbc/maps/vmaps/mmaps"
	@echo "  make client       Launch WoW through Bottles Flatpak/Wine"
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

image:
	docker build -f docker/Dockerfile -t "$(IMAGE)" .

config:
	./docker/prepare-configs.sh

db-up:
	$(COMPOSE) up -d mysql

db-import: config
	./docker/import-databases.sh

db-custom: db-up
	@for file in sql/custom/auth/*.sql; do [ -e "$$file" ] || continue; echo "Import $$file"; docker exec -i trinity335_mysql mysql -utrinity -ptrinity auth < "$$file"; done
	@for file in sql/custom/characters/*.sql; do [ -e "$$file" ] || continue; echo "Import $$file"; docker exec -i trinity335_mysql mysql -utrinity -ptrinity characters < "$$file"; done
	@for file in sql/custom/world/*.sql; do [ -e "$$file" ] || continue; echo "Import $$file"; docker exec -i trinity335_mysql mysql -utrinity -ptrinity world < "$$file"; done

data-fast:
	SKIP_MMAPS=1 ./docker/extract-client-data.sh "$(CLIENT_DIR)"

data:
	./docker/extract-client-data.sh "$(CLIENT_DIR)"

client:
	flatpak run --command=sh com.usebottles.bottles -lc 'cd "$(CURDIR)/$(CLIENT_DIR)" && WINEPREFIX="$(WINEPREFIX)" wine Wow.exe -opengl'

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
