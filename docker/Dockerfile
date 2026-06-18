FROM trinitycore/circle-ci:debian-12-builder AS builder

WORKDIR /home/circleci/project

COPY --chown=circleci:circleci . .

USER circleci

RUN mkdir -p bin \
    && cd bin \
    && cmake .. \
      -DWITH_WARNINGS=0 \
      -DWITH_COREDEBUG=0 \
      -DUSE_COREPCH=1 \
      -DUSE_SCRIPTPCH=1 \
      -DTOOLS=1 \
      -DSCRIPTS=static \
      -DSERVERS=1 \
      -DNOJEM=0 \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX=check_install \
      -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' \
    && make -j"$(nproc)" \
    && make install

FROM trinitycore/circle-ci:debian-12-runner

WORKDIR /home/circleci

COPY --from=builder /home/circleci/project/bin/check_install/bin/* /usr/local/bin/
COPY --from=builder /home/circleci/project/bin/check_install/etc /home/circleci/project/bin/check_install/etc
COPY --from=builder /home/circleci/project/sql /home/circleci/project/sql
COPY --from=builder /home/circleci/project/README.md /README.md

ENTRYPOINT [ "echo", "Use --entrypoint=authserver or --entrypoint=worldserver" ]
