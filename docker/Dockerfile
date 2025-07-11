FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    libopenmpi-dev \
    openmpi-bin \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY ./src/ ./src/
COPY ./dms.conf ./

# Compilar tudo numa única linha sem depender do Makefile
RUN mpicc -Wall -Wextra -std=c99 -pthread \
    src/dms.c src/dms_communication.c src/dms_api.c src/dms_config.c src/main.c \
    -o dms -pthread

# Definir ENTRYPOINT para garantir que mpirun seja sempre usado
ENTRYPOINT ["mpirun", "--allow-run-as-root", "--mca", "btl", "^vader"] 