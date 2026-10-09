/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "nsscomp.h"

#include <cctype>
#include <cstring>
#include <functional>
#include <set>

#include "reone/script/format/ncsreader.h"
#include "reone/script/format/ncswriter.h"
#include "reone/script/instrutil.h"
#include "reone/script/routine.h"
#include "reone/script/routines.h"
#include "reone/script/types.h"
#include "reone/script/variable.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone::script;

namespace reone {

namespace testsupport {

namespace {

// ---------------------------------------------------------------------------
// Types

enum class Ty {
    Void,
    Int,
    Float,
    String,
    Object,
    Vector,
    Location,
    Effect,
    Event,
    Talent,
    Action
};

int slotsOf(Ty type) {
    switch (type) {
    case Ty::Void:
    case Ty::Action:
        return 0;
    case Ty::Vector:
        return 3;
    default:
        return 1;
    }
}

const char *nameOf(Ty type) {
    switch (type) {
    case Ty::Void:
        return "void";
    case Ty::Int:
        return "int";
    case Ty::Float:
        return "float";
    case Ty::String:
        return "string";
    case Ty::Object:
        return "object";
    case Ty::Vector:
        return "vector";
    case Ty::Location:
        return "location";
    case Ty::Effect:
        return "effect";
    case Ty::Event:
        return "event";
    case Ty::Talent:
        return "talent";
    case Ty::Action:
        return "action";
    }
    return "?";
}

bool typeFromName(const std::string &name, Ty &type) {
    static const std::map<std::string, Ty> kTypes {
        {"void", Ty::Void}, {"int", Ty::Int}, {"float", Ty::Float}, {"string", Ty::String},
        {"object", Ty::Object}, {"vector", Ty::Vector}, {"location", Ty::Location},
        {"effect", Ty::Effect}, {"event", Ty::Event}, {"talent", Ty::Talent}};
    auto found = kTypes.find(name);
    if (found == kTypes.end()) {
        return false;
    }
    type = found->second;
    return true;
}

Ty fromVariableType(VariableType type) {
    switch (type) {
    case VariableType::Int:
        return Ty::Int;
    case VariableType::Float:
        return Ty::Float;
    case VariableType::String:
        return Ty::String;
    case VariableType::Vector:
        return Ty::Vector;
    case VariableType::Object:
        return Ty::Object;
    case VariableType::Effect:
        return Ty::Effect;
    case VariableType::Event:
        return Ty::Event;
    case VariableType::Location:
        return Ty::Location;
    case VariableType::Talent:
        return Ty::Talent;
    case VariableType::Action:
        return Ty::Action;
    default:
        return Ty::Void;
    }
}

// ---------------------------------------------------------------------------
// Lexer

struct Token {
    enum Kind {
        End,
        Ident,
        Int,
        Float,
        String,
        Punct
    };
    Kind kind {End};
    std::string text;
    int line {1};
    int intValue {0};
    float floatValue {0.0f};
};

std::vector<Token> lex(const std::string &src) {
    static const char *kPuncts[] = {">>>=", ">>>", "<<=", ">>=", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "++", "--",
                                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="};
    std::vector<Token> tokens;
    int line = 1;
    size_t i = 0;
    auto at = [&](size_t k) { return k < src.size() ? src[k] : '\0'; };
    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '/' && at(i + 1) == '/') {
            while (i < src.size() && src[i] != '\n') {
                ++i;
            }
        } else if (c == '/' && at(i + 1) == '*') {
            i += 2;
            while (i < src.size() && !(src[i] == '*' && at(i + 1) == '/')) {
                if (src[i] == '\n') {
                    ++line;
                }
                ++i;
            }
            if (i >= src.size()) {
                throw NssError(line, "unterminated comment");
            }
            i += 2;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i;
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                ++i;
            }
            Token t;
            t.kind = Token::Ident;
            t.text = src.substr(start, i - start);
            t.line = line;
            tokens.push_back(t);
        } else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(at(i + 1))))) {
            size_t start = i;
            Token t;
            t.line = line;
            if (c == '0' && (at(i + 1) == 'x' || at(i + 1) == 'X')) {
                i += 2;
                while (std::isxdigit(static_cast<unsigned char>(at(i)))) {
                    ++i;
                }
                t.kind = Token::Int;
                t.intValue = static_cast<int>(std::stoul(src.substr(start + 2, i - start - 2), nullptr, 16));
            } else {
                bool isFloat = false;
                while (std::isdigit(static_cast<unsigned char>(at(i)))) {
                    ++i;
                }
                if (at(i) == '.') {
                    isFloat = true;
                    ++i;
                    while (std::isdigit(static_cast<unsigned char>(at(i)))) {
                        ++i;
                    }
                }
                std::string number = src.substr(start, i - start);
                if (at(i) == 'f' || at(i) == 'F') {
                    isFloat = true;
                    ++i;
                }
                if (isFloat) {
                    t.kind = Token::Float;
                    t.floatValue = std::stof(number);
                } else {
                    t.kind = Token::Int;
                    t.intValue = static_cast<int>(std::stoll(number));
                }
            }
            t.text = src.substr(start, i - start);
            tokens.push_back(t);
        } else if (c == '"') {
            Token t;
            t.kind = Token::String;
            t.line = line;
            ++i;
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < src.size()) {
                    ++i;
                    switch (src[i]) {
                    case 'n':
                        t.text += '\n';
                        break;
                    case 't':
                        t.text += '\t';
                        break;
                    default:
                        t.text += src[i];
                        break;
                    }
                } else {
                    if (src[i] == '\n') {
                        ++line;
                    }
                    t.text += src[i];
                }
                ++i;
            }
            if (i >= src.size()) {
                throw NssError(line, "unterminated string");
            }
            ++i;
            tokens.push_back(t);
        } else {
            Token t;
            t.kind = Token::Punct;
            t.line = line;
            bool matched = false;
            for (const char *p : kPuncts) {
                size_t n = std::strlen(p);
                if (src.compare(i, n, p) == 0) {
                    t.text = p;
                    i += n;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (!std::strchr("(){}[];,.?:=+-*/%<>!~&|^", c)) {
                    throw NssError(line, std::string("unexpected character '") + c + "'");
                }
                t.text = std::string(1, c);
                ++i;
            }
            tokens.push_back(t);
        }
    }
    Token end;
    end.line = line;
    tokens.push_back(end);
    return tokens;
}

// ---------------------------------------------------------------------------
// Syntax tree

struct Expr;
struct Stmt;
using ExprP = std::shared_ptr<Expr>;
using StmtP = std::shared_ptr<Stmt>;

struct Expr {
    enum Kind {
        IntLit,
        FloatLit,
        StrLit,
        VecLit,
        Ident,
        Unary,
        Binary,
        Assign,
        Call,
        Member,
        Ternary,
        IncDec
    };
    Kind kind {IntLit};
    int line {0};
    int intValue {0};
    float floatValue {0.0f};
    std::string text; // identifier, operator or member name
    bool prefix {false};
    std::vector<ExprP> kids;
};

