# Biblioteca de Deduplicação a Nível de Bloco sobre FUSE

**Nota: 19.3/20**

Autores: Diogo Ribeiro, Nuno Rebelo, Marco Ferreira

## Visão Geral

Sistema de ficheiros FUSE que implementa deduplicação a nível de bloco. Todos os blocos de 4 KiB idênticos são deduplicated: apenas uma cópia física é mantida no master file, enquanto blocos duplicados reutilizam essa cópia através de um contador de referências. Os ficheiros vistos pelo utilizador são sequências de referências para posições no master file.

## Arquitetura

A biblioteca usa três tabelas de índice:
- `hash_to_master`: mapeia SHA-512 para MasterInfo (metadados do bloco físico)
- `file_to_master`: mapeia (path, blockIndex) para MasterInfo (metadados do bloco físico)
- `file_to_sizes`: guarda tamanhos lógicos de ficheiros
- `GSList *free_block_list`: lista LIFO de slots reutilizáveis (uint64_t), drenada antes de append ao master file. Gestão em O(1) com inserção/remoção na cabeça.

MasterInfo armazena: hash SHA-512, índice do bloco no master file e contador de referências atómico.

A sincronização combina um pthread_rwlock para metadados, um pthread_mutex dedicado à free list e variáveis atómicas para contadores frequentes.

## Implementações Principais

**Caminho de escrita**: três passes com consolidação atrasada
1. **Decisão** (rdlock metadados): calcula hashes SHA-512, consulta hash_to_master, marca blocos como HIT ou MISS
2. **Flush** (sem lock): aloca slots via free list ou __atomic_fetch_add (append), agrupa MISSes contíguos e emite pwrite por run
3. **Consolidação** (wrlock metadados): double-check em hash_to_master para evitar duplicados, insere em file_to_master e atualiza file_to_sizes

O I/O (pwrite) executa sem qualquer lock, permitindo paralelismo entre threads sobre offsets disjuntos.

**Caminho de leitura**: batching com lock refinado
- **Fase 1** (rdlock metadados): lookup de cada bloco lógico em file_to_master, copia índices físicos para array local. Lock libertado imediatamente após.
- **Fases 2-3** (sem lock): ordena índices físicos (insertion sort para N≤32, qsort acima), agrupa consecutivos, emite pread por grupo contíguo e copia blocos para buffer de saída

**Remoção em lote**: processa unlink/truncate em uma única rotina `remove_blocks_dedup_batch` sob wrlock metadados. Percorre todos os blocos lógicos do ficheiro, consulta file_to_master para obter MasterInfo, decrementa refcount. Quando refcount=1 (última referência), remove entrada de hash_to_master em O(1) e acumula slot em lista local. Apenas no final adquire mutex da free list para concatenar slots de uma vez. Reduz aquisições de lock de 2N para 2, tornando o custo proporcional ao trabalho útil.

## Avaliação

Instrumentação com eBPF para contar syscalls, medir tempo de execução de cada syscall, utilização de page cache e ocupação de disco. Benchmarks com FIO sob diferentes níveis de concorrência (1-12 jobs).

Principais resultados:
- Batching de escrita reduz syscalls pwrite em ~40% (15% duplicados): 1,3M → 783k
- Batching de leitura reduz syscalls pread em ~65% (15% duplicados): 1,41M → 492k
- Refactor de concorrência melhora IOPS de pico em ~16% (8 jobs: 6780 → 7624 IOPS)
- Redução de espaço em disco de 72% com 75% duplicados (1467 MB → 412 MB)

## Gargalos Identificados

1. **Hash SHA-512 serial**: representa 32% do tempo no caminho de escrita. Possível melhoria via pipelining com io_uring ou thread pool para sobrepor cálculo de hashes com pwrite.

2.**Free list sem coalescing**: slots libertados (tombstones) não são agrupados. Agrupar fragmentos dispersos de tombstones é custoso, logo offsets provenientes da free list não permitem batching de runs: cada bloco é escrito isoladamente em vez de agrupado com outros. 

## Trabalho Futuro

- Pipelining SHA-512 com I/O usando io_uring ou thread pool
- Substituir path por inode numérico nas chaves
- Migração para low-level FUSE interface
- Sharding do índice para reduzir contenção
