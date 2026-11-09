#pragma once

#include <string>
#include <vector>

struct Node {
    virtual ~Node() = default;
};

struct Program {
    std::vector<::Node*> body;
};

struct Param {
    std::string name;
    std::string type;
};

struct VariableDeclaration : Node {
    std::string identifier;
    std::string declaredType;
    ::Node* value = nullptr;
};

struct Function : Node {
    std::string name;
    std::vector<Param> params;
    std::string returnType;
    std::vector<::Node*> body;
    // true for `extern func name(...): type;` -- a signature-only
    // declaration for a function implemented elsewhere.
    bool isExtern = false;
};

struct ReturnStatement : Node {
    ::Node* value = nullptr;
};

struct PrintStatement : Node {
    ::Node* expression = nullptr;
};

struct IfStatement : Node {
    ::Node* condition = nullptr;
    std::vector<::Node*> consequent;
    std::vector<::Node*> alternate;
};

struct WhileLoop : Node {
    ::Node* condition = nullptr;
    std::vector<::Node*> body;
};

struct LoopControl : Node {
    std::string keyword;
};

struct Comment : Node {
    std::string value;
};

struct BinaryExpression : Node {
    ::Node* left = nullptr;
    std::string op;
    ::Node* right = nullptr;
};

struct CallExpression : Node {
    std::string callee;
    std::vector<::Node*> arguments;
};

struct Increment : Node {
    std::string identifier;
};

struct Decrement : Node {
    std::string identifier;
};

struct Identifier : Node {
    std::string name;
};

struct LiteralInt : Node {
    long long value = 0;
};

struct LiteralDoudle : Node {
    double value = 0.0;
};

struct LiteralString : Node {
    std::string value;
};

struct LiteralChar : Node {
    char value;
};

struct LiteralBool : Node {
    bool value = false;
};

struct IndexExpression : Node {
    ::Node* object = nullptr;
    ::Node* index = nullptr;
};

struct IndexAssignment : Node {
    ::Node* object = nullptr;
    ::Node* index = nullptr;
    ::Node* value = nullptr;
};

// Fixed or dynamic array literal: [1, 2, 3] / ["a", "b"] / []
struct ArrayLiteral : Node {
    std::vector<::Node*> elements;
};

struct Assignment : Node {
    std::string identifier;
    ::Node* value = nullptr;
};

struct ExpressionStatement : Node {
    ::Node* expression = nullptr;
};

struct UnaryExpression : Node {
    std::string op;
    ::Node* operand = nullptr;
};

struct ImportStatement : Node {
    std::string path;
};

struct StructField {
    std::string name;
    std::string type;
};

struct StructDecl : Node {
    std::string name;
    std::vector<StructField> fields;
};

struct FieldAccess : Node {
    ::Node* object = nullptr;
    std::string field;
};

struct FieldAssignment : Node {
    ::Node* object = nullptr;
    std::string field;
    ::Node* value = nullptr;
};

struct StructLiteral : Node {
    std::string name;
    std::vector<std::string> fieldNames;
    std::vector<::Node*> fieldValues;
};

// for i in a..b { }  or  for x in arr { }
struct ForLoop : Node {
    std::string iterator;
    ::Node* start = nullptr;      // range start (null if foreach)
    ::Node* end = nullptr;        // range end
    ::Node* collection = nullptr; // foreach collection (null if range)
    std::vector<::Node*> body;
};