struct Stmt {
    enum Kind {
        Block,
        Decl,
        ExprStmt,
        If,
        While,
        DoWhile,
        For,
        Return,
        Break,
        Continue,
        Empty
    };
    Kind kind {Empty};
    int line {0};
    std::vector<StmtP> body; // Block
    Ty declType {Ty::Void};
    std::vector<std::pair<std::string, ExprP>> decls;
    ExprP expr;  // expression, condition or return value
    ExprP incr;  // For
    StmtP init;  // For
    StmtP first; // then / loop body
    StmtP second; // else
};

struct Param {
    Ty type;
    std::string name;
};

struct FuncDecl {
    Ty ret {Ty::Void};
    std::string name;
    std::vector<Param> params;
    StmtP body; // null for a prototype
    int line {0};
};

struct GlobalDecl {
    Ty type {Ty::Void};
    std::string name;
    ExprP init;
    int line {0};
};

struct TopItem {
    std::shared_ptr<FuncDecl> func;
    std::shared_ptr<GlobalDecl> global;
};

// ---------------------------------------------------------------------------
// Parser

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) :
        _t(std::move(tokens)) {
    }

    std::vector<TopItem> parseProgram() {
        std::vector<TopItem> items;
        while (peek().kind != Token::End) {
            parseTopLevel(items);
        }
        return items;
    }

