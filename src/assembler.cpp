#include "assembler.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <tuple>
#include <utility>

namespace montagem {

namespace {

struct Instrucao {
    int opcode;
    int operandos;
};

const std::map<std::string, Instrucao>& tabelaInstrucoes() {
    static const std::map<std::string, Instrucao> tabela = {
        {"ADD", {1, 1}},    {"SUB", {2, 1}},   {"MUL", {3, 1}},
        {"MULT", {3, 1}},  // MULT é como o exemplo está escrito
        {"DIV", {4, 1}},    {"JMP", {5, 1}},   {"JMPN", {6, 1}},
        {"JMPP", {7, 1}},   {"JMPZ", {8, 1}},  {"COPY", {9, 2}},
        {"LOAD", {10, 1}},  {"STORE", {11, 1}}, {"INPUT", {12, 1}},
        {"OUTPUT", {13, 1}}, {"STOP", {14, 0}},
    };
    return tabela;
}

bool ehDiretiva(const std::string& nome) {
    static const char* const diretivas[] = {"SECAO", "SECTION", "SPACE", "CONST", "EQU",
                                            "IF",    "BEGIN",   "END",   "PUBLIC", "EXTERN"};
    return std::any_of(std::begin(diretivas), std::end(diretivas),
                       [&](const char* d) { return nome == d; });
}

bool ehReservada(const std::string& nome) {
    return tabelaInstrucoes().count(nome) != 0 || ehDiretiva(nome);
}

// Tipos internos

enum class TipoToken {
    IDENTIFICADOR,
    NUMERO,       
    DOIS_PONTOS,
    MAIS,
    MENOS,
    INVALIDO
};

struct Token {
    TipoToken tipo;
    std::string texto;
    long long valor;
};

enum class Secao { NENHUMA, TEXT, DATA, EQU, EXTERN };

struct Pendencia {
    int posicao;
    int linha;  
};

// Entrada da Tabela de Simbolos.
struct Simbolo {
    bool definido = false;
    Secao secao = Secao::NENHUMA;
    int valor = 0;  // deslocamento dentro da secao (EQU: o proprio numero)
    int linhaDefinicao = 0;
    std::vector<Pendencia> pendencias;
};

struct Rotulo {
    std::string nome;
    int linha;
};

struct Operando {
    std::string rotulo;
    int deslocamento;
};

struct Exportacao {
    std::string nome;
    int linha;
};

// Valor que nao cabe em palavra nenhuma: marca literais grandes demais.
const long long NUMERO_GRANDE = 1000000000000LL;

// Montador começa aqui

class Montador {
public:
    Resultado executar(std::istream& fonte) {
        std::string linha;
        while (std::getline(fonte, linha)) {
            if (textos_.empty()) removerBom(linha);
            if (!linha.empty() && linha.back() == '\r') linha.pop_back();
            textos_.push_back(linha);
            linhaAtual_ = static_cast<int>(textos_.size());
            processarLinha(linha);
        }
        finalizar();
        return std::move(resultado_);
    }

private:
    std::vector<std::string> textos_;  // so para imprimir erros; nunca e reanalisado
    int linhaAtual_ = 0;
    Secao secaoAtual_ = Secao::NENHUMA;
    bool viuSecao_ = false;
    bool viuText_ = false;
    bool viuData_ = false;
    bool descartarProxima_ = false;  // efeito de IF com valor 0
    std::vector<Rotulo> rotulosPendentes_;


    std::vector<int> codigoText_;
    std::vector<int> relocacaoText_;
    std::vector<int> codigoData_;
    std::map<std::string, Simbolo> simbolos_;


    int linhaBegin_ = 0;
    int linhaEnd_ = 0;
    std::vector<Exportacao> exportacoes_;

    Resultado resultado_;

    void erro(TipoErro tipo, const std::string& descricao) {
        erroNaLinha(tipo, linhaAtual_, descricao);
    }

    void erroNaLinha(TipoErro tipo, int linha, const std::string& descricao) {
        std::string texto;
        if (linha >= 1 && linha <= static_cast<int>(textos_.size()))
            texto = textos_[static_cast<std::size_t>(linha - 1)];
        resultado_.erros.push_back({tipo, linha, texto, descricao});
    }

