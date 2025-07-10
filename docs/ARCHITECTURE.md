# Arquitetura do Sistema DMS

## Visão Geral

O Sistema de Memória Compartilhada Distribuída (DMS) é composto por múltiplos componentes que trabalham juntos para fornecer uma abstração de memória compartilhada entre processos distribuídos.

## Componentes Principais

### 1. Gerenciador de Blocos (`dms.c`)

- **Responsabilidade**: Inicialização do sistema e gerenciamento de blocos locais
- **Funções principais**:
  - `dms_init()`: Inicializa contexto e estruturas de dados
  - `get_block_owner()`: Determina qual processo possui um bloco
  - `get_local_block_data()`: Acessa dados de blocos locais

### 2. Camada de Comunicação (`dms_communication.c`)

- **Responsabilidade**: Comunicação entre processos usando MPI (Message Passing Interface)
- **Funções principais**:
  - `send_message()` / `receive_message()`: Envio e recebimento de mensagens via MPI
  - `handle_message()`: Processamento de mensagens recebidas
  - `invalidate_cache_in_other_processes()`: Protocolo de invalidação distribuída

### 3. API do Sistema (`dms_api.c`)

- **Responsabilidade**: Interface pública para leitura e escrita
- **Funções principais**:
  - `le()`: Operação de leitura com cache transparente
  - `escreve()`: Operação de escrita com invalidação
  - `invalidate_cache_entry()`: Invalidação local de cache

### 4. Gerenciamento de Configuração (`dms_config.c`)

- **Responsabilidade**: Parsing e validação de configurações
- **Funções principais**:
  - `load_config_from_file()`: Carrega configuração de arquivo
  - `parse_command_line_config()`: Processa argumentos da linha de comando

### 5. Programa Principal (`main.c`)

- **Responsabilidade**: Testes e demonstração do sistema
- **Funções principais**:
  - Testes automáticos de funcionalidade
  - Modo interativo para experimentação

## Fluxo de Dados

```
[Processo A] ---> [Cache Local] ---> [API le/escreve] ---> [MPI] ---> [Processo B]
     |                                       |                              |
     v                                       v                              v
[Blocos Locais]                    [Protocolo de Cache]            [Blocos Locais]
```

## Protocolo de Coerência de Cache

### Operação de Leitura

1. Verifica se é bloco local → acesso direto
2. Se remoto → verifica cache local
3. Cache hit → retorna dados
4. Cache miss → solicita bloco do dono
5. Armazena no cache e retorna dados

### Operação de Escrita

1. Se bloco local → escreve diretamente + invalida caches remotos
2. Se bloco remoto → envia requisição de escrita ao dono
3. Dono escreve localmente e invalida todos os caches
4. Confirma operação de volta ao solicitante

## Estruturas de Dados

### `dms_context_t`

Contexto global do sistema contendo:

- Configuração do sistema
- Blocos locais
- Cache de blocos remotos (Round-Robin)
- Recursos MPI (rank, size, mutex)
- Mapeamento de donos de blocos

### `cache_entry_t`

Entrada de cache contendo:

- ID do bloco
- Dados do bloco
- Flags de validade
- Mutex para sincronização

### `dms_message_t`

Mensagem de comunicação contendo:

- Tipo da mensagem
- IDs de processo origem/destino
- ID do bloco
- Posição e tamanho
- Dados do bloco

## Considerações de Performance

1. **Cache Round-Robin**: Implementa substituição simples circular; LRU real pode ser implementado futuramente
2. **Sincronização**: Mutexes protegem estruturas críticas e operações MPI
3. **Comunicação MPI**: Operações síncronas com timeouts para evitar bloqueios
4. **Distribuição**: Hash simples para balanceamento de carga entre processos

## Política de Cache

### Implementação Atual: Round-Robin

O sistema implementa uma política de substituição **Round-Robin** para o cache local:

#### Algoritmo de Alocação

```c
cache_entry_t *allocate_cache_entry(int block_id) {
    // 1. Busca entrada inválida
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            return setup_entry(&cache[i], block_id);
        }
    }

    // 2. Round-Robin se cache cheio
    static int next_victim = 0;
    cache_entry_t *victim = &cache[next_victim];
    next_victim = (next_victim + 1) % CACHE_SIZE;
    return setup_entry(victim, block_id);
}
```

#### Características

- **Simplicidade**: Algoritmo determinístico e eficiente
- **Baixo Overhead**: Não requer rastreamento de timestamps
- **Thread-Safe**: Protegido por `cache_mutex`
- **Previsível**: Comportamento consistente

#### Limitações

- **Não considera localidade temporal**: Pode substituir blocos frequentemente acessados
- **Performance sub-ótima**: Para workloads com padrões específicos

### Extensões Futuras: LRU

#### Implementação LRU Proposta

Para melhorar a performance em cenários com localidade temporal:

```c
typedef struct {
    int block_id;
    byte *data;
    int valid;
    int dirty;
    uint64_t last_access;  // Timestamp ou contador
    struct cache_entry *prev, *next;  // Lista duplamente ligada
    pthread_mutex_t mutex;
} cache_entry_lru_t;
```

#### Vantagens LRU

- **Localidade Temporal**: Mantém blocos recentemente acessados
- **Adaptativo**: Se ajusta ao padrão de acesso
- **Performance**: Melhor para workloads repetitivos

#### Alternativas de Implementação

1. **LRU com Lista**: O(1) acesso e substituição
2. **Clock Algorithm**: Aproximação eficiente do LRU
3. **LRU-K**: Considera K referências passadas
4. **Segmented LRU**: Combina LRU com outros critérios

## Limitações Atuais

1. **Escalabilidade**: Limitado a 16 processos (MAX_PROCESSES)
2. **Tamanho**: Blocos limitados a 4KB (MAX_BLOCK_SIZE)
3. **Comunicação**: Dependente da implementação MPI disponível
4. **Cache**: Política Round-Robin simples, não otimizada para localidade temporal
5. **Tolerância a Falhas**: Sem recovery automático de processos MPI
