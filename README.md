# Distributed Memory System (DMS)

Este repositório contém uma implementação de um **Sistema de Memória Distribuída (DMS)** escrita em C e baseada em **MPI**, já empacotada com **Docker** e **Docker Compose**. Assim você compila e executa tudo sem precisar instalar dependências nativas no host.

---

## Requisitos mínimos

| Ferramenta                            | Versão recomendada                        | Observação                                                  |
| ------------------------------------- | ----------------------------------------- | ----------------------------------------------------------- |
| Docker                                | ≥ 24.x                                    | Testado em Ubuntu 22.04, macOS 14 (Intel e Apple Silicon)   |
| Docker Compose (CLI `docker compose`) | já incluso nas versões recentes do Docker | Caso use a versão legada `docker-compose`, adapte o comando |

---

## Passo a passo rápido

```bash
# 1 – Copie o arquivo de configuração de exemplo para a raiz do projeto
$ cp examples/sample.conf dms.conf

# 2 – Construa a imagem e suba os contêineres MPI
$ docker compose -f docker/docker-compose.yml up --build

# 3 – Encerramento
$ docker compose -f docker/docker-compose.yml down
```

O comando acima inicia **quatro processos MPI** (`-np 4`) utilizando o arquivo `dms.conf`. O processo de *rank* 0 executa a bateria de testes; os demais processos aguardam requisições.

---

## Estrutura de diretórios

```
dms/
├─ docker/
│  ├─ Dockerfile                    # Configuração do container para ambiente MPI/C
│  └─ docker-compose.yml            # Orquestração para executar 4 processos MPI
├─ examples/
│  └─ sample.conf                   # Arquivo de configuração exemplo com parâmetros do sistema
├─ src/
│  ├─ dms.c                         # Implementação principal do sistema de memória distribuída
│  ├─ dms.h                         # Definições de estruturas, tipos e protótipos de funções
│  ├─ dms_api.c                     # Implementação das funções de API (le, escreve)
│  ├─ dms_communication.c           # Comunicação MPI entre processos e gerenciamento de cache
│  ├─ dms_config.c                  # Parser de configuração (arquivo e linha de comando)
│  └─ main.c                        # Ponto de entrada e bateria de testes do sistema
├─ .gitignore
├─ README.md
└─ dms.conf                         # Gerado a partir do sample.conf
```

---

## Detalhes do `dms.conf`

| Chave        | Descrição                                                 | Valor padrão | Limites  |
| ------------ | --------------------------------------------------------- | ------------ | -------- |
| `n`          | Número de processos MPI                                   | 4            | 1 – 16   |
| `k`          | Número total de blocos de memória distribuída             | 1000         | > 0      |
| `t`          | Tamanho de cada bloco (bytes)                             | 4096         | 1 – 4096 |
| `process_id` | **Ignorado em tempo de execução** – sobrescrito pelo rank | 0            | 0 – n‑1  |

> Em execuções via Docker/Compose o valor de `process_id` é automaticamente substituído pelo _rank_ retornado pelo MPI, portanto você não precisa ajustá‑lo manualmente.

---

## Membros da equipe

Gustavo Luiz Kohler - 23102480
Leonardo Peres - 23200521
Lucas Nunes Bossle - 23100751
Luigi Remor - 23203395
