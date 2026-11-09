#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "../ast/ast.cpp"
#include "../types/types.cpp"

class Parser
{
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)), index_(0) {}

    Program parse()
    {
        Program program;
        while (!isAtEnd())
        {
            if (peek().type == TokenType::Comment)
            {
                advance();
                continue;
            }
            if (peek().value == ";")
            {
                advance();
                continue;
            }

            Node *stmt = parseStatement();
            if (stmt)
            {
                program.body.push_back(stmt);
            }
        }
        return program;
    }

private:
    std::string parseTypeName()
    {
        std::string type = advance().value;

        if (type == "Vec" && peek().value == "<")
        {
            advance();
            std::string elementType = parseTypeName();
            if (peek().value == ">")
                advance();
            return elementType + "[]";
        }

        if (type == "map" && peek().value == "[")
        {
            advance();
            std::string keyType = advance().value;
            if (peek().value == ",")
                advance();
            std::string valueType = advance().value;
            if (peek().value == "]")
                advance();
            return "map[" + keyType + "," + valueType + "]";
        }

        if (peek().value == "[")
        {
            advance();
            if (peek().value == "]")
            {
                advance();
                return type + "[]";
            }

            std::string size = advance().value;
            if (peek().value == "]")
                advance();
            return type + "[" + size + "]";
        }

        return type;
    }

    bool isAtEnd() const
    {
        return peek().type == TokenType::EOF_T;
    }

    Token peek() const
    {
        if (index_ >= tokens_.size())
            return Token{TokenType::EOF_T, ""};
        return tokens_[index_];
    }

    Token advance()
    {
        if (index_ >= tokens_.size())
            return Token{TokenType::EOF_T, ""};
        return tokens_[index_++];
    }

    bool isBinaryOperator(const Token &token) const
    {
        if (token.type != TokenType::Operator)
            return false;
        static const std::vector<std::string> ops = {
            "+", "-", "*", "/", "%", "==", "!=", ">", "<", ">=", "<=", "&&", "||",
            "&", "|", "^", "<<", ">>"
        };
        return std::find(ops.begin(), ops.end(), token.value) != ops.end();
    }

    Node *parseStatement()
    {
        // Comments were only ever skipped by the top-level parse() loop,
        // so one written inside a function/if/while body would fall
        // through every check below and hit the catch-all -- which
        // consumes the comment's own token, not the comment, and hands
        // back an empty Identifier as if it were real code. Skipping
        // here instead means every nested body-parsing loop (which all
        // call parseStatement in a loop and already handle a null
        // return the same way ';' is handled) gets this for free.
        if (peek().type == TokenType::Comment)
        {
            advance();
            return nullptr;
        }

        if (peek().value == ";")
        {
            advance();
            return nullptr;
        }

        if (peek().value == "let" || peek().value == "const" || peek().value == "mut")
        {
            return parseVariableDeclaration();
        }

        if (peek().value == "func" || peek().value == "fn")
        {
            return parseFunctionDeclaration();
        }

        if (peek().value == "extern")
        {
            advance(); // "extern"
            Node *node = parseFunctionDeclaration();
            if (auto *fn = dynamic_cast<Function *>(node))
            {
                fn->isExtern = true;
            }
            return node;
        }

        if (peek().value == "printf" || peek().value == "say")
        {
            return parsePrintStatement();
        }

        if (peek().value == "return")
        {
            return parseReturnStatement();
        }

        if (peek().value == "break" || peek().value == "continue")
        {
            LoopControl *node = new LoopControl();
            node->keyword = advance().value;
            return node;
        }

        if (peek().value == "if")
        {
            return parseIfStatement();
        }

        if (peek().value == "while")
        {
            return parseWhileLoop();
        }

        if (peek().value == "for")
        {
            return parseForLoop();
        }

        if (peek().value == "struct")
        {
            return parseStructDecl();
        }

        if (peek().value == "import")
        {
            return parseImportStatement();
        }

        if (peek().type == TokenType::Identifier)
        {
            return parseIdentifierStatement();
        }

        if (peek().type == TokenType::Number || peek().type == TokenType::Double || peek().type == TokenType::String || peek().type == TokenType::Char)
        {
            return parseLiteralStatement();
        }

        if (peek().value == "true" || peek().value == "false")
        {
            return parseLiteralStatement();
        }

        ExpressionStatement *node = new ExpressionStatement();
        node->expression = parseExpression();
        return node;
    }

    Node *parseVariableDeclaration()
    {
        std::string kind = advance().value;
        if (kind == "let" && peek().value == "mut")
            advance();
        std::string name = advance().value;

        std::string type = "";
        if (peek().value == ":")
        {
            advance();
            type = parseTypeName();
        }

        // Arrays may be declared without an initializer
        // (`let nums: number[5];` / `let xs: number[];`) or with one
        // (`let xs: number[] = [1, 2, 3];`).
        Node *value = nullptr;
        if (peek().value == "=")
        {
            advance();
            value = parseExpression();
        }

        VariableDeclaration *node = new VariableDeclaration();
        node->identifier = name;
        node->declaredType = type;
        node->value = value;
        (void)kind;
        return node;
    }

    Node *parseFunctionDeclaration()
    {
        advance(); // func
        Function *node = new Function();

        Token nameTok = advance();
        node->name = nameTok.value;

        if (peek().value == "(")
        {
            advance();
            while (!isAtEnd() && peek().value != ")")
            {
                Token paramName = advance();
                Param p;
                p.name = paramName.value;
                if (peek().value == ":")
                {
                    advance();
                    p.type = parseTypeName();
                }
                node->params.push_back(p);
                if (peek().value == ",")
                {
                    advance();
                }
            }
            if (peek().value == ")")
            {
                advance();
            }
        }

        if (peek().value == ":" || peek().value == "->")
        {
            advance();
            node->returnType = parseTypeName();
        }

        if (peek().value == "{")
        {
            advance();
            while (!isAtEnd() && peek().value != "}")
            {
                Node *stmt = parseStatement();
                if (stmt)
                {
                    node->body.push_back(stmt);
                }
            }
            if (peek().value == "}")
            {
                advance();
            }
        }

        return node;
    }

    Node *parsePrintStatement()
    {
        advance();
        PrintStatement *node = new PrintStatement();
        node->expression = parseExpression();
        return node;
    }

    Node *parseReturnStatement()
    {
        advance(); // "return"

        // A bare `return;` with no value is valid (early-exit from a
        // void function). Only parse an expression if the next token
        // could actually start one -- otherwise, since ';' produces no
        // token at all (the lexer discards it), parseExpression() would
        // fall through to its catch-all and consume whatever comes
        // next instead (e.g. a block's closing '}'), desynchronizing
        // the rest of the parse.
        bool hasValue = peek().type == TokenType::Identifier ||
                        peek().type == TokenType::Number ||
                        peek().type == TokenType::Double ||
                        peek().type == TokenType::String ||
                        peek().value == "true" || peek().value == "false" ||
                        peek().value == "(" ||
                        (peek().type == TokenType::Operator && (peek().value == "!" || peek().value == "-"));

        ReturnStatement *node = new ReturnStatement();
        node->value = hasValue ? parseExpression() : nullptr;
        return node;
    }

    Node *parseIfStatement()
    {
        advance();
        IfStatement *node = new IfStatement();
        node->condition = parseExpression();

        if (peek().value == "{")
        {
            advance();
            while (!isAtEnd() && peek().value != "}")
            {
                Node *stmt = parseStatement();
                if (stmt)
                {
                    node->consequent.push_back(stmt);
                }
            }
            if (peek().value == "}")
            {
                advance();
            }
        }

        if (peek().value == "else")
        {
            advance();
            if (peek().value == "if")
            {
                Node *nested = parseIfStatement();
                node->alternate.push_back(nested);
            }
            else if (peek().value == "{")
            {
                advance();
                while (!isAtEnd() && peek().value != "}")
                {
                    Node *stmt = parseStatement();
                    if (stmt)
                    {
                        node->alternate.push_back(stmt);
                    }
                }
                if (peek().value == "}")
                {
                    advance();
                }
            }
        }

        return node;
    }

    Node *parseWhileLoop()
    {
        advance();
        WhileLoop *node = new WhileLoop();
        node->condition = parseExpression();

        if (peek().value == "{")
        {
            advance();
            while (!isAtEnd() && peek().value != "}")
            {
                Node *stmt = parseStatement();
                if (stmt)
                {
                    node->body.push_back(stmt);
                }
            }
            if (peek().value == "}")
            {
                advance();
            }
        }

        return node;
    }

    Node *parseImportStatement()
    {
        advance(); // "import"

        ImportStatement *node = new ImportStatement();
        if (peek().type == TokenType::String)
        {
            node->path = advance().value;
        }
        return node;
    }


    Node *parseStructDecl()
    {
        advance(); // struct
        StructDecl *node = new StructDecl();
        node->name = advance().value;
        if (peek().value == "{")
            advance();
        while (!isAtEnd() && peek().value != "}")
        {
            if (peek().type == TokenType::Comment)
            {
                advance();
                continue;
            }
            StructField field;
            field.name = advance().value;
            if (peek().value == ":")
            {
                advance();
                field.type = parseTypeName();
            }
            else
            {
                field.type = "number";
            }
            node->fields.push_back(field);
            if (peek().value == ",")
                advance();
        }
        if (peek().value == "}")
            advance();
        return node;
    }

    Node *parseForLoop()
    {
        advance(); // for
        ForLoop *node = new ForLoop();
        node->iterator = advance().value;
        if (peek().value == "in")
            advance();

        // Parse the iterable. Support explicit range: <expr> .. <expr>
        // without relying on the binary-operator table for "..".
        Node *first = parsePrecedence(0);
        if (peek().value == "..")
        {
            advance();
            node->start = first;
            node->end = parsePrecedence(0);
        }
        else
        {
            node->collection = first;
        }

        if (peek().value == "{")
            advance();
        while (!isAtEnd() && peek().value != "}")
        {
            Node *stmt = parseStatement();
            if (stmt)
                node->body.push_back(stmt);
        }
        if (peek().value == "}")
            advance();
        return node;
    }

    Node *parseIdentifierStatement()
    {
        std::string name = advance().value;

        // Field assign: name.field = expr
        if (peek().value == ".")
        {
            advance();
            std::string field = advance().value;
            if (peek().value == "=")
            {
                advance();
                FieldAssignment *fa = new FieldAssignment();
                Identifier *obj = new Identifier();
                obj->name = name;
                fa->object = obj;
                fa->field = field;
                fa->value = parseExpression();
                return fa;
            }
            // bare field access as statement
            FieldAccess *acc = new FieldAccess();
            Identifier *obj = new Identifier();
            obj->name = name;
            acc->object = obj;
            acc->field = field;
            return acc;
        }

        if (peek().value == "[")
        {
            advance();
            Node *indexExpr = parseExpression();
            if (peek().value == "]")
            {
                advance();
            }

            if (peek().value == "=")
            {
                advance();
                IndexAssignment *node = new IndexAssignment();
                Identifier *obj = new Identifier();
                obj->name = name;
                node->object = obj;
                node->index = indexExpr;
                node->value = parseExpression();
                return node;
            }

            // `name[i]` used as a bare statement (rare, but falls back
            // safely rather than dropping the tokens).
            IndexExpression *idx = new IndexExpression();
            Identifier *obj = new Identifier();
            obj->name = name;
            idx->object = obj;
            idx->index = indexExpr;
            return idx;
        }

        if (peek().value == "=")
        {
            advance();
            Assignment *node = new Assignment();
            node->identifier = name;
            node->value = parseExpression();
            return node;
        }

        if (peek().value == "(")
        {
            advance();
            CallExpression *node = new CallExpression();
            node->callee = name;
            while (!isAtEnd() && peek().value != ")")
            {
                node->arguments.push_back(parseExpression());
                if (peek().value == ",")
                {
                    advance();
                }
            }
            if (peek().value == ")")
            {
                advance();
            }
            return node;
        }

        if (peek().value == "++" || peek().value == "--")
        {
            std::string op = advance().value;
            if (op == "++")
            {
                auto *node = new Increment();
                node->identifier = name;
                return node;
            }
            else
            {
                auto *node = new Decrement();
                node->identifier = name;
                return node;
            }
        }

        Identifier *node = new Identifier();
        node->name = name;
        return node;
    }

    Node *parseLiteralStatement()
    {
        Token tok = advance();

        if (tok.type == TokenType::Number)
        {
            LiteralInt *val = new LiteralInt();
            val->value = std::stoll(tok.value);
            return val;
        }

        if (tok.type == TokenType::Double)
        {
            LiteralDoudle *val = new LiteralDoudle();
            val->value = std::stod(tok.value);
            return val;
        }

        if (tok.type == TokenType::String)
        {
            LiteralString *val = new LiteralString();
            val->value = tok.value;
            return val;
        }

        if (tok.type == TokenType::Char) {
            LiteralChar *val = new LiteralChar();

            if (tok.value.empty()) {
                // malformed / empty char literal – decide your error policy
                // Option A: treat as null character
                val->value = '0';
                // Option B: return an error node / report and recover
            } else {
                val->value = static_cast<unsigned char>(tok.value[0]);  // or just tok.value[0]
            }

            return val;
        }

        if (tok.value == "true")
        {
            LiteralBool *val = new LiteralBool();
            val->value = true;
            return val;
        }

        if (tok.value == "false")
        {
            LiteralBool *val = new LiteralBool();
            val->value = false;
            return val;
        }

        return new Identifier();
    }

    Node *parseExpression()
    {
        return parsePrecedence(0);
    }

    Node *parsePrecedence(int minPrecedence)
    {
        Node *left = parsePrimary();

        while (!isAtEnd() && peek().type == TokenType::Operator && isBinaryOperator(peek()))
        {
            std::string op = peek().value;
            int precedence = precedenceOf(op);
            if (precedence < minPrecedence)
            {
                break;
            }
            advance();

            Node *previousLeft = left;
            Node *right = parsePrecedence(precedence + 1);

            BinaryExpression *expr = new BinaryExpression();
            expr->op = op;
            expr->left = previousLeft;
            expr->right = right;
            left = expr;
        }

        return left;
    }

    int precedenceOf(const std::string &op) const
    {
        if (op == "||")
            return 1;
        if (op == "&&")
            return 2;
        if (op == "|")
            return 3;
        if (op == "^")
            return 4;
        if (op == "&")
            return 5;
        if (op == "==" || op == "!=")
            return 6;
        if (op == "<" || op == ">" || op == "<=" || op == ">=")
            return 7;
        if (op == "<<" || op == ">>")
            return 8;
        if (op == "+" || op == "-")
            return 9;
        if (op == "*" || op == "/" || op == "%")
            return 10;
        if (op == "..")
            return 11; // only used inside for-range parsing as a marker
        return -1;
    }

    Node *parsePrimary()
    {
        if (peek().type == TokenType::Identifier)
        {
            std::string name = advance().value;
            if (peek().value == "(")
            {
                advance();
                CallExpression *node = new CallExpression();
                node->callee = name;
                while (!isAtEnd() && peek().value != ")")
                {
                    node->arguments.push_back(parseExpression());
                    if (peek().value == ",")
                    {
                        advance();
                    }
                }
                if (peek().value == ")")
                {
                    advance();
                }
                return node;
            }

            // Struct literal: Name { field: value, ... }
            // Lookahead: only if after "{" we see `ident :` — otherwise
            // `for x in nums { ... }` would steal the for-body brace.
            if (peek().value == "{")
            {
                // peek tokens: index_, tokens_
                bool isStructLit = false;
                if (index_ + 1 < tokens_.size())
                {
                    const Token &t1 = tokens_[index_ + 1]; // after current '{'
                    // Actually peek() is '{', so index_ points at '{'. Next is index_+1
                }
                if (index_ + 2 < tokens_.size())
                {
                    // tokens_[index_] is '{', [index_+1] should be field name, [index_+2] ':'
                    const Token &fnameTok = tokens_[index_ + 1];
                    const Token &colonTok = tokens_[index_ + 2];
                    if ((fnameTok.type == TokenType::Identifier || fnameTok.type == TokenType::Keyword) &&
                        colonTok.value == ":")
                    {
                        isStructLit = true;
                    }
                    // empty struct literal Name {}
                    if (fnameTok.value == "}")
                        isStructLit = true;
                }
                if (isStructLit)
                {
                    advance(); // {
                    StructLiteral *lit = new StructLiteral();
                    lit->name = name;
                    while (!isAtEnd() && peek().value != "}")
                    {
                        if (peek().type == TokenType::Comment)
                        {
                            advance();
                            continue;
                        }
                        std::string fname = advance().value;
                        if (peek().value == ":")
                            advance();
                        Node *fval = parseExpression();
                        lit->fieldNames.push_back(fname);
                        lit->fieldValues.push_back(fval);
                        if (peek().value == ",")
                            advance();
                    }
                    if (peek().value == "}")
                        advance();
                    return lit;
                }
            }

            if (peek().value == "[")
            {
                advance();
                IndexExpression *idx = new IndexExpression();
                Identifier *obj = new Identifier();
                obj->name = name;
                idx->object = obj;
                idx->index = parseExpression();
                if (peek().value == "]")
                {
                    advance();
                }
                return idx;
            }

            // Field access: name.field (possibly chained later)
            if (peek().value == ".")
            {
                Node *obj = nullptr;
                Identifier *id = new Identifier();
                id->name = name;
                obj = id;
                while (peek().value == ".")
                {
                    advance();
                    FieldAccess *acc = new FieldAccess();
                    acc->object = obj;
                    acc->field = advance().value;
                    obj = acc;
                }
                return obj;
            }

            Identifier *node = new Identifier();
            node->name = name;
            return node;
        }

        if (peek().type == TokenType::Number)
        {
            LiteralInt *val = new LiteralInt();
            val->value = std::stoll(advance().value);
            return val;
        }

        if (peek().type == TokenType::Double)
        {
            LiteralDoudle *val = new LiteralDoudle();
            val->value = std::stod(advance().value);
            return val;
        }

        if (peek().type == TokenType::String)
        {
            LiteralString *val = new LiteralString();
            val->value = advance().value;
            return val;
        }

        if (peek().type == TokenType::Char)
        {
            Token tok = advance();
            LiteralChar *val = new LiteralChar();
            if (tok.value.empty()) {
                val->value = '\0';          // better than '0'
            } else {
                val->value = static_cast<unsigned char>(tok.value[0]);
            }
            return val;
        }

        if (peek().value == "true" || peek().value == "false")
        {
            LiteralBool *val = new LiteralBool();
            val->value = (advance().value == "true");
            return val;
        }

        if (peek().value == "(")
        {
            advance();
            Node *inner = parseExpression();
            if (peek().value == ")")
            {
                advance();
            }
            return inner;
        }

        // Array literal: [1, 2, 3] or []
        if (peek().value == "[")
        {
            advance();
            ArrayLiteral *lit = new ArrayLiteral();
            while (!isAtEnd() && peek().value != "]")
            {
                lit->elements.push_back(parseExpression());
                if (peek().value == ",")
                {
                    advance();
                }
            }
            if (peek().value == "]")
            {
                advance();
            }
            return lit;
        }

        if (peek().type == TokenType::Operator && (peek().value == "!" || peek().value == "-" || peek().value == "~"))
        {
            UnaryExpression *node = new UnaryExpression();
            node->op = advance().value;
            node->operand = parsePrimary();
            return node;
        }

        Token t = advance();
        (void)t;
        return new Identifier();
    }

    std::vector<Token> tokens_;
    size_t index_;
};