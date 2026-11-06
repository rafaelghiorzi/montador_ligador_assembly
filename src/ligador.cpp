// ligador.cpp - executavel: ./ligador prog1.asm [prog2.asm [prog3.asm]]
//
// Monta cada modulo com o nucleo compartilhado (gera progN.obj) e liga tudo em
// <primeiro arquivo>.lig, seguindo o algoritmo de aula:
//   alinhar -> fatores de correcao -> TGD -> corrigir usos e relocacao -> concatenar.
#include <cstddef>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assembler.hpp"

namespace {

struct Modulo {
    std::string arquivo;
    montagem::Resultado objeto;
    int fator = 0;  // fator de correcao
};

// Monta um arquivo, mostra seus erros e grava o objeto se estiver correto.
// Devolve false se houve qualquer problema.
bool montarModulo(const std::string& caminho, Modulo& modulo) {
    modulo.arquivo = caminho;
    if (montagem::temExtensao(caminho, montagem::EXT_OBJETO) ||
        montagem::temExtensao(caminho, montagem::EXT_LIGADO)) {
        std::cerr << "erro: '" << caminho << "' deve ser um arquivo fonte\n";
        return false;
    }
    std::ifstream arquivo(caminho, std::ios::binary);
    if (!arquivo) {
        std::cerr << "erro: nao foi possivel abrir '" << caminho << "'\n";
        return false;
    }

    modulo.objeto = montagem::montar(arquivo);
    if (!modulo.objeto.ok()) {
        std::cerr << caminho << ":\n";
        for (const montagem::Erro& erro : modulo.objeto.erros)
            std::cerr << montagem::formatarErro(erro) << "\n";
        return false;
    }
    if (!modulo.objeto.ehModulo) {
        std::cerr << "[LIGADOR] " << caminho << ": nao e um modulo (falta BEGIN/END)\n";
        return false;
    }

    const std::string saida = montagem::trocarExtensao(caminho, montagem::EXT_OBJETO);
    if (!montagem::gravarArquivo(saida, montagem::serializarObjeto(modulo.objeto))) {
        std::cerr << "erro: nao foi possivel gravar '" << saida << "'\n";
        return false;
    }
    return true;
}

// Liga os modulos; em caso de erro, mostra todos e devolve false.
bool ligar(std::vector<Modulo>& modulos, std::vector<int>& executavel) {
    bool ok = true;

    // Passos 1 e 2: alinhar na ordem dos argumentos e calcular os fatores.
    long long tamanhoTotal = 0;
    for (Modulo& modulo : modulos) {
        modulo.fator = static_cast<int>(tamanhoTotal);
        tamanhoTotal += static_cast<long long>(modulo.objeto.codigo.size());
    }
    if (tamanhoTotal > montagem::TAMANHO_MEMORIA) {
        std::cerr << "[LIGADOR] programa ligado excede a memoria da maquina\n";
        ok = false;
    }

    // Passo 3: Tabela Global de Definicoes.
    std::map<std::string, int> tgd;
    for (const Modulo& modulo : modulos) {
        for (const auto& definicao : modulo.objeto.definicoes) {
            if (!tgd.emplace(definicao.first, definicao.second + modulo.fator).second) {
                std::cerr << "[LIGADOR] simbolo '" << definicao.first
                          << "' definido em mais de um modulo (" << modulo.arquivo << ")\n";
                ok = false;
            }
        }
    }

    // Passo 4: corrigir o codigo de cada modulo.
    for (Modulo& modulo : modulos) {
        std::vector<int>& codigo = modulo.objeto.codigo;
        std::set<int> posicoesDeUso;
        for (const montagem::UsoExterno& uso : modulo.objeto.usos) {
            posicoesDeUso.insert(uso.posicao);
            const auto definicao = tgd.find(uso.simbolo);
            if (definicao == tgd.end()) {
                std::cerr << "[LIGADOR] simbolo '" << uso.simbolo << "' usado em "
                          << modulo.arquivo << " nao foi definido por nenhum modulo\n";
                ok = false;
                continue;
            }
            codigo[uso.posicao] += definicao->second;
        }
        for (std::size_t pos = 0; pos < codigo.size(); ++pos) {
            if (modulo.objeto.relocacao[pos] == 1 &&
                posicoesDeUso.count(static_cast<int>(pos)) == 0) {
                codigo[pos] += modulo.fator;
            }
        }
    }
    if (!ok) return false;

    // Passo 5: concatenar.
    for (const Modulo& modulo : modulos)
        executavel.insert(executavel.end(), modulo.objeto.codigo.begin(), modulo.objeto.codigo.end());
    return true;
}

}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        std::cerr << "uso: ligador prog1.asm [prog2.asm [prog3.asm]]\n";
        return 2;
    }

    // Monta todos os modulos antes de decidir, para mostrar todos os erros.
    std::vector<Modulo> modulos(static_cast<std::size_t>(argc - 1));
    bool ok = true;
    for (int i = 1; i < argc; ++i)
        ok = montarModulo(argv[i], modulos[static_cast<std::size_t>(i - 1)]) && ok;
    if (!ok) return 1;

    std::vector<int> executavel;
    if (!ligar(modulos, executavel)) return 1;

    const std::string saida = montagem::trocarExtensao(argv[1], montagem::EXT_LIGADO);
    if (!montagem::gravarArquivo(saida, montagem::serializarCodigo(executavel) + "\n")) {
        std::cerr << "erro: nao foi possivel gravar '" << saida << "'\n";
        return 2;
    }
    return 0;
}
