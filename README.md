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

> _Vale verificar as permissões do simulador antes de rodar, porque o arquivo disponibilizado no Aprender3 estava com permissões que não deixavam executar o arquivo._

## Usar (com os exemplos)

```
./montador exemplos/program.asm                                # gera o código objeto
./ligador  exemplos/prog1.asm exemplos/prog2.asm             # gera os códigos objeto e prog1.lig
```

Se houver erro, o montador lista todos na saída de erro, não gera o arquivo e
termina com código diferente de 0.

Formato de cada erro:

```
[SEMANTICO] linha 12: "      JMP FIM" -> rotulo 'FIM' nao declarado
```

Erros do arquivo inteiro, como a falta de `SECAO TEXT`, saem com `linha 0`.

## Simulador

```
./simulador exemplos/program.obj

./simulador exemplos/prog1.lig
```

Ele recebe o arquivo como argumento, lê as entradas da entrada padrão e imprime
um número por linha. Um opcode inválido ou 0 o deixa em laço infinito, sem
mensagem, então vale rodar com `timeout`.
