# Sistema de Memória Compartilhada Distribuída (DMS)

## Visão Geral

Este projeto implementa um protótipo de sistema de memória compartilhada distribuída com coerência de cache e resolução de conflitos. O sistema permite que múltiplos processos compartilhem um espaço de endereçamento dividido em blocos, com operações de leitura e escrita transparentes, independentemente da localização física dos dados.

## Arquitetura do Sistema

### Componentes Principais

1. **Gerenciamento de Blocos**: Distribui blocos entre processos usando hash simples (block_id % n_processes)
2. **Cache Local**: Cada processo mantém cache dos blocos remotos acessados com política Round-Robin
3. **Protocolo de Coerência**: Implementa invalidação na escrita (write-invalidation)
4. **Comunicação**: Usa MPI (Message Passing Interface) para comunicação entre processos

### Estrutura de Dados

- **dms_context_t**: Contexto principal contendo configuração, cache e dados locais
- **cache_entry_t**: Entrada de cache com dados, flags de validade e mutex
- **dms_message_t**: Mensagens para comunicação entre processos

## Configuração do Sistema

### Parâmetros

- **n**: Número de processos no grupo
- **k**: Número total de blocos de memória
- **t**: Tamanho de cada bloco em bytes
- **process_id**: ID único do processo (0 a n-1)

### Exemplo de Configuração

Para um sistema com 4GB distribuído entre 4 processos:

- n = 4 processos
- k = 1.000.000 blocos
- t = 4.096 bytes
- Total: 4GB de memória distribuída

## Compilação

### Opção 1: Docker (Recomendado)

A forma mais fácil de executar o sistema é usando Docker, que fornece um ambiente Linux consistente:

```bash
# Construir e executar com Docker Compose
make docker-run

# Ou manualmente:
docker build -t dms-system .
docker-compose up

# Abrir shell no container para testes
make docker-shell

# Limpar containers e imagens
make docker-clean
```

### Opção 2: Compilação Nativa (Linux)

#### Pré-requisitos

- GCC com suporte a C99
- Bibliotecas POSIX (pthread, rt)
- Sistema operacional Linux

#### Comandos de Compilação

```bash
# Compilação básica
make

# Compilação com debug
make debug

# Compilação otimizada
make release

# Limpeza
make clean

# Criar arquivo de configuração exemplo
make config

# Mostrar ajuda
make help
```

## Execução

### Usando Parâmetros de Linha de Comando

```bash
# Processo 0 (master)
./dms -n 4 -k 1000 -t 4096 -p 0

# Processo 1
./dms -n 4 -k 1000 -t 4096 -p 1

# Processo 2
./dms -n 4 -k 1000 -t 4096 -p 2

# Processo 3
./dms -n 4 -k 1000 -t 4096 -p 3
```

### Usando Arquivo de Configuração

1. Criar arquivo `dms.conf`:

```
# Configuração DMS
n 4
k 1000
t 4096
process_id 0
```

2. Executar:

```bash
./dms dms.conf
```

## API do Sistema

### Função de Leitura

```c
int le(int posicao, byte *buffer, int tamanho);
```

- **posicao**: Posição inicial na memória
- **buffer**: Buffer para receber os dados
- **tamanho**: Número de bytes a ler
- **Retorno**: Código de erro (0 = sucesso)

### Função de Escrita

```c
int escreve(int posicao, byte *buffer, int tamanho);
```

- **posicao**: Posição inicial na memória
- **buffer**: Buffer com dados a escrever
- **tamanho**: Número de bytes a escrever
- **Retorno**: Código de erro (0 = sucesso)

### Códigos de Erro

- `DMS_SUCCESS (0)`: Operação bem-sucedida
- `DMS_ERROR_INVALID_POSITION (-1)`: Posição inválida
- `DMS_ERROR_INVALID_SIZE (-2)`: Tamanho inválido
- `DMS_ERROR_BLOCK_NOT_FOUND (-3)`: Bloco não encontrado
- `DMS_ERROR_COMMUNICATION (-4)`: Erro de comunicação
- `DMS_ERROR_MEMORY (-5)`: Erro de memória
- `DMS_ERROR_INVALID_PROCESS (-6)`: Processo inválido

## Mecanismo de Coerência de Cache

### Estratégia de Cache

1. **Cache Miss**: Quando um processo acessa bloco remoto pela primeira vez

   - Envia MSG_READ_REQUEST para o processo dono
   - Recebe MSG_READ_RESPONSE com dados do bloco
   - Armazena no cache local

