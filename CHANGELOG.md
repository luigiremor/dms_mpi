# Changelog

Todas as mudanças notáveis neste projeto serão documentadas neste arquivo.

O formato é baseado em [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
e este projeto adere ao [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-12-19

### Adicionado

- Sistema básico de memória compartilhada distribuída
- API com funções `le()` e `escreve()`
- Protocolo de coerência de cache com write-invalidation
- Comunicação entre processos usando POSIX message queues
- Distribuição de blocos usando hash simples (block_id % n_processes)
- Cache local para blocos remotos com replacement LRU
- Sistema de configuração via arquivo ou linha de comando
- Testes automáticos para validação do sistema
- Modo interativo para experimentação
- Suporte para Docker com ambiente Linux
- Docker Compose para execução de múltiplos processos
- Documentação completa de arquitetura
- Scripts de build automatizado
- Exemplos de configuração

### Características Técnicas

- Suporte para até 16 processos (MAX_PROCESSES)
- Blocos de até 4KB (MAX_BLOCK_SIZE)
- Cache com 128 entradas (CACHE_SIZE)
- Thread dedicada para processamento de mensagens
- Sincronização com mutexes pthread
- Tratamento de erros abrangente

### Estrutura do Projeto

```
dms/
├── src/           # Código fonte principal
├── docs/          # Documentação técnica
├── scripts/       # Scripts de build e automação
├── examples/      # Exemplos de configuração
├── docker/        # Arquivos Docker
└── tests/         # Testes (futuro)
```

### Componentes

- `dms.c` - Gerenciador de blocos e inicialização
- `dms_api.c` - Interface pública (le/escreve)
- `dms_communication.c` - Camada de comunicação
- `dms_config.c` - Gerenciamento de configuração
- `main.c` - Programa principal e testes
