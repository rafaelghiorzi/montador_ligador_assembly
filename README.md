# Montador e ligador da máquina hipotética

Trabalho de Software Básico, feito em C++17

## Compilar

Linux:

```
make
```

Windows (MinGW):

```
g++ -std=c++17 -Wall -Wextra -O2 src/montador.cpp src/assembler.cpp -o montador.exe
g++ -std=c++17 -Wall -Wextra -O2 src/ligador.cpp src/assembler.cpp -o ligador.exe
```

## Usar

```
./montador myprogram.asm                     # gera myprogram.obj
./ligador prog1.asm [prog2.asm [prog3.asm]]  # gera progN.obj e prog1.lig
```

Se houver erro, o montador lista todos na saída de erro, não gera o arquivo e
termina com código diferente de 0.

Formato de cada erro:

```
[SEMANTICO] linha 12: "      JMP FIM" -> rotulo 'FIM' nao declarado
```

Erros do arquivo inteiro, como a falta de `SECAO TEXT`, saem com `linha 0`.

## Testar

```
make test
```

O script `testes/rodar_testes.sh` usa pastas temporárias, então não grava nada
em `testes/` nem em `exemplos/`. Ele confere:

- os exemplos do documento, byte a byte;
- os erros esperados, com a mensagem exata;
- os exemplos do professor, comparando os valores numéricos;
- a execução no simulador do professor.

## Organização

| Arquivo | Conteúdo |
|---|---|
| `src/assembler.hpp`, `src/assembler.cpp` | Núcleo compartilhado: léxico, sintaxe, semântica, síntese |
| `src/montador.cpp` | Executável do montador |
| `src/ligador.cpp` | Executável do ligador |
| `testes/` | Casos de teste e script de comparação |
| `DECISOES.md` | Decisões de implementação e resultados no simulador |

`src/main.cpp` é o "Hello, World" que já estava no repositório e não entra na compilação.

## `SECAO` ou `SECTION`

O montador aceita as duas formas. O PDF do trabalho usa `SECAO` e os arquivos em
`exemplos/` usam `SECTION`, ou seja, o próprio professor usa as duas. O simulador
não lê código-fonte, então não ajuda a escolher.

## Simulador

```
cp simulador/simulador /tmp/sim && chmod +x /tmp/sim   # o arquivo veio sem permissão de execução
/tmp/sim programa.obj
```

Ele recebe o arquivo como argumento, lê as entradas da entrada padrão e imprime
um número por linha. Um opcode inválido ou 0 o deixa em laço infinito, sem
mensagem, então vale rodar com `timeout`.