2. **Cache Hit**: Acessos subsequentes ao mesmo bloco

   - Lê diretamente do cache local
   - Operação mais rápida

3. **Write Invalidation**: Quando um processo escreve em um bloco
   - Se for dono: escreve localmente e invalida caches remotos
   - Se não for dono: envia MSG_WRITE_REQUEST para o dono
   - Dono escreve e envia MSG_INVALIDATE para todos os outros processos

### Protocolo de Mensagens

- `MSG_READ_REQUEST`: Solicitar bloco para leitura
- `MSG_READ_RESPONSE`: Resposta com dados do bloco
- `MSG_WRITE_REQUEST`: Solicitar escrita em bloco remoto
- `MSG_WRITE_RESPONSE`: Confirmação de escrita
- `MSG_INVALIDATE`: Invalidar entrada de cache
- `MSG_INVALIDATE_ACK`: Confirmação de invalidação

### Política de Substituição de Cache

O sistema implementa uma política de substituição **Round-Robin** simples para o cache local:

#### Algoritmo Round-Robin Atual

1. **Busca por Entrada Inválida**: Primeiro tenta encontrar uma entrada de cache marcada como inválida
2. **Substituição Circular**: Se todas as entradas estão válidas, usa um contador circular (`next_victim`) que percorre as 128 entradas sequencialmente
3. **Substituição**: A entrada selecionada é sobrescrita com o novo bloco, independentemente da frequência de uso

#### Implementação

```c
// Se todas as entradas estão válidas, usar Round-Robin
static int next_victim = 0;
cache_entry_t *victim = &cache[next_victim];
next_victim = (next_victim + 1) % CACHE_SIZE;
```

#### Vantagens do Round-Robin

- **Simplicidade**: Implementação simples e eficiente
- **Baixo Overhead**: Não requer rastreamento de timestamps ou contadores de acesso
- **Determinístico**: Comportamento previsível
- **Thread-Safe**: Fácil de sincronizar

#### Limitações do Round-Robin

- **Não considera localidade temporal**: Pode remover blocos frequentemente acessados
- **Performance sub-ótima**: Para workloads com padrões de acesso específicos
- **Fairness desnecessária**: Trata todos os blocos igualmente

#### Extensões Futuras: LRU Real

Para melhorar a performance, o sistema pode ser estendido com **Least Recently Used (LRU)**:

##### Implementação LRU Proposta

```c
typedef struct {
    int block_id;
    byte *data;
    int valid;
    int dirty;
    uint64_t last_access_time;  // Timestamp do último acesso
    pthread_mutex_t mutex;
} cache_entry_lru_t;

// Função de substituição LRU
cache_entry_t *allocate_cache_entry_lru(int block_id) {
    uint64_t oldest_time = UINT64_MAX;
    int lru_index = 0;

    // Encontrar entrada menos recentemente usada
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            // Entrada livre encontrada
            return setup_cache_entry(&cache[i], block_id);
        }
        if (cache[i].last_access_time < oldest_time) {
            oldest_time = cache[i].last_access_time;
            lru_index = i;
        }
    }

    return setup_cache_entry(&cache[lru_index], block_id);
}
```

##### Vantagens do LRU

- **Localidade Temporal**: Mantém blocos recentemente acessados
- **Performance Melhorada**: Para workloads com padrões de acesso repetitivos
- **Adaptativo**: Se ajusta automaticamente ao padrão de acesso da aplicação

##### Desvantagens do LRU

- **Overhead de Memória**: Requer armazenar timestamps
- **Overhead Computacional**: Busca pela entrada mais antiga a cada substituição
- **Complexidade**: Maior complexidade de implementação e debugging

##### Implementações LRU Alternativas

1. **LRU com Lista Duplamente Ligada**: O(1) para acesso, O(1) para substituição
2. **Approximated LRU**: Usa bits de referência para aproximar LRU com menor overhead
3. **Clock Algorithm**: Combinação de simplicidade e eficiência

## Casos de Teste

### Teste 1: Operações Básicas

- Escrever string "ALO MUNDO" na posição 0
- Ler de volta e verificar consistência
- **Objetivo**: Validar operações básicas de leitura/escrita

### Teste 2: Operações Cross-Block

- Escrever dados que atravessam múltiplos blocos
- Ler de volta dados fragmentados
- **Objetivo**: Verificar operações que abrangem múltiplos blocos

### Teste 3: Comportamento de Cache