private:
    std::vector<Token> _t;
    size_t _p {0};

    const Token &peek(size_t ahead = 0) const { return _t[std::min(_p + ahead, _t.size() - 1)]; }
    Token next() {
        Token t = peek();
        if (_p < _t.size() - 1) {
            ++_p;
        }
        return t;
    }
    bool isPunct(const char *text, size_t ahead = 0) const {
        return peek(ahead).kind == Token::Punct && peek(ahead).text == text;
    }
    bool acceptPunct(const char *text) {
        if (isPunct(text)) {
            next();
            return true;
        }
        return false;
    }
    void expectPunct(const char *text) {
        if (!acceptPunct(text)) {
            throw NssError(peek().line, std::string("expected '") + text + "' but found '" + peek().text + "'");
        }
    }
    bool isType(size_t ahead = 0) const {
        Ty ignored;
        return peek(ahead).kind == Token::Ident && typeFromName(peek(ahead).text, ignored);
    }
    Ty parseType() {
        Ty type;
        if (peek().kind != Token::Ident || !typeFromName(peek().text, type)) {
            throw NssError(peek().line, "expected a type but found '" + peek().text + "'");
        }
        next();
        return type;
    }
    std::string parseIdent() {
        if (peek().kind != Token::Ident) {
            throw NssError(peek().line, "expected a name but found '" + peek().text + "'");
        }
        return next().text;
    }

    void parseTopLevel(std::vector<TopItem> &items) {
        bool isConst = false;
        if (peek().kind == Token::Ident && peek().text == "const") {
            next();
            isConst = true;
        }
        (void)isConst;
        int line = peek().line;
        Ty type = parseType();
        std::string name = parseIdent();
        if (isPunct("(")) {
            auto func = std::make_shared<FuncDecl>();
            func->ret = type;
            func->name = name;
            func->line = line;
            next();
            if (!isPunct(")")) {
                do {
                    Param param;
                    param.type = parseType();
                    if (param.type == Ty::Void) {
                        throw NssError(peek().line, "a parameter cannot be void");
                    }
                    param.name = parseIdent();
                    if (isPunct("=")) {
                        throw NssError(peek().line, "default argument values are not supported");
                    }
                    func->params.push_back(param);
                } while (acceptPunct(","));
            }
            expectPunct(")");
            if (acceptPunct(";")) {
                items.push_back({func, nullptr});
                return;
            }
            func->body = parseBlock();
            items.push_back({func, nullptr});
            return;
        }
        // Globals, possibly several declarators.
        for (;;) {
            auto global = std::make_shared<GlobalDecl>();
            global->type = type;
            global->name = name;
            global->line = line;
            if (type == Ty::Void) {
                throw NssError(line, "a variable cannot be void");
            }
            if (acceptPunct("=")) {
                global->init = parseExpr();
            }
            items.push_back({nullptr, global});
            if (!acceptPunct(",")) {
                break;
            }
            name = parseIdent();
        }
        expectPunct(";");
    }

    StmtP parseBlock() {
        auto block = std::make_shared<Stmt>();
        block->kind = Stmt::Block;
        block->line = peek().line;
        expectPunct("{");
        while (!isPunct("}")) {
            if (peek().kind == Token::End) {
                throw NssError(peek().line, "unterminated block");
            }
            block->body.push_back(parseStmt());
        }
        expectPunct("}");
        return block;
    }

    StmtP parseDecl() {
        auto stmt = std::make_shared<Stmt>();
        stmt->kind = Stmt::Decl;
        stmt->line = peek().line;
        if (peek().kind == Token::Ident && peek().text == "const") {
            next();
        }
        stmt->declType = parseType();
        if (stmt->declType == Ty::Void) {
            throw NssError(stmt->line, "a variable cannot be void");
        }
        do {
            std::string name = parseIdent();
            ExprP init;
            if (acceptPunct("=")) {
                init = parseExpr();
            }
            stmt->decls.emplace_back(name, init);
        } while (acceptPunct(","));
        return stmt;
    }

    StmtP parseStmt() {
        const Token &t = peek();
        auto stmt = std::make_shared<Stmt>();
        stmt->line = t.line;
        if (isPunct("{")) {
            return parseBlock();
        }
        if (acceptPunct(";")) {
            stmt->kind = Stmt::Empty;
            return stmt;
        }
        if (isType() || (t.kind == Token::Ident && t.text == "const")) {
            auto decl = parseDecl();
            expectPunct(";");
            return decl;
        }
        if (t.kind == Token::Ident) {
            if (t.text == "if") {
                next();
                stmt->kind = Stmt::If;
                expectPunct("(");
                stmt->expr = parseExpr();
                expectPunct(")");
                stmt->first = parseStmt();
                if (peek().kind == Token::Ident && peek().text == "else") {
                    next();
                    stmt->second = parseStmt();
                }
                return stmt;
            }
            if (t.text == "while") {
                next();
                stmt->kind = Stmt::While;
                expectPunct("(");
                stmt->expr = parseExpr();
                expectPunct(")");
                stmt->first = parseStmt();
                return stmt;
            }
            if (t.text == "do") {
                next();
                stmt->kind = Stmt::DoWhile;
                stmt->first = parseStmt();
                if (peek().kind != Token::Ident || peek().text != "while") {
                    throw NssError(peek().line, "expected 'while'");
                }
                next();
                expectPunct("(");
                stmt->expr = parseExpr();
                expectPunct(")");
                expectPunct(";");
                return stmt;
            }
            if (t.text == "for") {
                next();
                stmt->kind = Stmt::For;
                expectPunct("(");
                if (!acceptPunct(";")) {
                    if (isType()) {
                        stmt->init = parseDecl();
                    } else {
                        auto init = std::make_shared<Stmt>();
                        init->kind = Stmt::ExprStmt;
                        init->line = peek().line;
                        init->expr = parseExpr();
                        stmt->init = init;
                    }
                    expectPunct(";");
                }
                if (!isPunct(";")) {
                    stmt->expr = parseExpr();
                }
                expectPunct(";");
                if (!isPunct(")")) {
                    stmt->incr = parseExpr();
                }
                expectPunct(")");
                stmt->first = parseStmt();
                return stmt;
            }
            if (t.text == "return") {
                next();
                stmt->kind = Stmt::Return;
                if (!isPunct(";")) {
                    stmt->expr = parseExpr();
                }
                expectPunct(";");
                return stmt;
            }
            if (t.text == "break" || t.text == "continue") {
                stmt->kind = t.text == "break" ? Stmt::Break : Stmt::Continue;
                next();
                expectPunct(";");
                return stmt;
            }
            if (t.text == "switch" || t.text == "case" || t.text == "default" || t.text == "struct") {
                throw NssError(t.line, "'" + t.text + "' is not supported by the test compiler");
            }
        }
        stmt->kind = Stmt::ExprStmt;
        stmt->expr = parseExpr();
        expectPunct(";");
        return stmt;
    }

    ExprP make(Expr::Kind kind, int line) {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        e->line = line;
        return e;
    }

    ExprP parseExpr() { return parseAssign(); }

    ExprP parseAssign() {
        auto left = parseTernary();
        static const char *kAssign[] = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", ">>>="};
        for (const char *op : kAssign) {
            if (isPunct(op)) {
                int line = peek().line;
                next();
                auto e = make(Expr::Assign, line);
                e->text = op;
                e->kids = {left, parseAssign()};
                return e;
            }
        }
        return left;
    }

    ExprP parseTernary() {
        auto cond = parseBinary(0);
        if (isPunct("?")) {
            int line = peek().line;
            next();
            auto e = make(Expr::Ternary, line);
            auto a = parseAssign();
            expectPunct(":");
            auto b = parseAssign();
            e->kids = {cond, a, b};
            return e;
        }
        return cond;
    }

    static int precedence(const std::string &op) {
        static const std::map<std::string, int> kPrec {
            {"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5}, {"==", 6}, {"!=", 6},
            {"<", 7}, {"<=", 7}, {">", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {">>>", 8},
            {"+", 9}, {"-", 9}, {"*", 10}, {"/", 10}, {"%", 10}};
        auto found = kPrec.find(op);
        return found == kPrec.end() ? -1 : found->second;
    }

    ExprP parseBinary(int minPrec) {
        auto left = parseUnary();
        for (;;) {
            if (peek().kind != Token::Punct) {
                break;
            }
            int prec = precedence(peek().text);
            if (prec < 0 || prec < minPrec || prec == 0) {
                break;
            }
            Token op = next();
            auto right = parseBinary(prec + 1);
            auto e = make(Expr::Binary, op.line);
            e->text = op.text;
            e->kids = {left, right};
            left = e;
        }
        return left;
    }

    ExprP parseUnary() {
        const Token &t = peek();
        if (t.kind == Token::Punct) {
            if (t.text == "-" || t.text == "!" || t.text == "~") {
                Token op = next();
                // A minus on a literal folds into the literal.
                if (op.text == "-" && peek().kind == Token::Int && !isPunct(".", 1)) {
                    auto e = make(Expr::IntLit, op.line);
                    e->intValue = -next().intValue;
                    return parsePostfix(e);
                }
                if (op.text == "-" && peek().kind == Token::Float) {
                    auto e = make(Expr::FloatLit, op.line);
                    e->floatValue = -next().floatValue;
                    return parsePostfix(e);
                }
                auto e = make(Expr::Unary, op.line);
                e->text = op.text;
                e->kids = {parseUnary()};
                return e;
            }
            if (t.text == "++" || t.text == "--") {
                Token op = next();
                auto e = make(Expr::IncDec, op.line);
                e->text = op.text;
                e->prefix = true;
                e->kids = {parseUnary()};
                return e;
            }
        }
        return parsePostfix(parsePrimary());
    }

    ExprP parsePostfix(ExprP e) {
        for (;;) {
            if (isPunct(".")) {
                int line = peek().line;
                next();
                auto member = make(Expr::Member, line);
                member->text = parseIdent();
                member->kids = {e};
                e = member;
            } else if (isPunct("++") || isPunct("--")) {
                Token op = next();
                auto inc = make(Expr::IncDec, op.line);
                inc->text = op.text;
                inc->prefix = false;
                inc->kids = {e};
                e = inc;
            } else {
                break;
            }
        }
        return e;
    }

    ExprP parsePrimary() {
        Token t = next();
        switch (t.kind) {
        case Token::Int: {
            auto e = make(Expr::IntLit, t.line);
            e->intValue = t.intValue;
            return e;
        }
        case Token::Float: {
            auto e = make(Expr::FloatLit, t.line);
            e->floatValue = t.floatValue;
            return e;
        }
        case Token::String: {
            auto e = make(Expr::StrLit, t.line);
            e->text = t.text;
            return e;
        }
        case Token::Ident: {
            if (isPunct("(")) {
                next();
                auto call = make(Expr::Call, t.line);
                call->text = t.text;
                if (!isPunct(")")) {
                    do {
                        call->kids.push_back(parseAssign());
                    } while (acceptPunct(","));
                }
                expectPunct(")");
                return call;
            }
            auto e = make(Expr::Ident, t.line);
            e->text = t.text;
            return e;
        }
        case Token::Punct:
            if (t.text == "(") {
                auto e = parseExpr();
                expectPunct(")");
                return e;
            }
            if (t.text == "[") {
                auto e = make(Expr::VecLit, t.line);
                e->kids.push_back(parseAssign());
                expectPunct(",");
                e->kids.push_back(parseAssign());
                expectPunct(",");
                e->kids.push_back(parseAssign());
                expectPunct("]");
                return e;
            }
            break;
        default:
            break;
        }
        throw NssError(t.line, "unexpected '" + t.text + "'");
    }
};

// ---------------------------------------------------------------------------
// Code generation

struct Em {
    Instruction ins;
    int jumpLabel {-1};
    int globalSlot {-1};        // CPTOPBP / CPDOWNBP / INCIBP: fixed up once the number of globals is known
    bool storeStateGlobals {false}; // STORE_STATE: size is the globals' bytes
    bool popGlobals {false};    // MOVSP that removes the globals and the saved base pointer
};

struct Buf {
    std::vector<Em> code;
    std::vector<std::pair<size_t, int>> binds; // instruction index -> label bound there
};

struct VarInfo {
    Ty type {Ty::Int};
    int slot {0};
    bool global {false};
};

struct FuncInfo {
    Ty ret {Ty::Void};
    std::vector<Param> params;
    int label {-1};
    bool defined {false};
};

class Gen {
public:
    Gen(IRoutines &routines, const NssOptions &options) :
        _routines(routines),
        _options(options) {
    }

    std::shared_ptr<ScriptProgram> run(const std::string &name, const std::vector<TopItem> &items) {
        // Pass 1: functions and globals, in order. Bodies and initialisers are emitted as they come.
        for (const auto &item : items) {
            if (item.func) {
                declareFunction(*item.func);
            }
        }
        _cur = &_globalsBuf;
        for (const auto &item : items) {
            if (item.global) {
                emitGlobal(*item.global);
            }
        }
        _cur = &_funcBuf;
        for (const auto &item : items) {
            if (item.func && item.func->body) {
                emitFunction(*item.func);
            }
        }
        return assemble(name);
    }

private:
    IRoutines &_routines;
    const NssOptions &_options;

    Buf _startBuf;
    Buf _globalsBuf;
    Buf _funcBuf;
    Buf *_cur {nullptr};

    int _labelCount {0};
    std::map<std::string, FuncInfo> _funcs;
    std::map<std::string, VarInfo> _globals;
    int _globalSlots {0};
    bool _inGlobalsInit {false};

    // Per function
    std::vector<std::map<std::string, VarInfo>> _scopes;
    int _depth {0};
    int _paramSlots {0};
    Ty _retType {Ty::Void};
    bool _inFunction {false};
    struct Loop {
        int breakLabel;
        int continueLabel;
        int depth;
    };
    std::vector<Loop> _loops;

    // Emission helpers ----------------------------------------------------

    int newLabel() { return _labelCount++; }
    void bind(int label) { _cur->binds.emplace_back(_cur->code.size(), label); }
    Em &emit(Instruction ins) {
        _cur->code.push_back(Em {std::move(ins)});
        return _cur->code.back();
    }
    void emitJump(Instruction ins, int label) { emit(std::move(ins)).jumpLabel = label; }

    void rsadd(Ty type) {
        switch (type) {
        case Ty::Int:
            emit(Instruction(InstructionType::RSADDI));
            break;
        case Ty::Float:
            emit(Instruction(InstructionType::RSADDF));
            break;
        case Ty::String:
            emit(Instruction(InstructionType::RSADDS));
            break;
        case Ty::Object:
            emit(Instruction(InstructionType::RSADDO));
            break;
        case Ty::Effect:
            emit(Instruction(InstructionType::RSADDEFF));
            break;
        case Ty::Event:
            emit(Instruction(InstructionType::RSADDEVT));
            break;
        case Ty::Location:
            emit(Instruction(InstructionType::RSADDLOC));
            break;
        case Ty::Talent:
            emit(Instruction(InstructionType::RSADDTAL));
            break;
        case Ty::Vector:
            for (int i = 0; i < 3; ++i) {
                emit(Instruction(InstructionType::RSADDF));
            }
            break;
        default:
            break;
        }
        _depth += slotsOf(type);
    }

    void pop(int slots) {
        if (slots > 0) {
            emit(Instruction::newMOVSP(-4 * slots));
            _depth -= slots;
        }
    }

    static std::string fail(Ty from, Ty to) {
        return std::string("cannot use ") + nameOf(from) + " as " + nameOf(to);
    }

    // Scopes ---------------------------------------------------------------

    const VarInfo *findVar(const std::string &name) const {
        for (auto it = _scopes.rbegin(); it != _scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        auto found = _globals.find(name);
        return found == _globals.end() ? nullptr : &found->second;
    }

    void pushScope() { _scopes.emplace_back(); }
    void popScope(int depthAtStart) {
        pop(_depth - depthAtStart);
        _scopes.pop_back();
    }

    // Variable access ---------------------------------------------------------

    // Copies the variable (or a component of it) to the top of the stack.
    void loadVar(const VarInfo &var, int slotOffset, int slots) {
        if (var.global && !_inGlobalsInit) {
            Em &e = emit(Instruction::newCPTOPBP(0, static_cast<uint16_t>(slots * 4)));
            e.globalSlot = var.slot + slotOffset;
        } else {
            emit(Instruction::newCPTOPSP((var.slot + slotOffset - _depth) * 4, static_cast<uint16_t>(slots * 4)));
        }
        _depth += slots;
    }

    // Copies the top of the stack into the variable (or component); the value stays.
    void storeVar(const VarInfo &var, int slotOffset, int slots) {
        if (var.global && !_inGlobalsInit) {
            Em &e = emit(Instruction::newCPDOWNBP(0, static_cast<uint16_t>(slots * 4)));
            e.globalSlot = var.slot + slotOffset;
        } else {
            emit(Instruction::newCPDOWNSP((var.slot + slotOffset - _depth) * 4, static_cast<uint16_t>(slots * 4)));
        }
    }

    // Declarations ---------------------------------------------------------

    void declareFunction(const FuncDecl &decl) {
        auto found = _funcs.find(decl.name);
        if (found == _funcs.end()) {
            FuncInfo info;
            info.ret = decl.ret;
            info.params = decl.params;
            info.label = newLabel();
            _funcs[decl.name] = info;
            found = _funcs.find(decl.name);
        } else {
            bool same = found->second.ret == decl.ret && found->second.params.size() == decl.params.size();
            for (size_t i = 0; same && i < decl.params.size(); ++i) {
                same = found->second.params[i].type == decl.params[i].type;
            }
            if (!same) {
                throw NssError(decl.line, "'" + decl.name + "' is declared again with another signature");
            }
        }
        if (decl.body) {
            if (found->second.defined) {
                throw NssError(decl.line, "'" + decl.name + "' is defined twice");
            }
            found->second.defined = true;
        }
    }

    void emitGlobal(const GlobalDecl &decl) {
        if (_globals.count(decl.name)) {
            throw NssError(decl.line, "global '" + decl.name + "' is declared twice");
        }
        VarInfo var;
        var.type = decl.type;
        var.global = true;
        var.slot = _globalSlots;
        _inGlobalsInit = true;
        _depth = _globalSlots;
        _scopes.clear();
        if (decl.init) {
            Ty type = genExpr(*decl.init);
            convert(type, decl.type, decl.line);
        } else {
            rsadd(decl.type);
        }
        _inGlobalsInit = false;
        _globalSlots += slotsOf(decl.type);
        _globals[decl.name] = var;
    }

    void emitFunction(const FuncDecl &decl) {
        FuncInfo &info = _funcs[decl.name];
        _inFunction = true;
        _scopes.clear();
        _loops.clear();
        _retType = decl.ret;
        pushScope();
        // The caller pushed the arguments last to first, so the first argument is on top.
        int slot = 0;
        for (auto it = decl.params.rbegin(); it != decl.params.rend(); ++it) {
            VarInfo var;
            var.type = it->type;
            var.slot = slot;
            _scopes.back()[it->name] = var;
            slot += slotsOf(it->type);
        }
        _paramSlots = slot;
        _depth = slot;
        bind(info.label);
        genBlockBody(*decl.body);
        pop(_depth - _paramSlots);
        emit(Instruction(InstructionType::RETN));
        _scopes.pop_back();
        _inFunction = false;
    }

    // Expressions: types ------------------------------------------------------

    bool constantType(const std::string &name, Ty &type) const {
        if (name == "TRUE" || name == "FALSE" || _options.intConstants.count(name)) {
            type = Ty::Int;
            return true;
        }
        if (name == "OBJECT_SELF" || name == "OBJECT_INVALID") {
            type = Ty::Object;
            return true;
        }
        if (_options.floatConstants.count(name)) {
            type = Ty::Float;
            return true;
        }
        if (_options.stringConstants.count(name)) {
            type = Ty::String;
            return true;
        }
        return false;
    }

    static bool isNumeric(Ty t) { return t == Ty::Int || t == Ty::Float; }

    Ty infer(const Expr &e) {
        switch (e.kind) {
        case Expr::IntLit:
            return Ty::Int;
        case Expr::FloatLit:
            return Ty::Float;
        case Expr::StrLit:
            return Ty::String;
        case Expr::VecLit:
            return Ty::Vector;
        case Expr::Ident: {
            if (auto *var = findVar(e.text)) {
                return var->type;
            }
            Ty type;
            if (constantType(e.text, type)) {
                return type;
            }
            throw NssError(e.line, "unknown name '" + e.text + "'");
        }
        case Expr::Unary:
            return e.text == "!" || e.text == "~" ? Ty::Int : infer(*e.kids[0]);
        case Expr::Binary:
            return inferBinary(e);
        case Expr::Assign:
            return infer(*e.kids[0]);
        case Expr::Call: {
            auto found = _funcs.find(e.text);
            if (found != _funcs.end()) {
                return found->second.ret;
            }
            int index = _routines.getIndexByName(e.text);
            if (index < 0) {
                throw NssError(e.line, "unknown function '" + e.text + "'");
            }
            return fromVariableType(_routines.get(index).returnType());
        }
        case Expr::Member:
            return Ty::Float;
        case Expr::Ternary:
            return infer(*e.kids[1]);
        case Expr::IncDec:
            return Ty::Int;
        }
        return Ty::Void;
    }

    Ty inferBinary(const Expr &e) {
        const std::string &op = e.text;
        Ty l = infer(*e.kids[0]);
        Ty r = infer(*e.kids[1]);
        if (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            return Ty::Int;
        }
        if (op == "+" && l == Ty::String && r == Ty::String) {
            return Ty::String;
        }
        if (l == Ty::Vector || r == Ty::Vector) {
            return Ty::Vector;
        }
        if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>" || op == ">>>" || op == "%") {
            return Ty::Int;
        }
        if (l == Ty::Float || r == Ty::Float) {
            return Ty::Float;
        }
        return Ty::Int;
    }

    // Expressions: code --------------------------------------------------------

    // Widens the value on top of the stack; only int to float is implicit.
    void convert(Ty from, Ty to, int line) {
        if (from == to) {
            return;
        }
        if (from == Ty::Int && to == Ty::Float) {
            emit(Instruction::newCONSTF(0.0f));
            emit(Instruction(InstructionType::ADDIF));
            return;
        }
        throw NssError(line, fail(from, to));
    }

    // Generates e leaving its value on the stack, converted to `want`.
    void genAs(const Expr &e, Ty want) {
        Ty got = genExpr(e);
        convert(got, want, e.line);
    }

    Ty genExpr(const Expr &e) {
        switch (e.kind) {
        case Expr::IntLit:
            emit(Instruction::newCONSTI(e.intValue));
            ++_depth;
            return Ty::Int;
        case Expr::FloatLit:
            emit(Instruction::newCONSTF(e.floatValue));
            ++_depth;
            return Ty::Float;
        case Expr::StrLit:
            emit(Instruction::newCONSTS(e.text));
            ++_depth;
            return Ty::String;
        case Expr::VecLit:
            // x is pushed first, so it sits deepest and z is on top.
            for (const auto &kid : e.kids) {
                genAs(*kid, Ty::Float);
            }
            return Ty::Vector;
        case Expr::Ident:
            return genIdent(e);
        case Expr::Unary:
            return genUnary(e);
        case Expr::Binary:
            return genBinary(e);
        case Expr::Assign:
            return genAssign(e);
        case Expr::Call:
            return genCall(e);
        case Expr::Member:
            return genMember(e);
        case Expr::Ternary:
            return genTernary(e);
        case Expr::IncDec:
            return genIncDec(e);
        }
        return Ty::Void;
    }

    Ty genIdent(const Expr &e) {
        if (auto *var = findVar(e.text)) {
            loadVar(*var, 0, slotsOf(var->type));
            return var->type;
        }
        if (e.text == "TRUE" || e.text == "FALSE") {
            emit(Instruction::newCONSTI(e.text == "TRUE" ? 1 : 0));
            ++_depth;
            return Ty::Int;
        }
        if (e.text == "OBJECT_SELF" || e.text == "OBJECT_INVALID") {
            emit(Instruction::newCONSTO(e.text == "OBJECT_SELF" ? static_cast<int>(kObjectSelf) : static_cast<int>(kObjectInvalid)));
            ++_depth;
            return Ty::Object;
        }
        if (auto i = _options.intConstants.find(e.text); i != _options.intConstants.end()) {
            emit(Instruction::newCONSTI(i->second));
            ++_depth;
            return Ty::Int;
        }
        if (auto f = _options.floatConstants.find(e.text); f != _options.floatConstants.end()) {
            emit(Instruction::newCONSTF(f->second));
            ++_depth;
            return Ty::Float;
        }
        if (auto s = _options.stringConstants.find(e.text); s != _options.stringConstants.end()) {
            emit(Instruction::newCONSTS(s->second));
            ++_depth;
            return Ty::String;
        }
        throw NssError(e.line, "unknown name '" + e.text + "'");
    }

    Ty genUnary(const Expr &e) {
        Ty type = genExpr(*e.kids[0]);
        if (e.text == "-") {
            if (type == Ty::Int) {
                emit(Instruction(InstructionType::NEGI));
            } else if (type == Ty::Float) {
                emit(Instruction(InstructionType::NEGF));
            } else {
                throw NssError(e.line, std::string("cannot negate ") + nameOf(type));
            }
            return type;
        }
        if (type != Ty::Int) {
            throw NssError(e.line, "'" + e.text + "' needs an int");
        }
        emit(Instruction(e.text == "!" ? InstructionType::NOTI : InstructionType::COMPI));
        return Ty::Int;
    }

    static bool binaryInstruction(const std::string &op, Ty l, Ty r, InstructionType &out) {
        struct Row {
            const char *op;
            Ty l, r;
            InstructionType type;
        };
        static const Row kRows[] = {
            {"+", Ty::Int, Ty::Int, InstructionType::ADDII},
            {"-", Ty::Int, Ty::Int, InstructionType::SUBII},
            {"*", Ty::Int, Ty::Int, InstructionType::MULII},
            {"/", Ty::Int, Ty::Int, InstructionType::DIVII},
            {"%", Ty::Int, Ty::Int, InstructionType::MODII},
            {"<<", Ty::Int, Ty::Int, InstructionType::SHLEFTII},
            {">>", Ty::Int, Ty::Int, InstructionType::SHRIGHTII},
            {">>>", Ty::Int, Ty::Int, InstructionType::USHRIGHTII},
            {"&", Ty::Int, Ty::Int, InstructionType::BOOLANDII},
            {"|", Ty::Int, Ty::Int, InstructionType::INCORII},
            {"^", Ty::Int, Ty::Int, InstructionType::EXCORII},
            {"&&", Ty::Int, Ty::Int, InstructionType::LOGANDII},
            {"||", Ty::Int, Ty::Int, InstructionType::LOGORII},
            {"==", Ty::Int, Ty::Int, InstructionType::EQUALII},
            {"!=", Ty::Int, Ty::Int, InstructionType::NEQUALII},
            {">=", Ty::Int, Ty::Int, InstructionType::GEQII},
            {">", Ty::Int, Ty::Int, InstructionType::GTII},
            {"<", Ty::Int, Ty::Int, InstructionType::LTII},
            {"<=", Ty::Int, Ty::Int, InstructionType::LEQII},
            {"+", Ty::Float, Ty::Float, InstructionType::ADDFF},
            {"-", Ty::Float, Ty::Float, InstructionType::SUBFF},
            {"*", Ty::Float, Ty::Float, InstructionType::MULFF},
            {"/", Ty::Float, Ty::Float, InstructionType::DIVFF},
            {"==", Ty::Float, Ty::Float, InstructionType::EQUALFF},
            {"!=", Ty::Float, Ty::Float, InstructionType::NEQUALFF},
            {">=", Ty::Float, Ty::Float, InstructionType::GEQFF},
            {">", Ty::Float, Ty::Float, InstructionType::GTFF},
            {"<", Ty::Float, Ty::Float, InstructionType::LTFF},
            {"<=", Ty::Float, Ty::Float, InstructionType::LEQFF},
            {"+", Ty::String, Ty::String, InstructionType::ADDSS},
            {"==", Ty::String, Ty::String, InstructionType::EQUALSS},
            {"!=", Ty::String, Ty::String, InstructionType::NEQUALSS},
            {"==", Ty::Object, Ty::Object, InstructionType::EQUALOO},
            {"!=", Ty::Object, Ty::Object, InstructionType::NEQUALOO},
            {"==", Ty::Effect, Ty::Effect, InstructionType::EQUALEFFEFF},
            {"!=", Ty::Effect, Ty::Effect, InstructionType::NEQUALEFFEFF},
            {"==", Ty::Event, Ty::Event, InstructionType::EQUALEVTEVT},
            {"!=", Ty::Event, Ty::Event, InstructionType::NEQUALEVTEVT},
            {"==", Ty::Location, Ty::Location, InstructionType::EQUALLOCLOC},
            {"!=", Ty::Location, Ty::Location, InstructionType::NEQUALLOCLOC},
            {"==", Ty::Talent, Ty::Talent, InstructionType::EQUALTALTAL},
            {"!=", Ty::Talent, Ty::Talent, InstructionType::NEQUALTALTAL},
            {"+", Ty::Vector, Ty::Vector, InstructionType::ADDVV},
            {"-", Ty::Vector, Ty::Vector, InstructionType::SUBVV},
            {"*", Ty::Vector, Ty::Float, InstructionType::MULVF},
            {"*", Ty::Float, Ty::Vector, InstructionType::MULFV},
            {"/", Ty::Vector, Ty::Float, InstructionType::DIVVF},
            {"/", Ty::Float, Ty::Vector, InstructionType::DIVFV},
        };
        for (const auto &row : kRows) {
            if (op == row.op && l == row.l && r == row.r) {
                out = row.type;
                return true;
            }
        }
        return false;
    }

    Ty genBinary(const Expr &e) {
        const std::string &op = e.text;
        Ty l = infer(*e.kids[0]);
        Ty r = infer(*e.kids[1]);
        // Operand types after the implicit int to float widening.
        Ty wl = l;
        Ty wr = r;
        bool intFloatMix = (l == Ty::Int && r == Ty::Float) || (l == Ty::Float && r == Ty::Int);
        if (intFloatMix) {
            wl = wr = Ty::Float;
        } else if (l == Ty::Int && r == Ty::Vector) {
            wl = Ty::Float;
        } else if (l == Ty::Vector && r == Ty::Int) {
            wr = Ty::Float;
        }
        int slotsL = slotsOf(wl);
        int slotsR = slotsOf(wr);
        bool vectorEquality = l == Ty::Vector && r == Ty::Vector && (op == "==" || op == "!=");
        InstructionType type = InstructionType::NOP;
        bool found = binaryInstruction(op, wl, wr, type);
        if (!found && !vectorEquality) {
            throw NssError(e.line, std::string("cannot apply '") + op + "' to " + nameOf(l) + " and " + nameOf(r));
        }
        genAs(*e.kids[0], wl);
        genAs(*e.kids[1], wr);
        if (vectorEquality) {
            emit(Instruction(op == "==" ? InstructionType::EQUALTT : InstructionType::NEQUALTT)).ins.size = 12;
            _depth -= slotsL + slotsR - 1;
            return Ty::Int;
        }
        emit(Instruction(type));
        Ty result = inferBinary(e);
        _depth += slotsOf(result) - slotsL - slotsR;
        return result;
    }

    // The lvalue a name or a vector component stands for.
    struct LValue {
        const VarInfo *var {nullptr};
        int slotOffset {0};
        Ty type {Ty::Int};
    };

    LValue lvalueOf(const Expr &e) {
        LValue lv;
        if (e.kind == Expr::Ident) {
            lv.var = findVar(e.text);
            if (!lv.var) {
                throw NssError(e.line, "'" + e.text + "' is not a variable");
            }
            lv.type = lv.var->type;
            return lv;
        }
        if (e.kind == Expr::Member && e.kids[0]->kind == Expr::Ident) {
            lv.var = findVar(e.kids[0]->text);
            if (!lv.var || lv.var->type != Ty::Vector) {
                throw NssError(e.line, "'" + e.kids[0]->text + "' is not a vector variable");
            }
            lv.slotOffset = componentIndex(e);
            lv.type = Ty::Float;
            return lv;
        }
        throw NssError(e.line, "the left side cannot be assigned to");
    }

    static int componentIndex(const Expr &e) {
        if (e.text == "x") {
            return 0;
        }
        if (e.text == "y") {
            return 1;
        }
        if (e.text == "z") {
            return 2;
        }
        throw NssError(e.line, "a vector has no component '" + e.text + "'");
    }

    Ty genAssign(const Expr &e) {
        LValue lv = lvalueOf(*e.kids[0]);
        if (e.text == "=") {
            genAs(*e.kids[1], lv.type);
        } else {
            Expr combined;
            combined.kind = Expr::Binary;
            combined.line = e.line;
            combined.text = e.text.substr(0, e.text.size() - 1);
            combined.kids = {e.kids[0], e.kids[1]};
            Ty got = genExpr(combined);
            convert(got, lv.type, e.line);
        }
        storeVar(*lv.var, lv.slotOffset, slotsOf(lv.type));
        return lv.type;
    }

    Ty genIncDec(const Expr &e) {
        LValue lv = lvalueOf(*e.kids[0]);
        if (lv.type != Ty::Int || lv.slotOffset != 0) {
            throw NssError(e.line, "'" + e.text + "' needs an int variable");
        }
        bool inc = e.text == "++";
        auto step = [&]() {
            if (lv.var->global && !_inGlobalsInit) {
                Em &em = emit(inc ? Instruction::newINCIBP(0) : Instruction::newDECIBP(0));
                em.globalSlot = lv.var->slot;
            } else {
                emit(inc ? Instruction::newINCISP((lv.var->slot - _depth) * 4) : Instruction::newDECISP((lv.var->slot - _depth) * 4));
            }
        };
        if (e.prefix) {
            step();
            loadVar(*lv.var, 0, 1);
        } else {
            loadVar(*lv.var, 0, 1);
            step();
        }
        return Ty::Int;
    }

    Ty genMember(const Expr &e) {
        int index = componentIndex(e);
        const Expr &base = *e.kids[0];
        if (base.kind == Expr::Ident) {
            if (auto *var = findVar(base.text)) {
                if (var->type != Ty::Vector) {
                    throw NssError(e.line, "'" + base.text + "' is not a vector");
                }
                loadVar(*var, index, 1);
                return Ty::Float;
            }
        }
        Ty type = genExpr(base);
        if (type != Ty::Vector) {
            throw NssError(e.line, std::string("a ") + nameOf(type) + " has no components");
        }
        // Copy the component, move it down over the vector's first slot, drop the rest.
        int vectorSlot = _depth - 3;
        emit(Instruction::newCPTOPSP((vectorSlot + index - _depth) * 4, 4));
        ++_depth;
        emit(Instruction::newCPDOWNSP((vectorSlot - _depth) * 4, 4));
        pop(3);
        return Ty::Float;
    }

    Ty genTernary(const Expr &e) {
        Ty a = infer(*e.kids[1]);
        Ty b = infer(*e.kids[2]);
        Ty result = a;
        if ((a == Ty::Int && b == Ty::Float) || (a == Ty::Float && b == Ty::Int)) {
            result = Ty::Float;
        } else if (a != b) {
            throw NssError(e.line, std::string("the branches of '?:' are ") + nameOf(a) + " and " + nameOf(b));
        }
        int elseLabel = newLabel();
        int endLabel = newLabel();
        genAs(*e.kids[0], Ty::Int);
        emitJump(Instruction::newJZ(0), elseLabel);
        --_depth;
        int start = _depth;
        genAs(*e.kids[1], result);
        emitJump(Instruction::newJMP(0), endLabel);
        _depth = start;
        bind(elseLabel);
        genAs(*e.kids[2], result);
        bind(endLabel);
        return result;
    }

    Ty genCall(const Expr &e) {
        auto user = _funcs.find(e.text);
        if (user != _funcs.end()) {
            return genUserCall(e, user->second);
        }
        return genRoutineCall(e);
    }

    Ty genUserCall(const Expr &e, const FuncInfo &info) {
        if (e.kids.size() != info.params.size()) {
            throw NssError(e.line, "'" + e.text + "' takes " + std::to_string(info.params.size()) + " arguments");
        }
        rsadd(info.ret);
        int argSlots = 0;
        for (size_t i = info.params.size(); i-- > 0;) {
            genAs(*e.kids[i], info.params[i].type);
            argSlots += slotsOf(info.params[i].type);
        }
        emitJump(Instruction::newJSR(0), info.label);
        pop(argSlots);
        return info.ret;
    }

    Ty genRoutineCall(const Expr &e) {
        int index = _routines.getIndexByName(e.text);
        if (index < 0) {
            throw NssError(e.line, "unknown function '" + e.text + "'");
        }
        Routine &routine = _routines.get(index);
        if (static_cast<int>(e.kids.size()) > routine.getArgumentCount()) {
            throw NssError(e.line, "'" + e.text + "' takes at most " + std::to_string(routine.getArgumentCount()) + " arguments");
        }
        int argSlots = 0;
        for (size_t i = e.kids.size(); i-- > 0;) {
            Ty want = fromVariableType(routine.getArgumentType(static_cast<int>(i)));
            if (want == Ty::Action) {
                genActionArg(*e.kids[i]);
            } else {
                genAs(*e.kids[i], want);
                argSlots += slotsOf(want);
            }
        }
        emit(Instruction::newACTION(index, static_cast<int>(e.kids.size())));
        _depth -= argSlots;
        Ty ret = fromVariableType(routine.returnType());
        _depth += slotsOf(ret);
        if (ret == Ty::Vector) {
            // The VM pushes a returned vector as z, y, x, while every other vector on the
            // stack (literals, arguments, arithmetic) is x, y, z with z on top. Turn the
            // result around so that .x of a returned vector is its x.
            emit(Instruction::newCPTOPSP(-4, 4));
            emit(Instruction::newCPTOPSP(-12, 4));
            emit(Instruction::newCPTOPSP(-20, 4));
            emit(Instruction::newCPDOWNSP(-24, 12));
            emit(Instruction::newMOVSP(-12));
        }
        return ret;
    }

    // An action parameter: the expression runs later, from a saved copy of the
    // globals and of this function's stack.
    void genActionArg(const Expr &e) {
        if (!_inFunction) {
            throw NssError(e.line, "an action argument is only allowed inside a function");
        }
        int endLabel = newLabel();
        Em &store = emit(Instruction::newSTORE_STATE(0, _depth * 4));
        store.storeStateGlobals = true;
        emitJump(Instruction::newJMP(0), endLabel);
        int saved = _depth;
        Ty type = genExpr(e);
        pop(slotsOf(type));
        emit(Instruction(InstructionType::RETN));
        _depth = saved;
        bind(endLabel);
    }

    // Statements -------------------------------------------------------------

    void genBlockBody(const Stmt &block) {
        pushScope();
        int start = _depth;
        for (const auto &stmt : block.body) {
            genStmt(*stmt);
        }
        popScope(start);
    }

    void genStmt(const Stmt &s) {
        switch (s.kind) {
        case Stmt::Block:
            genBlockBody(s);
            break;
        case Stmt::Empty:
            break;
        case Stmt::Decl:
            for (const auto &[name, init] : s.decls) {
                if (_scopes.back().count(name)) {
                    throw NssError(s.line, "'" + name + "' is declared twice");
                }
                VarInfo var;
                var.type = s.declType;
                var.slot = _depth;
                if (init) {
                    genAs(*init, s.declType);
                } else {
                    rsadd(s.declType);
                }
                _scopes.back()[name] = var;
            }
            break;
        case Stmt::ExprStmt: {
            Ty type = genExpr(*s.expr);
            pop(slotsOf(type));
            break;
        }
        case Stmt::If: {
            int elseLabel = newLabel();
            genAs(*s.expr, Ty::Int);
            emitJump(Instruction::newJZ(0), elseLabel);
            --_depth;
            genScoped(*s.first);
            if (s.second) {
                int endLabel = newLabel();
                emitJump(Instruction::newJMP(0), endLabel);
                bind(elseLabel);
                genScoped(*s.second);
                bind(endLabel);
            } else {
                bind(elseLabel);
            }
            break;
        }
        case Stmt::While: {
            int top = newLabel();
            int end = newLabel();
            bind(top);
            genAs(*s.expr, Ty::Int);
            emitJump(Instruction::newJZ(0), end);
            --_depth;
            _loops.push_back({end, top, _depth});
            genScoped(*s.first);
            _loops.pop_back();
            emitJump(Instruction::newJMP(0), top);
            bind(end);
            break;
        }
        case Stmt::DoWhile: {
            int top = newLabel();
            int cond = newLabel();
            int end = newLabel();
            bind(top);
            _loops.push_back({end, cond, _depth});
            genScoped(*s.first);
            _loops.pop_back();
            bind(cond);
            genAs(*s.expr, Ty::Int);
            emitJump(Instruction::newJNZ(0), top);
            --_depth;
            bind(end);
            break;
        }
        case Stmt::For: {
            pushScope();
            int start = _depth;
            if (s.init) {
                genStmt(*s.init);
            }
            int top = newLabel();
            int incr = newLabel();
            int end = newLabel();
            bind(top);
            if (s.expr) {
                genAs(*s.expr, Ty::Int);
                emitJump(Instruction::newJZ(0), end);
                --_depth;
            }
            _loops.push_back({end, incr, _depth});
            genScoped(*s.first);
            _loops.pop_back();
            bind(incr);
            if (s.incr) {
                Ty type = genExpr(*s.incr);
                pop(slotsOf(type));
            }
            emitJump(Instruction::newJMP(0), top);
            bind(end);
            popScope(start);
            break;
        }
        case Stmt::Return:
            genReturn(s);
            break;
        case Stmt::Break:
        case Stmt::Continue: {
            if (_loops.empty()) {
                throw NssError(s.line, "'break' and 'continue' belong in a loop");
            }
            const Loop &loop = _loops.back();
            int saved = _depth;
            int toPop = _depth - loop.depth;
            if (toPop > 0) {
                emit(Instruction::newMOVSP(-4 * toPop));
            }
            emitJump(Instruction::newJMP(0), s.kind == Stmt::Break ? loop.breakLabel : loop.continueLabel);
            _depth = saved;
            break;
        }
        }
    }

    // A nested statement gets its own scope so its declarations end with it.
    void genScoped(const Stmt &s) {
        pushScope();
        int start = _depth;
        genStmt(s);
        popScope(start);
    }

    void genReturn(const Stmt &s) {
        if (!_inFunction) {
            throw NssError(s.line, "'return' outside a function");
        }
        int saved = _depth;
        if (_retType == Ty::Void) {
            if (s.expr) {
                throw NssError(s.line, "a void function returns no value");
            }
        } else {
            if (!s.expr) {
                throw NssError(s.line, "this function must return a value");
            }
            genAs(*s.expr, _retType);
            int slots = slotsOf(_retType);
            // The return slots sit just below the arguments.
            emit(Instruction::newCPDOWNSP((-slots - _depth) * 4, static_cast<uint16_t>(slots * 4)));
        }
        int toPop = _depth - _paramSlots;
        if (toPop > 0) {
            emit(Instruction::newMOVSP(-4 * toPop));
        }
        emit(Instruction(InstructionType::RETN));
        _depth = saved;
    }

    // Assembly ------------------------------------------------------------------

    std::shared_ptr<ScriptProgram> assemble(const std::string &name) {
        std::string entry = _funcs.count("main") ? "main" : (_funcs.count("StartingConditional") ? "StartingConditional" : "");
        if (entry.empty() || !_funcs[entry].defined) {
            throw NssError(1, "no 'main' or 'StartingConditional' function");
        }
        const FuncInfo &main = _funcs[entry];
        if (!main.params.empty()) {
            throw NssError(1, "'" + entry + "' takes no arguments");
        }

        int globalsLabel = newLabel();
        _cur = &_startBuf;
        _depth = 0;
        bool hasGlobals = _globalSlots > 0;
        if (hasGlobals) {
            emitJump(Instruction::newJSR(0), globalsLabel);
        }
        rsadd(main.ret);
        emitJump(Instruction::newJSR(0), main.label);
        if (hasGlobals && main.ret == Ty::Void) {
            emit(Instruction::newMOVSP(0)).popGlobals = true;
        }
        emit(Instruction(InstructionType::RETN));

        if (hasGlobals) {
            _cur = &_globalsBuf;
            emit(Instruction(InstructionType::SAVEBP));
            emit(Instruction(InstructionType::RETN));
        }

        // Concatenate: start, globals (initialisers then SAVEBP), functions.
        if (hasGlobals) {
            _globalsBuf.binds.emplace_back(0, globalsLabel);
        }
        std::vector<Em> all;
        std::map<int, size_t> labelIndex;
        for (Buf *buf : {&_startBuf, &_globalsBuf, &_funcBuf}) {
            size_t base = all.size();
            for (const auto &[index, label] : buf->binds) {
                labelIndex[label] = base + index;
            }
            all.insert(all.end(), buf->code.begin(), buf->code.end());
        }

        // Offsets
        std::vector<uint32_t> offsets(all.size() + 1);
        uint32_t offset = 13;
        for (size_t i = 0; i < all.size(); ++i) {
            offsets[i] = offset;
            offset += getInstructionSize(all[i].ins);
        }
        offsets[all.size()] = offset;

        auto program = std::make_shared<ScriptProgram>(name);
        for (size_t i = 0; i < all.size(); ++i) {
            Em &em = all[i];
            if (em.jumpLabel >= 0) {
                auto found = labelIndex.find(em.jumpLabel);
                if (found == labelIndex.end()) {
                    throw NssError(1, "internal error: unbound label");
                }
                em.ins.jumpOffset = static_cast<int>(offsets[found->second]) - static_cast<int>(offsets[i]);
            }
            if (em.globalSlot >= 0) {
                em.ins.stackOffset = (em.globalSlot - _globalSlots) * 4;
            }
            if (em.storeStateGlobals) {
                em.ins.size = static_cast<uint16_t>(_globalSlots * 4);
            }
            if (em.popGlobals) {
                em.ins.stackOffset = -(_globalSlots + 1) * 4;
            }
            program->add(em.ins);
        }
        return program;
    }
};

} // namespace