    void lexico(const std::string& d) { erro(TipoErro::LEXICO, d); }
    void sintatico(const std::string& d) { erro(TipoErro::SINTATICO, d); }
    void semantico(const std::string& d) { erro(TipoErro::SEMANTICO, d); }

    // FASE 1: analise lexica

    static void removerBom(std::string& linha) {
        if (linha.compare(0, 3, "\xEF\xBB\xBF") == 0) linha.erase(0, 3);
    }

    static bool ehEspaco(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
    static bool ehDigito(char c) { return c >= '0' && c <= '9'; }
    static bool ehCaractereDeNome(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || ehDigito(c) || c == '_';
    }
    static bool ehDelimitador(char c) { return c == ':' || c == '+' || c == '-'; }

    static std::string paraMaiusculas(std::string texto) {
        for (char& c : texto)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return texto;
    }

    // Devolve a linha sem o comentario.
    static std::string removerComentario(const std::string& linha) {
        const std::size_t pontoEVirgula = linha.find(';');
        return pontoEVirgula == std::string::npos ? linha : linha.substr(0, pontoEVirgula);
    }

    static bool linhaVazia(const std::string& texto) {
        return std::all_of(texto.begin(), texto.end(), ehEspaco);
    }

    // Devolve o caractere invalido inteiro, mesmo que ocupe varios bytes em UTF-8.
    static std::string caractereEm(const std::string& palavra, std::size_t i) {
        std::size_t fim = i + 1;
        if (static_cast<unsigned char>(palavra[i]) >= 0xC0)
            while (fim < palavra.size() && (static_cast<unsigned char>(palavra[fim]) & 0xC0) == 0x80)
                ++fim;
        return palavra.substr(i, fim - i);
    }

    Token classificarPalavra(const std::string& original, bool seguidaDeDoisPontos) {
        const std::string palavra = paraMaiusculas(original);

        for (std::size_t i = 0; i < palavra.size(); ++i) {
            if (!ehCaractereDeNome(palavra[i])) {
                lexico("caractere invalido '" + caractereEm(palavra, i) + "' em '" + palavra + "'");
                return {TipoToken::INVALIDO, palavra, 0};
            }
        }

        if (ehDigito(palavra[0])) {
            if (seguidaDeDoisPontos) {
                lexico("rotulo '" + palavra + "' comeca com digito");
                return {TipoToken::INVALIDO, palavra, 0};
            }
            if (!std::all_of(palavra.begin(), palavra.end(), ehDigito)) {
                lexico("numero malformado '" + palavra + "'");
                return {TipoToken::INVALIDO, palavra, 0};
            }
            const long long valor = palavra.size() > 12 ? NUMERO_GRANDE : std::stoll(palavra);
            return {TipoToken::NUMERO, palavra, valor};
        }

        if (palavra.size() > 30) lexico("rotulo '" + palavra + "' excede 30 caracteres");
        return {TipoToken::IDENTIFICADOR, palavra, 0};
    }

    std::vector<Token> analiseLexica(const std::string& texto) {
        std::vector<Token> tokens;
        std::size_t i = 0;
        while (i < texto.size()) {
            const char c = texto[i];
            if (ehEspaco(c)) {
                ++i;
            } else if (c == ':') {
                tokens.push_back({TipoToken::DOIS_PONTOS, ":", 0});
                ++i;
            } else if (c == '+') {
                tokens.push_back({TipoToken::MAIS, "+", 0});
                ++i;
            } else if (c == '-') {
                tokens.push_back({TipoToken::MENOS, "-", 0});
                ++i;
            } else {
                const std::size_t inicio = i;
                while (i < texto.size() && !ehEspaco(texto[i]) && !ehDelimitador(texto[i])) ++i;
                std::size_t j = i;
                while (j < texto.size() && ehEspaco(texto[j])) ++j;
                const bool seguida = j < texto.size() && texto[j] == ':';
                tokens.push_back(classificarPalavra(texto.substr(inicio, i - inicio), seguida));
            }
        }
        return tokens;
    }

    // FASE 2: analise sintatica

    void processarLinha(const std::string& linha) {
        const std::string util = removerComentario(linha);
        if (linhaVazia(util)) return;  // comentario ou linha em branco nao contam

        if (descartarProxima_) {
            descartarProxima_ = false;
            return;
        }

        const std::vector<Token> tokens = analiseLexica(util);

        // Forma BEGIN: NOME dos exemplos do professor
        if (tokens.size() == 3 && tokens[0].tipo == TipoToken::IDENTIFICADOR &&
            tokens[0].texto == "BEGIN" && tokens[1].tipo == TipoToken::DOIS_PONTOS &&
            tokens[2].tipo == TipoToken::IDENTIFICADOR && !ehReservada(tokens[2].texto)) {
            tratarBegin(tokens[2].texto);
            return;
        }

        // [ROTULO:] ... (coleta rotulos; mais de um por linha e erro).
        std::vector<Rotulo> rotulos = std::move(rotulosPendentes_);
        rotulosPendentes_.clear();
        std::size_t pos = 0;
        while (pos < tokens.size()) {
            if (tokens[pos].tipo == TipoToken::DOIS_PONTOS) {
                sintatico("':' sem rotulo");
                ++pos;
            } else if (pos + 1 < tokens.size() && tokens[pos + 1].tipo == TipoToken::DOIS_PONTOS) {
                const Token& candidato = tokens[pos];
                if (candidato.tipo == TipoToken::IDENTIFICADOR) {
                    if (ehReservada(candidato.texto))
                        sintatico("rotulo com nome reservado '" + candidato.texto + "'");
                    if (!rotulos.empty()) sintatico("dois rotulos para a mesma linha");
                    rotulos.push_back({candidato.texto, linhaAtual_});
                } else if (candidato.tipo != TipoToken::INVALIDO) {
                    sintatico("':' sem rotulo");
                }
                pos += 2;
            } else {
                break;
            }
        }

        // Rotulo sozinho: vale para a proxima linha util.
        if (pos >= tokens.size()) {
            rotulosPendentes_ = std::move(rotulos);
            return;
        }

        const Token& operacao = tokens[pos];
        ++pos;
        if (operacao.tipo != TipoToken::IDENTIFICADOR) {
            if (operacao.tipo != TipoToken::INVALIDO)
                sintatico("operacao esperada, encontrado '" + operacao.texto + "'");
            definirRotulos(rotulos);
            return;
        }

        const auto instrucao = tabelaInstrucoes().find(operacao.texto);
        if (instrucao != tabelaInstrucoes().end()) {
            tratarInstrucao(operacao.texto, instrucao->second, tokens, pos, rotulos);
        } else if (operacao.texto == "SECAO" || operacao.texto == "SECTION") {
            recusarRotulos(rotulos, operacao.texto);
            tratarSecao(tokens, pos);
        } else if (operacao.texto == "SPACE") {
            tratarSpace(tokens, pos, rotulos);
        } else if (operacao.texto == "CONST") {
            tratarConst(tokens, pos, rotulos);
        } else if (operacao.texto == "EQU") {
            tratarEqu(tokens, pos, rotulos);
        } else if (operacao.texto == "IF") {
            recusarRotulos(rotulos, operacao.texto);
            tratarIf(tokens, pos);
        } else if (operacao.texto == "BEGIN") {
            tratarBeginComRotulo(tokens, pos, rotulos);
        } else if (operacao.texto == "END") {
            recusarRotulos(rotulos, operacao.texto);
            tratarEnd(tokens, pos);
        } else if (operacao.texto == "PUBLIC") {
            recusarRotulos(rotulos, operacao.texto);
            tratarPublic(tokens, pos);
        } else if (operacao.texto == "EXTERN") {
            tratarExtern(tokens, pos, rotulos);
        } else {
            sintatico("instrucao ou diretiva inexistente '" + operacao.texto + "'");
            definirRotulos(rotulos);
        }
    }

    static bool temTokenInvalido(const std::vector<Token>& tokens, std::size_t pos) {
        return std::any_of(tokens.begin() + static_cast<std::ptrdiff_t>(pos), tokens.end(),
                           [](const Token& t) { return t.tipo == TipoToken::INVALIDO; });
    }

    // Valor numerico: [+|-] inteiro, ou simbolo de EQU ja definido, e nada depois.
    // Devolve false se houve erro (ja reportado).
    bool lerValor(const std::vector<Token>& tokens, std::size_t pos, const std::string& diretiva,
                  long long& valor) {
        if (temTokenInvalido(tokens, pos)) return false;  // ja reportado no lexico
        if (pos >= tokens.size()) {
            sintatico(diretiva + " sem argumento");
            return false;
        }

        long long sinal = 1;
        bool temSinal = false;
        if (tokens[pos].tipo == TipoToken::MAIS || tokens[pos].tipo == TipoToken::MENOS) {
            sinal = tokens[pos].tipo == TipoToken::MENOS ? -1 : 1;
            temSinal = true;
            ++pos;
        }
        if (pos >= tokens.size()) {
            sintatico(diretiva + ": sinal sem numero");
            return false;
        }

        const Token& token = tokens[pos];
        if (token.tipo == TipoToken::NUMERO) {
            valor = sinal * token.valor;
        } else if (token.tipo == TipoToken::IDENTIFICADOR && !temSinal) {
            const auto simbolo = simbolos_.find(token.texto);
            if (simbolo == simbolos_.end() || !simbolo->second.definido ||
                simbolo->second.secao != Secao::EQU) {
                sintatico(diretiva + ": '" + token.texto + "' nao e numero nem simbolo de EQU definido");
                return false;
            }
            valor = simbolo->second.valor;
        } else {
            sintatico(diretiva + ": argumento invalido '" + token.texto + "'");
            return false;
        }

        if (pos + 1 != tokens.size()) {
            sintatico(diretiva + " aceita um unico argumento");
            return false;
        }
        return true;
    }

    // Operandos de instrucao: ROT [+ n] repetido. Devolve false se houve erro.
    bool lerOperandos(const std::vector<Token>& tokens, std::size_t pos,
                      std::vector<Operando>& operandos) {
        if (temTokenInvalido(tokens, pos)) return false;  // ja reportado no lexico
        while (pos < tokens.size()) {
            const Token& nome = tokens[pos];
            if (nome.tipo != TipoToken::IDENTIFICADOR) {
                sintatico("operando deve ser um rotulo, encontrado '" + nome.texto + "'");
                return false;
            }
            ++pos;

            int deslocamento = 0;
            if (pos < tokens.size() && tokens[pos].tipo == TipoToken::MAIS) {
                ++pos;
                if (pos >= tokens.size()) {
                    sintatico("'+' sem numero");
                    return false;
                }
                if (!lerDeslocamento(tokens[pos], deslocamento)) return false;
                ++pos;
            }
            operandos.push_back({nome.texto, deslocamento});
        }
        return true;
    }

    bool lerDeslocamento(const Token& token, int& deslocamento) {
        long long valor = -1;
        if (token.tipo == TipoToken::NUMERO) {
            valor = token.valor;
        } else if (token.tipo == TipoToken::IDENTIFICADOR) {
            const auto simbolo = simbolos_.find(token.texto);
            if (simbolo != simbolos_.end() && simbolo->second.definido &&
                simbolo->second.secao == Secao::EQU)
                valor = simbolo->second.valor;
        }
        if (valor < 0 || valor >= TAMANHO_MEMORIA) {
            sintatico("'+' sem numero valido: '" + token.texto + "'");
            return false;
        }
        deslocamento = static_cast<int>(valor);
        return true;
    }

    // FASE 3: analise semantica

    // Define o rotulo em `secao` com o valor dado.
    void definirRotulo(const Rotulo& rotulo, Secao secao, int valor) {
        Simbolo& simbolo = simbolos_[rotulo.nome];
        if (simbolo.definido) {
            if (simbolo.secao == Secao::EXTERN || secao == Secao::EXTERN) {
                erroNaLinha(TipoErro::SEMANTICO, rotulo.linha,
                            "simbolo EXTERN '" + rotulo.nome + "' tambem definido localmente");
            } else {
                erroNaLinha(TipoErro::SEMANTICO, rotulo.linha, "rotulo '" + rotulo.nome + "' redeclarado");
            }
            return;
        }
        simbolo.definido = true;
        simbolo.secao = secao;
        simbolo.valor = valor;
        simbolo.linhaDefinicao = rotulo.linha;
    }

    // Define os rotulos na posicao corrente. Tambem usada em linhas com erro, para
    // que os usos do rotulo nao virem erros em cascata.
    void definirRotulos(const std::vector<Rotulo>& rotulos) {
        const bool naData = secaoAtual_ == Secao::DATA;
        const int valor = static_cast<int>(naData ? codigoData_.size() : codigoText_.size());
        for (const Rotulo& rotulo : rotulos)
            definirRotulo(rotulo, naData ? Secao::DATA : Secao::TEXT, valor);
    }

    void recusarRotulos(const std::vector<Rotulo>& rotulos, const std::string& diretiva) {
        if (!rotulos.empty()) sintatico(diretiva + " nao aceita rotulo");
        definirRotulos(rotulos);
    }

    void tratarInstrucao(const std::string& nome, const Instrucao& instrucao,
                         const std::vector<Token>& tokens, std::size_t pos,
                         const std::vector<Rotulo>& rotulos) {
        definirRotulos(rotulos);

        std::vector<Operando> operandos;
        bool valido = lerOperandos(tokens, pos, operandos);
        if (valido && static_cast<int>(operandos.size()) != instrucao.operandos) {
            sintatico(nome + " exige " + std::to_string(instrucao.operandos) + " operando(s), recebeu " +
                      std::to_string(operandos.size()));
            valido = false;
        }

        if (secaoAtual_ != Secao::TEXT) {
            semantico("instrucao " + nome + " fora da secao TEXT");
            return;
        }
        if (linhaEnd_ != 0) semantico("instrucao " + nome + " depois de END");

        emitirInstrucao(instrucao, operandos, valido);
    }

    void tratarSecao(const std::vector<Token>& tokens, std::size_t pos) {
        if (temTokenInvalido(tokens, pos)) return;
        if (pos + 1 != tokens.size() ||
            (tokens[pos].texto != "TEXT" && tokens[pos].texto != "DATA")) {
            sintatico("secao deve ser TEXT ou DATA");
            return;
        }
        const bool ehText = tokens[pos].texto == "TEXT";
        bool& jaVista = ehText ? viuText_ : viuData_;
        if (jaVista) semantico("secao " + tokens[pos].texto + " repetida");
        jaVista = true;
        viuSecao_ = true;
        secaoAtual_ = ehText ? Secao::TEXT : Secao::DATA;
    }

    void tratarSpace(const std::vector<Token>& tokens, std::size_t pos,
                     const std::vector<Rotulo>& rotulos) {
        if (rotulos.empty()) sintatico("SPACE exige rotulo");
        definirRotulos(rotulos);

        long long quantidade = 1;
        if (pos < tokens.size()) {
            if (!lerValor(tokens, pos, "SPACE", quantidade)) quantidade = 0;
            else if (quantidade <= 0 || quantidade > TAMANHO_MEMORIA) {
                sintatico("SPACE exige inteiro positivo que caiba na memoria");
                quantidade = 0;
            }
        }

        if (secaoAtual_ != Secao::DATA) {
            semantico("SPACE fora da secao DATA");
            return;
        }

        if (codigoData_.size() + static_cast<std::size_t>(quantidade) <= 2u * TAMANHO_MEMORIA)
            codigoData_.insert(codigoData_.end(), static_cast<std::size_t>(quantidade), 0);
        else
            semantico("programa excede a memoria da maquina");
    }

    void tratarConst(const std::vector<Token>& tokens, std::size_t pos,
                     const std::vector<Rotulo>& rotulos) {
        if (rotulos.empty()) sintatico("CONST exige rotulo");
        definirRotulos(rotulos);

        long long valor = 0;
        if (lerValor(tokens, pos, "CONST", valor) && (valor < -32768 || valor > 65535)) {
            sintatico("CONST fora do intervalo de 16 bits");
            valor = 0;
        }
        if (secaoAtual_ != Secao::DATA) {
            semantico("CONST fora da secao DATA");
            return;
        }
        codigoData_.push_back(static_cast<int>(valor));
    }

    void tratarEqu(const std::vector<Token>& tokens, std::size_t pos,
                   const std::vector<Rotulo>& rotulos) {
        if (rotulos.empty()) sintatico("EQU exige rotulo");
        if (viuSecao_) semantico("EQU depois de uma secao");

        long long valor = 0;
        if (lerValor(tokens, pos, "EQU", valor) && (valor < INT_MIN || valor > INT_MAX)) {
            sintatico("EQU com valor grande demais");
            valor = 0;
        }
        for (const Rotulo& rotulo : rotulos)
            definirRotulo(rotulo, Secao::EQU, static_cast<int>(valor));
    }

    void tratarIf(const std::vector<Token>& tokens, std::size_t pos) {
        if (secaoAtual_ != Secao::TEXT) semantico("IF fora da secao TEXT");
        long long valor = 1;
        // Com argumento invalido, a linha seguinte e analisada normalmente.
        if (lerValor(tokens, pos, "IF", valor) && valor == 0) descartarProxima_ = true;
    }

    void tratarBeginComRotulo(const std::vector<Token>& tokens, std::size_t pos,
                              const std::vector<Rotulo>& rotulos) {
        if (pos != tokens.size() && !temTokenInvalido(tokens, pos)) sintatico("BEGIN nao aceita operandos");
        if (rotulos.empty()) {
            sintatico("BEGIN exige rotulo com o nome do modulo");
            return;
        }
        // Com dois rotulos, o erro ja saiu; o ultimo e tratado como nome do modulo.
        std::vector<Rotulo> outros(rotulos.begin(), rotulos.end() - 1);
        definirRotulos(outros);
        tratarBegin(rotulos.back().nome);
    }

    void tratarBegin(const std::string& nome) {
        // BEGIN: NOME pode vir depois de um rotulo sozinho: dois rotulos na mesma linha.
        if (!rotulosPendentes_.empty()) {
            sintatico("dois rotulos para a mesma linha");
            definirRotulos(rotulosPendentes_);
            rotulosPendentes_.clear();
        }
        if (linhaBegin_ != 0) {
            semantico("BEGIN repetido");
        } else {
            if (!codigoText_.empty() || !codigoData_.empty())
                semantico("BEGIN deve vir antes de qualquer codigo ou dado");
            linhaBegin_ = linhaAtual_;
            resultado_.ehModulo = true;
            resultado_.nomeModulo = nome;
        }
        definirRotulo({nome, linhaAtual_}, Secao::TEXT, 0);
    }

    void tratarEnd(const std::vector<Token>& tokens, std::size_t pos) {
        if (pos != tokens.size() && !temTokenInvalido(tokens, pos)) sintatico("END nao aceita operandos");
        if (linhaBegin_ == 0) semantico("END sem BEGIN");
        if (linhaEnd_ != 0) semantico("END repetido");
        else linhaEnd_ = linhaAtual_;
    }

    // Regras comuns de PUBLIC e EXTERN: entre BEGIN e END, fora da DATA.
    void verificarContextoDeLigacao(const std::string& diretiva) {
        if (linhaBegin_ == 0) semantico(diretiva + " fora de modulo (sem BEGIN antes)");
        if (linhaEnd_ != 0) semantico(diretiva + " depois de END");
        if (secaoAtual_ == Secao::DATA) semantico(diretiva + " dentro da secao DATA");
    }

    void tratarPublic(const std::vector<Token>& tokens, std::size_t pos) {
        verificarContextoDeLigacao("PUBLIC");
        if (temTokenInvalido(tokens, pos)) return;
        if (pos + 1 != tokens.size() || tokens[pos].tipo != TipoToken::IDENTIFICADOR) {
            sintatico("PUBLIC exige exatamente um rotulo");
            return;
        }
        exportacoes_.push_back({tokens[pos].texto, linhaAtual_});
    }

    void tratarExtern(const std::vector<Token>& tokens, std::size_t pos,
                      const std::vector<Rotulo>& rotulos) {
        verificarContextoDeLigacao("EXTERN");
        if (pos != tokens.size() && !temTokenInvalido(tokens, pos)) sintatico("EXTERN nao aceita operandos");
        if (rotulos.empty()) {
            sintatico("EXTERN exige rotulo");
            return;
        }
        for (const Rotulo& rotulo : rotulos) {
            definirRotulo(rotulo, Secao::EXTERN, 0);
            Simbolo& simbolo = simbolos_[rotulo.nome];
            if (simbolo.secao != Secao::EXTERN) continue;  // conflito ja reportado
            // Usos anteriores a declaracao viram entradas da Tabela de Uso.
            for (const Pendencia& pendencia : simbolo.pendencias)
                resultado_.usos.push_back({rotulo.nome, pendencia.posicao});
            simbolo.pendencias.clear();
        }
    }

    // FASE 4: sintese

    void emitirInstrucao(const Instrucao& instrucao, const std::vector<Operando>& operandos,
                         bool operandosValidos) {
        codigoText_.push_back(instrucao.opcode);
        relocacaoText_.push_back(0);
        for (int i = 0; i < instrucao.operandos; ++i) {
            const int posicao = static_cast<int>(codigoText_.size());
            codigoText_.push_back(0);
            relocacaoText_.push_back(1);
            // Linha com erro so ocupa espaco; seus simbolos nao contam como uso.
            if (operandosValidos) usarOperando(operandos[static_cast<std::size_t>(i)], posicao);
        }
    }

    void usarOperando(const Operando& operando, int posicao) {
        int& campo = codigoText_[static_cast<std::size_t>(posicao)];
        campo = operando.deslocamento;  // o deslocamento fica guardado no proprio codigo

        Simbolo& simbolo = simbolos_[operando.rotulo];
        if (simbolo.definido && simbolo.secao == Secao::EQU) {
            semantico("simbolo de EQU '" + operando.rotulo + "' usado como operando");
        } else if (simbolo.definido && simbolo.secao == Secao::EXTERN) {
            resultado_.usos.push_back({operando.rotulo, posicao});
        } else if (simbolo.definido && simbolo.secao == Secao::TEXT) {
            campo += simbolo.valor;
        } else {
            // Ainda nao definido, ou da DATA: o endereco final so se conhece no fim.
            simbolo.pendencias.push_back({posicao, linhaAtual_});
        }
    }

    // Fim da leitura: daqui em diante so se percorrem as tabelas.
    void finalizar() {
        if (!rotulosPendentes_.empty()) {
            for (const Rotulo& rotulo : rotulosPendentes_)
                erroNaLinha(TipoErro::SINTATICO, rotulo.linha,
                            "rotulo '" + rotulo.nome + "' sem linha seguinte");
            definirRotulos(rotulosPendentes_);
        }
        if (!viuText_) erroNaLinha(TipoErro::SEMANTICO, 0, "secao TEXT ausente");
        if (linhaBegin_ != 0 && linhaEnd_ == 0)
            erroNaLinha(TipoErro::SEMANTICO, linhaBegin_, "BEGIN sem END");

        // Resolve as pendencias com o endereco final de cada simbolo.
        for (auto& entrada : simbolos_) {
            const Simbolo& simbolo = entrada.second;
            for (const Pendencia& pendencia : simbolo.pendencias) {
                if (!simbolo.definido) {
                    erroNaLinha(TipoErro::SEMANTICO, pendencia.linha,
                                "rotulo '" + entrada.first + "' nao declarado");
                } else if (simbolo.secao == Secao::EQU) {
                    erroNaLinha(TipoErro::SEMANTICO, pendencia.linha,
                                "simbolo de EQU '" + entrada.first + "' usado como operando");
                } else {
                    codigoText_[static_cast<std::size_t>(pendencia.posicao)] += enderecoFinal(simbolo);
                }
            }
        }

        // Tabela de Definicoes: copia o endereco final de cada PUBLIC.
        for (const Exportacao& exportacao : exportacoes_) {
            const auto simbolo = simbolos_.find(exportacao.nome);
            if (simbolo == simbolos_.end() || !simbolo->second.definido) {
                erroNaLinha(TipoErro::SEMANTICO, exportacao.linha,
                            "PUBLIC de '" + exportacao.nome + "', nao definido no modulo");
            } else if (simbolo->second.secao == Secao::EXTERN || simbolo->second.secao == Secao::EQU) {
                erroNaLinha(TipoErro::SEMANTICO, exportacao.linha,
                            "PUBLIC de '" + exportacao.nome + "', que nao e endereco deste modulo");
            } else {
                resultado_.definicoes[exportacao.nome] = enderecoFinal(simbolo->second);
            }
        }

        if (codigoText_.size() + codigoData_.size() > static_cast<std::size_t>(TAMANHO_MEMORIA))
            erroNaLinha(TipoErro::SEMANTICO, 0, "programa excede a memoria da maquina");

        // Saida: codigo_text concatenado com codigo_data.
        resultado_.codigo = codigoText_;
        resultado_.codigo.insert(resultado_.codigo.end(), codigoData_.begin(), codigoData_.end());
        resultado_.relocacao = relocacaoText_;
        resultado_.relocacao.insert(resultado_.relocacao.end(), codigoData_.size(), 0);
        std::stable_sort(resultado_.usos.begin(), resultado_.usos.end(),
                         [](const UsoExterno& a, const UsoExterno& b) { return a.posicao < b.posicao; });

        ordenarErros();
    }

    int enderecoFinal(const Simbolo& simbolo) const {
        return simbolo.secao == Secao::DATA ? static_cast<int>(codigoText_.size()) + simbolo.valor
                                            : simbolo.valor;
    }

    // Ordem do arquivo; erros do arquivo inteiro (linha 0) por ultimo. Dois usos
    // do mesmo simbolo na mesma linha (COPY N N) geram um unico erro.
    void ordenarErros() {
        std::vector<Erro>& erros = resultado_.erros;
        const auto chave = [](const Erro& e) { return e.linha == 0 ? INT_MAX : e.linha; };
        std::stable_sort(erros.begin(), erros.end(),
                         [&](const Erro& a, const Erro& b) { return chave(a) < chave(b); });
        const auto iguais = [](const Erro& a, const Erro& b) {
            return std::tie(a.tipo, a.linha, a.descricao) == std::tie(b.tipo, b.linha, b.descricao);
        };
        erros.erase(std::unique(erros.begin(), erros.end(), iguais), erros.end());
    }
};

const char* nomeDoTipo(TipoErro tipo) {
    switch (tipo) {
        case TipoErro::LEXICO: return "LEXICO";
        case TipoErro::SINTATICO: return "SINTATICO";
        case TipoErro::SEMANTICO: return "SEMANTICO";
    }
    return "";
}

}

Resultado montar(std::istream& fonte) {
    Montador montador;
    return montador.executar(fonte);
}

std::string formatarNumero(int valor) {
    return std::to_string(valor);
}

std::string serializarCodigo(const std::vector<int>& codigo) {
    std::string saida;
    for (std::size_t i = 0; i < codigo.size(); ++i) {
        if (i != 0) saida += ' ';
        saida += formatarNumero(codigo[i]);
    }
    return saida;
}

std::string serializarObjeto(const Resultado& resultado) {
    if (!resultado.ehModulo) return serializarCodigo(resultado.codigo) + "\n";

    std::string saida;
    saida += "H: " + resultado.nomeModulo + "\n";
    saida += "H: " + std::to_string(resultado.codigo.size()) + "\n";
    saida += "R: " + serializarCodigo(resultado.relocacao) + "\n";
    for (const auto& definicao : resultado.definicoes)
        saida += "D: " + definicao.first + " " + std::to_string(definicao.second) + "\n";
    for (const UsoExterno& uso : resultado.usos)
        saida += "U: " + uso.simbolo + " " + std::to_string(uso.posicao) + "\n";
    saida += "T: " + serializarCodigo(resultado.codigo) + "\n";
    return saida;
}

std::string formatarErro(const Erro& erro) {
    return std::string("[") + nomeDoTipo(erro.tipo) + "] linha " + std::to_string(erro.linha) +
           ": \"" + erro.textoLinha + "\" -> " + erro.descricao;
}

std::string trocarExtensao(const std::string& caminho, const std::string& extensao) {
    const std::size_t barra = caminho.find_last_of("/\\");
    const std::size_t inicioNome = barra == std::string::npos ? 0 : barra + 1;
    const std::size_t ponto = caminho.rfind('.');
    if (ponto == std::string::npos || ponto <= inicioNome) return caminho + extensao;
    return caminho.substr(0, ponto) + extensao;
}

bool temExtensao(const std::string& caminho, const std::string& extensao) {
    if (caminho.size() < extensao.size()) return false;
    const std::string fim = caminho.substr(caminho.size() - extensao.size());
    return std::equal(fim.begin(), fim.end(), extensao.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

bool gravarArquivo(const std::string& caminho, const std::string& conteudo) {
    std::ofstream arquivo(caminho, std::ios::binary);
    arquivo << conteudo;
    return static_cast<bool>(arquivo);
}

}