- Primeira leitura de bloco remoto (cache miss)
- Segunda leitura do mesmo bloco (cache hit)
- **Objetivo**: Validar funcionamento do cache

### Teste 4: Coerência de Cache

- Processo A lê bloco remoto (cache miss)
- Processo B escreve no mesmo bloco
- Processo A lê novamente (deve ver dados atualizados)
- **Objetivo**: Verificar invalidação de cache

### Teste 5: Condições de Corrida

- Múltiplos processos escrevem simultaneamente
- Verificar consistência final dos dados
- **Objetivo**: Testar resolução de conflitos

## Execução de Testes

### Teste Automático

O processo 0 (master) executa automaticamente todos os testes:

```bash
# Terminal 1
./dms -n 4 -k 100 -t 1024 -p 0

# Terminal 2
./dms -n 4 -k 100 -t 1024 -p 1

# Terminal 3
./dms -n 4 -k 100 -t 1024 -p 2

# Terminal 4
./dms -n 4 -k 100 -t 1024 -p 3
```

### Modo Interativo

No processo master (PID 0), após os testes automáticos:

```
dms> write 0 HelloWorld
dms> read 0 10
dms> write 5000 TestData
dms> read 5000 8
dms> quit
```

### Comandos Interativos

- `write <posição> <dados>`: Escrever dados na posição
- `read <posição> <tamanho>`: Ler dados da posição
- `quit` ou `exit`: Sair do sistema

## Estrutura de Arquivos

```
dms/
├── src/                     # Código fonte
│   ├── dms.h               # Definições principais
│   ├── dms.c               # Inicialização e funções core
│   ├── dms_communication.c # Comunicação entre processos
│   ├── dms_api.c          # Implementação das APIs le() e escreve()
│   ├── dms_config.c       # Gerenciamento de configuração
│   └── main.c             # Programa principal e testes
├── docker/                 # Configuração Docker
│   ├── Dockerfile         # Container para Linux
│   └── docker-compose.yml # Múltiplos processos
├── docs/                   # Documentação
│   └── ARCHITECTURE.md    # Arquitetura do sistema
├── scripts/                # Scripts de automação
│   └── build.sh           # Script de build
├── examples/               # Exemplos
│   └── sample.conf        # Configuração de exemplo
├── tests/                  # Testes (futuro)
├── Makefile               # Build system
├── README.md              # Esta documentação
├── CHANGELOG.md           # Registro de mudanças
├── .gitignore             # Arquivos ignorados
└── dms.conf               # Arquivo de configuração ativo
```

## Limitações e Considerações

### Limitações Atuais

1. **Número Máximo de Processos**: 16 (MAX_PROCESSES)
2. **Tamanho Máximo do Bloco**: 4096 bytes (MAX_BLOCK_SIZE)
3. **Número Máximo de Blocos**: 1.000.000 (MAX_BLOCKS)
4. **Tamanho do Cache**: 128 entradas (CACHE_SIZE)

### Considerações de Performance

1. **Cache Round-Robin**: Implementa substituição simples round-robin; extensão futura pode incluir LRU real
2. **Sincronização**: Usa mutexes para proteger estruturas críticas
3. **Comunicação**: MPI pode ter latência dependendo da implementação
4. **Distribuição**: Hash simples pode causar desbalanceamento

### Tolerância a Falhas

- **Detecção de Falhas**: Timeouts em operações de comunicação
- **Recuperação**: Cleanup automático de recursos
- **Consistência**: Protocolo garante consistência eventual

## Troubleshooting

### Problemas Comuns

1. **MPI Initialization Error**: Verificar se MPI está configurado corretamente
2. **Communication Error**: Verificar se todos os processos MPI estão rodando
3. **Segmentation Fault**: Verificar parâmetros de configuração
4. **Process Count Mismatch**: Número de processos MPI deve corresponder ao parâmetro 'n'

### Debug

```bash
# Compilar com debug
make debug

# Executar com GDB
gdb ./dms
(gdb) run -n 4 -k 100 -t 1024 -p 0
```

### Logs do Sistema

O sistema imprime informações detalhadas sobre:

- Inicialização dos processos
- Cache hits/misses
- Operações de invalidação
- Comunicação entre processos

## Autores

Este projeto foi desenvolvido como trabalho acadêmico para demonstrar conceitos de sistemas distribuídos, coerência de cache e programação concorrente.

## Licença

Este projeto é destinado exclusivamente para fins educacionais e acadêmicos.
