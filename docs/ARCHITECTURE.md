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

- **Responsabilidade**: Comunicação entre processos usando POSIX message queues
- **Funções principais**:
  - `send_message()` / `receive_message()`: Envio e recebimento de mensagens
  - `handle_message()`: Processamento de mensagens recebidas
  - `invalidate_cache_in_other_processes()`: Protocolo de invalidação

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
[Processo A] ---> [Cache Local] ---> [API le/escreve] ---> [Message Queue] ---> [Processo B]
     |                                       |                                        |
     v                                       v                                        v
[Blocos Locais]                    [Protocolo de Cache]                      [Blocos Locais]
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
- Cache de blocos remotos
- Fila de mensagens
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

1. **Cache LRU**: Implementa replacement simples para otimizar uso de memória
2. **Sincronização**: Mutexes protegem estruturas críticas
3. **Paralelismo**: Thread dedicada para processamento de mensagens
4. **Distribuição**: Hash simples para balanceamento de carga

## Limitações Atuais

1. **Escalabilidade**: Limitado a 16 processos
2. **Tamanho**: Blocos limitados a 4KB
3. **Rede**: Apenas IPC local (mesmo host)
4. **Tolerância a Falhas**: Sem recovery automático
