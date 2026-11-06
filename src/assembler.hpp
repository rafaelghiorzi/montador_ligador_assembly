#ifndef ASSEMBLER_HPP
#define ASSEMBLER_HPP

#include <istream>
#include <map>
#include <string>
#include <vector>

namespace montagem {

// Extensoes de saida: definidas em um unico lugar (especificacao 5.1).
constexpr const char* EXT_OBJETO = ".obj";
constexpr const char* EXT_LIGADO = ".lig";

// Limite da memoria da maquina hipotetica, em palavras.
constexpr int TAMANHO_MEMORIA = 65536;

enum class TipoErro { LEXICO, SINTATICO, SEMANTICO };

struct Erro {
    TipoErro tipo;
    int linha;              
    std::string textoLinha; 
    std::string descricao;
};

// Uma entrada da Tabela de Uso: o simbolo externo aparece em codigo[posicao].
struct UsoExterno {
    std::string simbolo;
    int posicao;
};

struct Resultado {
    std::vector<Erro> erros;
    bool ehModulo = false;  // o fonte tem BEGIN
    std::string nomeModulo;
    std::vector<int> codigo;     // TEXT seguida da DATA
    std::vector<int> relocacao;  // 1 bit por palavra de codigo
    std::map<std::string, int> definicoes;  // Tabela de Definicoes (PUBLIC)
    std::vector<UsoExterno> usos;           // Tabela de Uso, por posicao

    bool ok() const { return erros.empty(); }
};

// Monta o fonte em uma unica passagem. Nunca para no primeiro erro.
Resultado montar(std::istream& fonte);

// Formata um numero do .obj ou do .lig, sem zeros a esquerda (8, 128, -3):
// unico ponto de formatacao numerica.
std::string formatarNumero(int valor);

// Uma linha de numeros separados por um espaco, sem terminador.
std::string serializarCodigo(const std::vector<int>& codigo);

// Conteudo do arquivo objeto: linha unica sem BEGIN, formato H/R/D/U/T com BEGIN.
std::string serializarObjeto(const Resultado& resultado);

// Ex.: [SEMANTICO] linha 12: "      JMP FIM" -> rotulo 'FIM' nao declarado
std::string formatarErro(const Erro& erro);

// Troca a extensao apenas no ultimo componente do caminho (dir.v1/x -> dir.v1/x.obj).
std::string trocarExtensao(const std::string& caminho, const std::string& extensao);

// Compara a extensao de caminho, sem diferenciar caixa.
bool temExtensao(const std::string& caminho, const std::string& extensao);

// Grava texto em modo binario, para sair igual em Linux e em Windows.
bool gravarArquivo(const std::string& caminho, const std::string& conteudo);

}  // namespace montagem

#endif