std::shared_ptr<ScriptProgram> compileNss(
    const std::string &name,
    const std::string &source,
    IRoutines &routines,
    const NssOptions &options) {

    Parser parser(lex(source));
    auto items = parser.parseProgram();
    Gen gen(routines, options);
    return gen.run(name, items);
}

std::vector<char> writeNcsBytes(ScriptProgram &program) {
    ByteBuffer bytes;
    auto out = std::make_shared<MemoryOutputStream>(bytes);
    NcsWriter(program).save(out);
    return std::vector<char>(bytes.begin(), bytes.end());
}

std::shared_ptr<ScriptProgram> readNcsBytes(const std::string &name, const std::vector<char> &bytes) {
    ByteBuffer buffer(bytes.begin(), bytes.end());
    MemoryInputStream stream(buffer);
    NcsReader reader(stream, name);
    reader.load();
    return reader.program();
}

std::shared_ptr<ScriptProgram> compileNssRoundTrip(
    const std::string &name,
    const std::string &source,
    IRoutines &routines,
    const NssOptions &options) {

    auto program = compileNss(name, source, routines, options);
    return readNcsBytes(name, writeNcsBytes(*program));
}

bool sameProgram(const ScriptProgram &a, const ScriptProgram &b) {
    const auto &x = a.instructions();
    const auto &y = b.instructions();
    if (x.size() != y.size() || a.length() != b.length()) {
        return false;
    }
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i].type != y[i].type || x[i].offset != y[i].offset || x[i].nextOffset != y[i].nextOffset) {
            return false;
        }
        switch (x[i].type) {
        case InstructionType::CONSTS:
            if (x[i].strValue != y[i].strValue) {
                return false;
            }
            break;
        case InstructionType::CONSTF:
            if (x[i].floatValue != y[i].floatValue) {
                return false;
            }
            break;
        default:
            if (x[i].intValue != y[i].intValue || x[i].jumpOffset != y[i].jumpOffset || x[i].size != y[i].size) {
                return false;
            }
            break;
        }
    }
    return true;
}

} // namespace testsupport

} // namespace reone
