// montador.cpp - executavel: ./montador myprogram.asm -> gera myprogram.obj
#include <fstream>
#include <iostream>
#include <string>

#include "assembler.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "uso: montador <arquivo.asm>\n";
        return 2;
    }
    const std::string entrada = argv[1];

    // A saida substituiria a propria entrada: recusa em vez de sobrescrever.
    if (montagem::temExtensao(entrada, montagem::EXT_OBJETO)) {
        std::cerr << "erro: a entrada nao pode ter a extensao " << montagem::EXT_OBJETO << "\n";
        return 2;
    }

    std::ifstream arquivo(entrada, std::ios::binary);
    if (!arquivo) {
        std::cerr << "erro: nao foi possivel abrir '" << entrada << "'\n";
        return 2;
    }

    const montagem::Resultado resultado = montagem::montar(arquivo);
    if (!resultado.ok()) {
        for (const montagem::Erro& erro : resultado.erros)
            std::cerr << montagem::formatarErro(erro) << "\n";
        return 1;
    }

    const std::string saida = montagem::trocarExtensao(entrada, montagem::EXT_OBJETO);
    if (!montagem::gravarArquivo(saida, montagem::serializarObjeto(resultado))) {
        std::cerr << "erro: nao foi possivel gravar '" << saida << "'\n";
        return 2;
    }
    return 0;
}
