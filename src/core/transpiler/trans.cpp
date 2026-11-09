#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../lexer/lexer.cpp"
#include "../parser/parser.cpp"
#include "../typechecker/typechecker.cpp"

namespace
{
    std::map<std::string, std::vector<std::string>> functionParameterTypes;

    std::string trim(const std::string &value)
    {
        std::size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        {
            ++start;
        }

        std::size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        {
            --end;
        }

        return value.substr(start, end - start);
    }

    std::string escapeCString(const std::string &value)
    {
        std::ostringstream escaped;
        for (unsigned char character : value)
        {
            switch (character)
            {
            case '\\': escaped << "\\\\"; break;
            case '"': escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\v': escaped << "\\v"; break;
            default:
                if (character < 32 || character >= 127)
                {
                    escaped << '\\'
                            << static_cast<char>('0' + ((character >> 6) & 7))
                            << static_cast<char>('0' + ((character >> 3) & 7))
                            << static_cast<char>('0' + (character & 7));
                }
                else
                {
                    escaped << static_cast<char>(character);
                }
            }
        }
        return escaped.str();
    }

    std::string convertTypeName(const std::string &typeName)
    {
        if (typeName == "number")
            return "int";
        if (typeName == "double")
            return "double";
        if (typeName == "string")
            return "const char*";
        if (typeName == "bool")
            return "bool";
        if (typeName == "void")
            return "void";
        if (typeName == "char")
            return "char";
        if (typeName == "number[]")
            return "quill_arr_int";
        if (typeName == "double[]")
            return "quill_arr_double";
        if (typeName == "string[]")
            return "quill_arr_str";
        if (typeName == "bool[]")
            return "quill_arr_bool";
        return typeName;
    }

    // Maps any spelling of a Quill/C type name to Quill's own canonical
    // name ("number", "double", "string", "bool", "void"). Unknown/future type
    // names fall back to "number" so a typo doesn't crash the transpiler --
    // the typechecker is responsible for catching genuine type errors.
    std::string normalizeTypeName(const std::string &typeName)
    {
        std::string type = trim(typeName);
        if (type == "string" || type == "const char*")
            return "string";
        if (type == "char")
            return "char";
        if (type == "double" || type == "double")
            return "double";
        if (type == "bool")
            return "bool";
        if (type == "number")
            return "number";
        if (type == "void")
            return "void";
        if (!type.empty() && type.find('[') != std::string::npos)
            return type;
        if (!type.empty())
            return type;
        return "number";
    }


    // --- array type helpers -------------------------------------------------
    // "number[5]"  -> fixed, size 5
    // "number[]"   -> dynamic
    bool isArrayTypeName(const std::string &t)
    {
        return t.find('[') != std::string::npos;
    }
    bool isDynamicArrayType(const std::string &t)
    {
        return t.size() >= 2 && t.find("[]") != std::string::npos;
    }
    std::string arrayElemType(const std::string &t)
    {
        auto b = t.find('[');
        if (b == std::string::npos)
            return t;
        return t.substr(0, b);
    }
    // C struct name for a dynamic array of element type
    std::string dynArrCType(const std::string &elem)
    {
        if (elem == "double")
            return "quill_arr_double";
        if (elem == "bool")
            return "quill_arr_bool";
        if (elem == "string")
            return "quill_arr_str";
        return "quill_arr_int"; // number
    }
    std::string dynArrPrefix(const std::string &elem)
    {
        if (elem == "double")
            return "quill_arr_double";
        if (elem == "bool")
            return "quill_arr_bool";
        if (elem == "string")
            return "quill_arr_str";
        return "quill_arr_int";
    }

    bool isArrayReferenceType(const std::string &type)
    {
        return type.size() >= 4 && type.compare(type.size() - 4, 4, "@ref") == 0;
    }

    bool isGlobalType(const std::string &type)
    {
        return type.size() >= 7 && type.compare(type.size() - 7, 7, "@global") == 0;
    }

    std::string sourceTypeName(const std::string &type)
    {
        if (isArrayReferenceType(type))
            return type.substr(0, type.size() - 4);
        if (isGlobalType(type))
            return type.substr(0, type.size() - 7);
        return type;
    }

    std::string generatedVariableName(const std::string &name,
                                      const std::map<std::string, std::string> &scope)
    {
        auto it = scope.find(name);
        return it != scope.end() && isGlobalType(it->second) ? "quill_global_" + name : name;
    }

    bool isMapType(const std::string &t)
    {
        return t.rfind("map[", 0) == 0;
    }
    std::string mapKeyType(const std::string &t)
    {
        auto lb = t.find('[');
        auto comma = t.find(',');
        if (lb == std::string::npos || comma == std::string::npos)
            return "string";
        return t.substr(lb + 1, comma - lb - 1);
    }
    std::string mapValType(const std::string &t)
    {
        auto comma = t.find(',');
        auto rb = t.rfind(']');
        if (comma == std::string::npos || rb == std::string::npos)
            return "number";
        return t.substr(comma + 1, rb - comma - 1);
    }
    std::string mapRuntimeName(const std::string &valT)
    {
        if (valT == "string") return "quill_map_str";
        if (valT == "double") return "quill_map_double";
        if (valT == "bool") return "quill_map_bool";
        return "quill_map_int";
    }

    std::string inferNodeType(Node *node,
                              const std::map<std::string, std::string> &scope,
                              const std::map<std::string, std::string> &functionReturnTypes)
    {
        if (!node)
            return "void";

        if (dynamic_cast<LiteralInt *>(node))
            return "number";
        if (dynamic_cast<LiteralDoudle *>(node))
            return "double";
        if (dynamic_cast<LiteralString *>(node))
            return "string";
        if (dynamic_cast<LiteralChar *>(node))
            return "char";
        if (dynamic_cast<LiteralBool *>(node))
            return "bool";

        if (auto *ident = dynamic_cast<Identifier *>(node))
        {
            auto it = scope.find(ident->name);
            return it != scope.end() ? sourceTypeName(it->second) : "number";
        }

        // Indexing yields either a character code (indexing a plain
        // string -- always "number", same as C's char) or, for a real
        // array, the array's own element type ("string[3]" indexed
        // gives "string", "double[2]" gives "double", etc.) -- not
        // unconditionally "number", which was only ever correct back
        // when number[] was the only array element type in use.
        if (auto *idx = dynamic_cast<IndexExpression *>(node))
        {
            std::string objType = inferNodeType(idx->object, scope, functionReturnTypes);
            if (isMapType(objType))
                return mapValType(objType);
            auto bracketPos = objType.find('[');
            if (bracketPos != std::string::npos)
            {
                return objType.substr(0, bracketPos);
            }
            return "number";
        }

        if (auto *lit = dynamic_cast<ArrayLiteral *>(node))
        {
            if (lit->elements.empty())
                return "number[]";
            std::string elem = inferNodeType(lit->elements[0], scope, functionReturnTypes);
            for (size_t i = 1; i < lit->elements.size(); ++i)
            {
                std::string t = inferNodeType(lit->elements[i], scope, functionReturnTypes);
                if (t != elem)
                {
                    if ((elem == "number" && t == "double") || (elem == "double" && t == "number"))
                        elem = "double";
                    else
                        return "unknown";
                }
            }
            return elem + "[]";
        }

        if (auto *bin = dynamic_cast<BinaryExpression *>(node))
        {
            static const std::vector<std::string> comparisonOps = {
                "==", "!=", "<", ">", "<=", ">=", "&&", "||"};
            if (std::find(comparisonOps.begin(), comparisonOps.end(), bin->op) != comparisonOps.end())
            {
                return "bool";
            }

            std::string leftType = inferNodeType(bin->left, scope, functionReturnTypes);
            std::string rightType = inferNodeType(bin->right, scope, functionReturnTypes);

            if (bin->op == "+" && (leftType == "string" || rightType == "string"))
            {
                return "string";
            }
            if (leftType == "double" || rightType == "double")
            {
                return "double";
            }
            return "number";
        }

        if (auto *unary = dynamic_cast<UnaryExpression *>(node))
        {
            if (unary->op == "!")
                return "bool";
            return inferNodeType(unary->operand, scope, functionReturnTypes);
        }

        if (auto *call = dynamic_cast<CallExpression *>(node))
        {
            auto it = functionReturnTypes.find(call->callee);
            return it != functionReturnTypes.end() ? it->second : "number";
        }

        return "number";
    }

    void collectDeclaredLocals(const std::vector<Node *> &stmts,
                               std::map<std::string, std::string> &scope,
                               const std::map<std::string, std::string> &functionReturnTypes)
    {
        for (Node *node : stmts)
        {
            if (!node)
                continue;

            if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
            {
                std::string type;
                bool isArrayType = decl->declaredType.find('[') != std::string::npos;
                bool isMap = decl->declaredType.rfind("map[", 0) == 0;
                if (isMap || isArrayType)
                {
                    type = decl->declaredType;
                }
                else if (!decl->declaredType.empty())
                {
                    type = normalizeTypeName(decl->declaredType);
                }
                else
                {
                    type = inferNodeType(decl->value, scope, functionReturnTypes);
                }
                scope[decl->identifier] = type;
            }
            else if (auto *ifStmt = dynamic_cast<IfStatement *>(node))
            {
                collectDeclaredLocals(ifStmt->consequent, scope, functionReturnTypes);
                collectDeclaredLocals(ifStmt->alternate, scope, functionReturnTypes);
            }
            else if (auto *loop = dynamic_cast<WhileLoop *>(node))
            {
                collectDeclaredLocals(loop->body, scope, functionReturnTypes);
            }
            else if (auto *fl = dynamic_cast<ForLoop *>(node))
            {
                collectDeclaredLocals(fl->body, scope, functionReturnTypes);
            }
        }
    }

    std::map<std::string, std::string> buildScope(const std::vector<Param> &params,
                                                  const std::vector<Node *> &body,
                                                  const std::map<std::string, std::string> &functionReturnTypes)
    {
        std::map<std::string, std::string> scope;
        for (const Param &p : params)
        {
            std::string type = p.type.empty() ? "number" : normalizeTypeName(p.type);
            if (isDynamicArrayType(type))
                type += "@ref";
            scope[p.name] = type;
        }
        collectDeclaredLocals(body, scope, functionReturnTypes);
        return scope;
    }

    std::map<std::string, std::string> buildTopLevelScope(
        const Program &program,
        const std::map<std::string, std::string> &functionReturnTypes)
    {
        std::map<std::string, std::string> scope;
        for (Node *node : program.body)
        {
            if (dynamic_cast<VariableDeclaration *>(node))
            {
                std::vector<Node *> declaration{node};
                collectDeclaredLocals(declaration, scope, functionReturnTypes);
            }
        }
        for (auto &symbol : scope)
            symbol.second += "@global";
        return scope;
    }

    // Combines two return-site types the same way binary-expression
    // codegen already promotes mixed arithmetic: number+double ->
    // double (the more general of the two). A genuinely incompatible
    // pair (e.g. number and string) keeps whichever was found first --
    // Quill's typechecker doesn't cross-check return-path consistency,
    // so this stays lenient rather than introducing a new hard error
    // class here.
    std::string combineReturnTypes(const std::string &a, const std::string &b)
    {
        if (a == "void")
            return b;
        if (b == "void")
            return a;
        if (a == b)
            return a;
        if ((a == "number" && b == "double") || (a == "double" && b == "number"))
            return "double";
        return a;
    }

    // Walks *every* return statement reachable in this function body --
    // not just the first one found -- since an early bail-out of a
    // different numeric type (e.g. `return 0;` inside a safety-valve
    // check partway through a function that mainly `return`s a
    // double) must not be allowed to decide the whole function's
    // return type just because it happens to be visited first.
    std::string inferFunctionReturnType(const std::vector<Node *> &body,
                                        const std::map<std::string, std::string> &scope,
                                        const std::map<std::string, std::string> &functionReturnTypes)
    {
        std::string found = "void";
        for (Node *node : body)
        {
            if (!node)
                continue;

            std::string candidate = "void";

            if (auto *ret = dynamic_cast<ReturnStatement *>(node))
            {
                if (ret->value)
                {
                    candidate = inferNodeType(ret->value, scope, functionReturnTypes);
                }
            }
            else if (auto *ifStmt = dynamic_cast<IfStatement *>(node))
            {
                std::string fromConsequent = inferFunctionReturnType(ifStmt->consequent, scope, functionReturnTypes);
                std::string fromAlternate = inferFunctionReturnType(ifStmt->alternate, scope, functionReturnTypes);
                candidate = combineReturnTypes(fromConsequent, fromAlternate);
            }
            else if (auto *loop = dynamic_cast<WhileLoop *>(node))
            {
                candidate = inferFunctionReturnType(loop->body, scope, functionReturnTypes);
            }
            else if (auto *loop = dynamic_cast<ForLoop *>(node))
            {
                candidate = inferFunctionReturnType(loop->body, scope, functionReturnTypes);
            }

            found = combineReturnTypes(found, candidate);
        }
        return found;
    }

    std::map<std::string, std::string> buildFunctionReturnTypes(const Program &program)
    {
        std::map<std::string, std::string> types;
        types["len"] = "number";
        types["toString"] = "string";
        types["toInt"] = "number";
        types["toDouble"] = "double";
        types["toChar"] = "char";
        types["charAt"] = "char";
        types["substring"] = "string";
        types["contains"] = "bool";
        types["startsWith"] = "bool";
        types["endsWith"] = "bool";
        types["indexOf"] = "number";
        types["push"] = "void";
        types["pop"] = "number";
        types["read_file"] = "string";
        types["write_file"] = "number";
        types["file_exists"] = "bool";
        types["argc"] = "number";
        types["argv"] = "string";
        types["map_has"] = "bool";
        types["map_len"] = "number";
        types["map_del"] = "void";
        std::map<std::string, std::string> globalScope = buildTopLevelScope(program, types);

        for (Node *node : program.body)
        {
            if (auto *fn = dynamic_cast<Function *>(node))
            {
                if (!fn->returnType.empty())
                {
                    types[fn->name] = normalizeTypeName(fn->returnType);
                }
                else
                {
                    std::map<std::string, std::string> scope = buildScope(fn->params, fn->body, types);
                    for (const auto &global : globalScope)
                        scope.emplace(global);
                    types[fn->name] = inferFunctionReturnType(fn->body, scope, types);
                }
            }
        }
        return types;
    }

    std::map<std::string, std::vector<std::string>> buildFunctionParameterTypes(const Program &program)
    {
        std::map<std::string, std::vector<std::string>> types;
        for (Node *node : program.body)
        {
            if (auto *fn = dynamic_cast<Function *>(node))
            {
                std::vector<std::string> parameterTypes;
                for (const Param &param : fn->params)
                    parameterTypes.push_back(param.type.empty() ? "number" : normalizeTypeName(param.type));
                types[fn->name] = parameterTypes;
            }
        }
        return types;
    }

    std::string astToC(Node *node,
                       const std::map<std::string, std::string> &scope,
                       const std::map<std::string, std::string> &functionReturnTypes);

    std::string arrayAddress(Node *node,
                             const std::map<std::string, std::string> &scope,
                             const std::map<std::string, std::string> &functionReturnTypes)
    {
        if (auto *ident = dynamic_cast<Identifier *>(node))
        {
            auto it = scope.find(ident->name);
            if (it != scope.end() && isArrayReferenceType(it->second))
                return ident->name;
        }
        return "&(" + astToC(node, scope, functionReturnTypes) + ")";
    }

    bool isArrayReference(Node *node, const std::map<std::string, std::string> &scope)
    {
        auto *ident = dynamic_cast<Identifier *>(node);
        if (!ident)
            return false;
        auto it = scope.find(ident->name);
        return it != scope.end() && isArrayReferenceType(it->second);
    }

    std::string toCStringExpr(Node *node,
                              const std::map<std::string, std::string> &scope,
                              const std::map<std::string, std::string> &functionReturnTypes)
    {
        std::string type = inferNodeType(node, scope, functionReturnTypes);
        std::string code = astToC(node, scope, functionReturnTypes);
        if (type == "string")
            return code;
        if (type == "char")
            return "quill_char_str(" + code + ")";
        if (type == "double")
            return "quill_ftoa(" + code + ")";
        if (type == "bool")
            return "((" + code + ") ? \"true\" : \"false\")";
        return "quill_itoa(" + code + ")";
    }

    std::string astToC(Node *node,
                       const std::map<std::string, std::string> &scope,
                       const std::map<std::string, std::string> &functionReturnTypes)
    {
        if (!node)
            return "";

        if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
        {
            auto it = scope.find(decl->identifier);
            std::string typeName = it != scope.end() ? it->second : "number";

            if (isMapType(typeName))
            {
                std::string rt = mapRuntimeName(mapValType(typeName));
                return rt + " " + decl->identifier + " = " + rt + "_new();";
            }

            // Dynamic array: number[] / double[] / ...
            if (isDynamicArrayType(typeName))
            {
                std::string elem = arrayElemType(typeName);
                std::string ctype = dynArrCType(elem);
                std::string pref = dynArrPrefix(elem);
                if (auto *lit = dynamic_cast<ArrayLiteral *>(decl->value))
                {
                    std::ostringstream init;
                    init << ctype << " " << decl->identifier << " = " << pref << "_new();\n";
                    for (Node *el : lit->elements)
                    {
                        init << "    " << pref << "_push(&" << decl->identifier << ", "
                             << astToC(el, scope, functionReturnTypes) << ");\n";
                    }
                    std::string s = init.str();
                    // last extra newline handled by caller
                    if (!s.empty() && s.back() == '\n')
                        s.pop_back();
                    return s;
                }
                if (decl->value)
                {
                    std::string valueCode = astToC(decl->value, scope, functionReturnTypes);
                    if (isArrayReference(decl->value, scope))
                        valueCode = "*" + valueCode;
                    return ctype + " " + decl->identifier + " = " +
                           valueCode + ";";
                }
                return ctype + " " + decl->identifier + " = " + pref + "_new();";
            }

            // Fixed-size array: number[5]
            auto bracketPos = typeName.find('[');
            if (bracketPos != std::string::npos)
            {
                std::string baseType = typeName.substr(0, bracketPos);
                std::string sizePart = typeName.substr(bracketPos);
                if (auto *lit = dynamic_cast<ArrayLiteral *>(decl->value))
                {
                    std::ostringstream init;
                    init << "{";
                    for (size_t i = 0; i < lit->elements.size(); ++i)
                    {
                        if (i)
                            init << ", ";
                        init << astToC(lit->elements[i], scope, functionReturnTypes);
                    }
                    init << "}";
                    return convertTypeName(baseType) + " " + decl->identifier + sizePart +
                           " = " + init.str() + ";";
                }
                return convertTypeName(baseType) + " " + decl->identifier + sizePart + ";";
            }

            std::string type = convertTypeName(typeName);
            if (decl->value)
            {
                return type + " " + decl->identifier + " = " +
                       astToC(decl->value, scope, functionReturnTypes) + ";";
            }
            return type + " " + decl->identifier + " = {0};";
        }

        if (auto *assign = dynamic_cast<Assignment *>(node))
        {
            return generatedVariableName(assign->identifier, scope) + " = " + astToC(assign->value, scope, functionReturnTypes) + ";";
        }

        if (auto *idxAssign = dynamic_cast<IndexAssignment *>(node))
        {
            std::string objCode = astToC(idxAssign->object, scope, functionReturnTypes);
            std::string idxCode = astToC(idxAssign->index, scope, functionReturnTypes);
            std::string valCode = astToC(idxAssign->value, scope, functionReturnTypes);
            std::string objType = inferNodeType(idxAssign->object, scope, functionReturnTypes);
            if (isMapType(objType))
            {
                std::string rt = mapRuntimeName(mapValType(objType));
                return rt + "_set(&" + objCode + ", " + idxCode + ", " + valCode + ");";
            }
            if (isDynamicArrayType(objType))
            {
                std::string pref = dynArrPrefix(arrayElemType(objType));
                return pref + "_set(" + arrayAddress(idxAssign->object, scope, functionReturnTypes) + ", " + idxCode + ", " + valCode + ");";
            }
            return objCode + "[" + idxCode + "] = " + valCode + ";";
        }

        if (auto *bin = dynamic_cast<BinaryExpression *>(node))
        {
            if (bin->op == "==" || bin->op == "!=")
            {
                std::string leftType = inferNodeType(bin->left, scope, functionReturnTypes);
                std::string rightType = inferNodeType(bin->right, scope, functionReturnTypes);
                if (leftType == "string" && rightType == "string")
                {
                    std::string comparison = "strcmp(" + astToC(bin->left, scope, functionReturnTypes) + ", " +
                                            astToC(bin->right, scope, functionReturnTypes) + ")";
                    return "(" + comparison + (bin->op == "==" ? " == 0)" : " != 0)");
                }
            }
            if (bin->op == "+")
            {
                std::string leftType = inferNodeType(bin->left, scope, functionReturnTypes);
                std::string rightType = inferNodeType(bin->right, scope, functionReturnTypes);
                if (leftType == "string" || rightType == "string")
                {
                    return "quill_concat(" +
                           toCStringExpr(bin->left, scope, functionReturnTypes) + ", " +
                           toCStringExpr(bin->right, scope, functionReturnTypes) + ")";
                }
            }
            return "(" + astToC(bin->left, scope, functionReturnTypes) + " " + bin->op + " " +
                   astToC(bin->right, scope, functionReturnTypes) + ")";
        }

        if (auto *unary = dynamic_cast<UnaryExpression *>(node))
        {
            return unary->op + astToC(unary->operand, scope, functionReturnTypes);
        }

        if (auto *litInt = dynamic_cast<LiteralInt *>(node))
            return std::to_string(litInt->value);
        if (auto *litDoudle = dynamic_cast<LiteralDoudle *>(node))
            return std::to_string(litDoudle->value);
        if (auto *litBool = dynamic_cast<LiteralBool *>(node))
            return litBool->value ? "true" : "false";
        if (auto *litStr = dynamic_cast<LiteralString *>(node))
            return "\"" + escapeCString(litStr->value) + "\"";
        if (auto *litChar = dynamic_cast<LiteralChar *>(node)) {
            // Emit a proper C character constant
            char c = static_cast<char>(litChar->value);
            if (c == '\n') return "'\\n'";
            if (c == '\t') return "'\\t'";
            if (c == '\'') return "'\\''";
            if (c == '\\') return "'\\\\'";
            if (c == '\0') return "'\\0'";
            // printable ASCII
            if (c >= 32 && c < 127)
                return std::string("'") + c + "'";
            // fallback: just the integer code point
            return std::to_string(static_cast<int>(static_cast<unsigned char>(c)));
        }
        if (auto *ident = dynamic_cast<Identifier *>(node))
            return generatedVariableName(ident->name, scope);

        if (auto *idx = dynamic_cast<IndexExpression *>(node))
        {
            std::string objCode = astToC(idx->object, scope, functionReturnTypes);
            std::string idxCode = astToC(idx->index, scope, functionReturnTypes);
            std::string objType = inferNodeType(idx->object, scope, functionReturnTypes);
            if (isMapType(objType))
            {
                std::string rt = mapRuntimeName(mapValType(objType));
                return rt + "_get(&" + objCode + ", " + idxCode + ")";
            }
            if (isDynamicArrayType(objType))
            {
                std::string pref = dynArrPrefix(arrayElemType(objType));
                return pref + "_get(" + arrayAddress(idx->object, scope, functionReturnTypes) + ", " + idxCode + ")";
            }
            return objCode + "[" + idxCode + "]";
        }

        if (auto *lit = dynamic_cast<ArrayLiteral *>(node))
        {
            // Bare array literal as expression -> temporary dynamic array
            std::string elem = "number";
            if (!lit->elements.empty())
                elem = inferNodeType(lit->elements[0], scope, functionReturnTypes);
            std::string pref = dynArrPrefix(elem);
            // Not ideal as expression; prefer used only as initializer.
            // Emit a compound that creates a temp — callers should use decl form.
            return pref + "_new() /* array literal used as expression; prefer initializer */";
        }

        if (auto *print = dynamic_cast<PrintStatement *>(node))
        {
            std::string exprCode = astToC(print->expression, scope, functionReturnTypes);
            std::string type = inferNodeType(print->expression, scope, functionReturnTypes);
            if (type == "string")
                return "printf(\"%s\", " + exprCode + ");";
            if (type == "char")
                return "printf(\"%c\", " + exprCode + ");";
            if (type == "double")
                return "printf(\"%f\", " + exprCode + ");";
            if (type == "bool")
                return "printf(\"%s\", (" + exprCode + ") ? \"true\" : \"false\");";
            return "printf(\"%d\", " + exprCode + ");";
        }

        if (auto *inc = dynamic_cast<Increment *>(node))
        {
            return generatedVariableName(inc->identifier, scope) + "++;";
        }
        if (auto *dec = dynamic_cast<Decrement *>(node))
        {
            return generatedVariableName(dec->identifier, scope) + "--;";
        }

        if (auto *ifStmt = dynamic_cast<IfStatement *>(node))
        {
            std::ostringstream block;
            block << "if (" << astToC(ifStmt->condition, scope, functionReturnTypes) << ") {\n";
            for (Node *stmt : ifStmt->consequent)
            {
                std::string code = astToC(stmt, scope, functionReturnTypes);
                if (!code.empty() && code.back() != ';' && code.back() != '}')
                    code += ";";
                block << "        " << code << "\n";
            }
            block << "    }";
            if (!ifStmt->alternate.empty())
            {
                block << " else {\n";
                for (Node *stmt : ifStmt->alternate)
                {
                    std::string code = astToC(stmt, scope, functionReturnTypes);
                    if (!code.empty() && code.back() != ';' && code.back() != '}')
                        code += ";";
                    block << "        " << code << "\n";
                }
                block << "    }";
            }
            return block.str();
        }

        if (auto *loop = dynamic_cast<WhileLoop *>(node))
        {
            std::ostringstream block;
            block << "while (" << astToC(loop->condition, scope, functionReturnTypes) << ") {\n";
            for (Node *stmt : loop->body)
            {
                std::string code = astToC(stmt, scope, functionReturnTypes);
                if (!code.empty() && code.back() != ';' && code.back() != '}')
                    code += ";";
                block << "        " << code << "\n";
            }
            block << "    }";
            return block.str();
        }

        if (auto *fl = dynamic_cast<ForLoop *>(node))
        {
            std::ostringstream block;
            if (fl->start && fl->end)
            {
                block << "for (int " << fl->iterator << " = "
                      << astToC(fl->start, scope, functionReturnTypes) << "; "
                      << fl->iterator << " < "
                      << astToC(fl->end, scope, functionReturnTypes) << "; "
                      << fl->iterator << "++) {\n";
            }
            else if (fl->collection)
            {
                std::string col = astToC(fl->collection, scope, functionReturnTypes);
                std::string colType = inferNodeType(fl->collection, scope, functionReturnTypes);
                if (isDynamicArrayType(colType))
                {
                    std::string pref = dynArrPrefix(arrayElemType(colType));
                    std::string elemC = convertTypeName(arrayElemType(colType));
                      block << "for (int __qi = 0; __qi < " << pref << "_len(" << arrayAddress(fl->collection, scope, functionReturnTypes) << "); __qi++) {\n";
                    block << "        " << elemC << " " << fl->iterator << " = "
                          << pref << "_get(" << arrayAddress(fl->collection, scope, functionReturnTypes) << ", __qi);\n";
                }
                else if (isArrayTypeName(colType))
                {
                    std::string elemC = convertTypeName(arrayElemType(colType));
                    block << "for (int __qi = 0; __qi < (int)(sizeof(" << col
                          << ")/sizeof((" << col << ")[0])); __qi++) {\n";
                    block << "        " << elemC << " " << fl->iterator << " = "
                          << col << "[__qi];\n";
                }
                else
                {
                    block << "{\n";
                }
            }
            else
            {
                block << "{\n";
            }
            for (Node *stmt : fl->body)
            {
                std::string sc = astToC(stmt, scope, functionReturnTypes);
                if (!sc.empty() && sc.back() != ';' && sc.back() != '}')
                    sc += ";";
                block << "        " << sc << "\n";
            }
            block << "    }";
            return block.str();
        }

        if (auto *sd = dynamic_cast<StructDecl *>(node))
        {
            std::ostringstream block;
            block << "typedef struct {\n";
            for (const auto &f : sd->fields)
            {
                std::string ty = f.type.empty() ? "number" : f.type;
                if (isDynamicArrayType(ty))
                    block << "        " << dynArrCType(arrayElemType(ty)) << " " << f.name << ";\n";
                else if (isArrayTypeName(ty))
                {
                    auto bpos = ty.find('[');
                    block << "        " << convertTypeName(ty.substr(0, bpos)) << " " << f.name
                          << ty.substr(bpos) << ";\n";
                }
                else
                    block << "        " << convertTypeName(ty) << " " << f.name << ";\n";
            }
            block << "    } " << sd->name << ";";
            return block.str();
        }

        if (auto *fa = dynamic_cast<FieldAccess *>(node))
        {
            return astToC(fa->object, scope, functionReturnTypes) + "." + fa->field;
        }

        if (auto *fas = dynamic_cast<FieldAssignment *>(node))
        {
            return astToC(fas->object, scope, functionReturnTypes) + "." + fas->field + " = " +
                   astToC(fas->value, scope, functionReturnTypes) + ";";
        }

        if (auto *sl = dynamic_cast<StructLiteral *>(node))
        {
            std::ostringstream block;
            block << "(" << sl->name << "){";
            for (size_t i = 0; i < sl->fieldNames.size(); ++i)
            {
                if (i)
                    block << ", ";
                block << "." << sl->fieldNames[i] << " = "
                      << astToC(sl->fieldValues[i], scope, functionReturnTypes);
            }
            block << "}";
            return block.str();
        }

        if (auto *exprStmt = dynamic_cast<ExpressionStatement *>(node))
        {
            std::string code = astToC(exprStmt->expression, scope, functionReturnTypes);
            if (!code.empty() && code.back() != ';')
            {
                code += ";";
            }
            return code;
        }

        if (auto *ret = dynamic_cast<ReturnStatement *>(node))
        {
            if (ret->value)
            {
                std::string valueCode = astToC(ret->value, scope, functionReturnTypes);
                if (isArrayReference(ret->value, scope))
                    valueCode = "*" + valueCode;
                return "return " + valueCode + ";";
            }
            return "return;";
        }

        if (auto *control = dynamic_cast<LoopControl *>(node))
            return control->keyword + ";";

        if (auto *call = dynamic_cast<CallExpression *>(node))
        {
            std::string name = call->callee;

            std::vector<std::string> argCodes;
            std::vector<std::string> argTypes;
            for (Node *arg : call->arguments)
            {
                argCodes.push_back(astToC(arg, scope, functionReturnTypes));
                argTypes.push_back(inferNodeType(arg, scope, functionReturnTypes));
            }

            if (name == "C_top")
                throw std::runtime_error("C_top has been removed; declare top-level variables and structs in Quill.");

            auto parameterTypes = functionParameterTypes.find(name);
            if (parameterTypes != functionParameterTypes.end())
            {
                for (std::size_t i = 0; i < argCodes.size() && i < parameterTypes->second.size(); ++i)
                {
                    if (isDynamicArrayType(parameterTypes->second[i]) && isDynamicArrayType(argTypes[i]))
                    {
                        if (!isArrayReference(call->arguments[i], scope))
                            argCodes[i] = "&(" + argCodes[i] + ")";
                    }
                }
            }

            if (name == "random_int" && argCodes.empty())
            {
                return "quill_random_int()";
            }
            else if (name == "toInt" && argCodes.size() == 1)
            {
                return "((int)" + argCodes[0] + ")";
            }
            else if (name == "toDouble" && argCodes.size() == 1)
            {
                return "((double)" + argCodes[0] + ")";
            }
            else if (name == "toChar" && argCodes.size() == 1)
            {
                return "((char)" + argCodes[0] + ")";
            }
            else if (name == "charAt" && argCodes.size() == 2)
            {
                return "((char)(" + argCodes[0] + ")[" + argCodes[1] + "])";
            }
            else if (name == "substring" && argCodes.size() == 3)
            {
                return "quill_substring(" + argCodes[0] + ", " + argCodes[1] + ", " + argCodes[2] + ")";
            }
            else if (name == "contains" && argCodes.size() == 2)
            {
                return "(strstr(" + argCodes[0] + ", " + argCodes[1] + ") != NULL)";
            }
            else if (name == "startsWith" && argCodes.size() == 2)
            {
                return "(strncmp(" + argCodes[0] + ", " + argCodes[1] + ", strlen(" + argCodes[1] + ")) == 0)";
            }
            else if (name == "endsWith" && argCodes.size() == 2)
            {
                return "(strlen(" + argCodes[0] + ") >= strlen(" + argCodes[1] + ") && strcmp(" + argCodes[0] +
                       " + strlen(" + argCodes[0] + ") - strlen(" + argCodes[1] + "), " + argCodes[1] + ") == 0)";
            }
            else if (name == "indexOf" && argCodes.size() == 2)
            {
                return "quill_index_of(" + argCodes[0] + ", " + argCodes[1] + ")";
            }
            else if (name == "random_range_int" && argCodes.size() == 2)
            {
                return "quill_random_range_int(" + argCodes[0] + ", " + argCodes[1] + ")";
            }
            else if (name == "print")
            {
                name = "printf";
            }
            else if (name == "len" && argCodes.size() == 1)
            {
                std::string t = argTypes[0];
                if (isMapType(t))
                {
                    std::string rt = mapRuntimeName(mapValType(t));
                    return rt + "_len(&" + argCodes[0] + ")";
                }
                if (isDynamicArrayType(t))
                {
                    std::string pref = dynArrPrefix(arrayElemType(t));
                    return pref + "_len(" + arrayAddress(call->arguments[0], scope, functionReturnTypes) + ")";
                }
                if (isArrayTypeName(t) && !isDynamicArrayType(t))
                {
                    return "(int)(sizeof(" + argCodes[0] + ") / sizeof((" + argCodes[0] + ")[0]))";
                }
                return "(int)strlen(" + argCodes[0] + ")";
            }
            else if (name == "read_file" && argCodes.size() == 1)
            {
                return "quill_read_file(" + argCodes[0] + ")";
            }
            else if (name == "write_file" && argCodes.size() == 2)
            {
                return "quill_write_file(" + argCodes[0] + ", " + argCodes[1] + ")";
            }
            else if (name == "file_exists" && argCodes.size() == 1)
            {
                return "quill_file_exists(" + argCodes[0] + ")";
            }
            else if (name == "argc" && argCodes.size() == 0)
            {
                return "quill_argc";
            }
            else if (name == "argv" && argCodes.size() == 1)
            {
                return "quill_argv_at(" + argCodes[0] + ")";
            }
            else if (name == "map_has" && argCodes.size() == 2)
            {
                std::string t = argTypes[0];
                std::string rt = isMapType(t) ? mapRuntimeName(mapValType(t)) : "quill_map_int";
                return rt + "_has(&" + argCodes[0] + ", " + argCodes[1] + ")";
            }
            else if (name == "map_del" && argCodes.size() == 2)
            {
                std::string t = argTypes[0];
                std::string rt = isMapType(t) ? mapRuntimeName(mapValType(t)) : "quill_map_int";
                return rt + "_del(&" + argCodes[0] + ", " + argCodes[1] + ")";
            }
            else if (name == "map_len" && argCodes.size() == 1)
            {
                std::string t = argTypes[0];
                std::string rt = isMapType(t) ? mapRuntimeName(mapValType(t)) : "quill_map_int";
                return rt + "_len(&" + argCodes[0] + ")";
            }
            else if (name == "push" && argCodes.size() == 2)
            {
                std::string t = argTypes[0];
                if (!isDynamicArrayType(t))
                    return "/* push requires dynamic array */ 0";
                std::string pref = dynArrPrefix(arrayElemType(t));
                return pref + "_push(" + arrayAddress(call->arguments[0], scope, functionReturnTypes) + ", " + argCodes[1] + ")";
            }
            else if (name == "pop" && argCodes.size() == 1)
            {
                std::string t = argTypes[0];
                if (!isDynamicArrayType(t))
                    return "/* pop requires dynamic array */ 0";
                std::string pref = dynArrPrefix(arrayElemType(t));
                return pref + "_pop(" + arrayAddress(call->arguments[0], scope, functionReturnTypes) + ")";
            }
            else if (name == "C_call" && argCodes.size() == 1)
            {
                if (auto *raw = dynamic_cast<LiteralString *>(call->arguments[0]))
                    return raw->value;

                std::string &targetStr = argCodes[0];

                if (targetStr.size() >= 2 && targetStr.front() == '"' && targetStr.back() == '"')
                {
                    targetStr = targetStr.substr(1, targetStr.size() - 2);
                }

                std::string arg = {targetStr};
                return "" + arg + "";
            }
            else if (name == "input" && argCodes.size() == 1)
            {
                if (argTypes[0] == "string")
                    return "char tmp[512];\n    fgets(tmp, sizeof(tmp), stdin);\n    " + argCodes[0] +" = tmp;";
                if (argTypes[0] == "char")
                    return "{ int _c; do { _c = getchar(); } while (_c != EOF && isspace((unsigned char)_c)); " + argCodes[0] + " = (char)_c; }";
                if (argTypes[0] == "number")
                    return "scanf(\"%d\", &" + argCodes[0] + ");";

                if (argTypes[0] == "double")
                    return "scanf(\"%lf\", &" + argCodes[0] + ");";

                return "scanf(\"%d\", &" + argCodes[0] + ");";
            }
            else if (name == "toString" && argCodes.size() == 1)
            {
                if (argTypes[0] == "string" || argTypes[0] == "char")
                    return argCodes[0];
                if (argTypes[0] == "double")
                    return "quill_ftoa(" + argCodes[0] + ")";
                if (argTypes[0] == "bool")
                    return "((" + argCodes[0] + ") ? \"true\" : \"false\")";
                return "quill_itoa(" + argCodes[0] + ")";
            }

            std::string argsList;
            for (std::size_t i = 0; i < argCodes.size(); ++i)
            {
                if (i > 0)
                    argsList += ", ";
                argsList += argCodes[i];
            }
            return name + "(" + argsList + ")";
        }

        return "";
    }

    // ------------------------------------------------------------------
    // import "path.qsc";
    //
    // Splices the named file's entire contents -- functions and
    // top-level code alike -- in place of the import statement, the
    // same way C's #include works. Nested imports inside an imported
    // file resolve relative to *that* file's own directory, not the
    // original file's.
    // ------------------------------------------------------------------

    std::string resolveImportPath(const std::string &baseDir, const std::string &importPath)
    {
        if (!importPath.empty() && importPath[0] == '/')
        {
            return importPath; // already absolute
        }
        if (baseDir.empty())
        {
            return importPath;
        }
        return baseDir + "/" + importPath;
    }

    // `inProgress` tracks paths currently being expanded on this call
    // stack, so A importing B importing A is reported as a clear cycle
    // error instead of recursing forever.
    Program expandImports(const Program &program, const std::string &baseDir, std::set<std::string> &inProgress)
    {
        Program expanded;
        for (Node *node : program.body)
        {
            if (!node)
                continue;

            if (auto *imp = dynamic_cast<ImportStatement *>(node))
            {
                std::string resolvedPath = resolveImportPath(baseDir, imp->path);

                if (inProgress.find(resolvedPath) != inProgress.end())
                {
                    throw std::runtime_error("import cycle detected involving: " + resolvedPath);
                }

                std::ifstream importFile(resolvedPath);
                if (!importFile.is_open())
                {
                    throw std::runtime_error("could not open imported file: " + resolvedPath +
                                             " (imported as \"" + imp->path + "\")");
                }
                std::string importedSource((std::istreambuf_iterator<char>(importFile)),
                                           std::istreambuf_iterator<char>());

                Lexer importLexer(importedSource);
                std::vector<Token> importTokens = importLexer.tokenize();
                Parser importParser(importTokens);
                Program importedProgram = importParser.parse();

                std::size_t slashPos = resolvedPath.find_last_of('/');
                std::string importedBaseDir = slashPos == std::string::npos ? "" : resolvedPath.substr(0, slashPos);

                inProgress.insert(resolvedPath);
                Program expandedImport = expandImports(importedProgram, importedBaseDir, inProgress);
                inProgress.erase(resolvedPath);

                for (Node *importedNode : expandedImport.body)
                {
                    expanded.body.push_back(importedNode);
                }
            }
            else
            {
                expanded.body.push_back(node);
            }
        }
        return expanded;
    }

    // Every Quill function becomes its own C function -- two Function
    // nodes with the same name (whether both written locally, both
    // pulled in via import, or one of each) would silently emit two C
    // functions with identical names, which fails to compile. Catching
    // it here gives a clear Quill-level error instead of a confusing C
    // compiler error pointing at generated code the student never wrote.
    void checkForDuplicateFunctions(const Program &program)
    {
        std::map<std::string, bool> seen;
        for (Node *node : program.body)
        {
            if (auto *fn = dynamic_cast<Function *>(node))
            {
                if (seen.find(fn->name) != seen.end())
                {
                    throw std::runtime_error("duplicate function definition: " + fn->name);
                }
                seen[fn->name] = true;
            }
        }
    }

    class Transpiler
    {
    public:
        std::string transpile(const std::string &source, const std::string &baseDir, const bool typeCheck)
        {
            Lexer lexer(source);
            std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens);
            Program program = parser.parse();

            std::set<std::string> inProgress;
            program = expandImports(program, baseDir, inProgress);
            checkForDuplicateFunctions(program);

            // Built before type-checking runs (not after, as before) so
            // the checker can be handed the transpiler's own, already-
            // correct notion of each function's return type -- both
            // explicitly annotated and inferred -- instead of
            // maintaining a second, weaker copy of that logic.
            std::map<std::string, std::string> functionReturnTypes = buildFunctionReturnTypes(program);
            functionParameterTypes = buildFunctionParameterTypes(program);

            // Type-checking runs on the *expanded* program -- otherwise
            // anything defined only in an imported file would never be
            // checked at all, since the checker would just see an
            // ImportStatement it doesn't know how to interpret.
            if (typeCheck)
            {
                try
                {
                    TypeChecker checker;
                    checker.check(program, functionReturnTypes);
                }
                catch (const std::exception &ex)
                {
                    throw std::runtime_error("Type error: " + std::string(ex.what()));
                }
            }

            std::map<std::string, std::string> globalScope = buildTopLevelScope(program, functionReturnTypes);
            std::map<std::string, std::string> mainScope = buildScope({}, program.body, functionReturnTypes);
            for (const auto &global : globalScope)
                mainScope[global.first] = global.second;

            std::vector<std::string> functionBlocks;
            std::vector<std::string> mainLines;

            std::vector<std::string> preambleLines;

            for (Node *node : program.body)
            {
                if (!node)
                    continue;

                if (auto *fnNode = dynamic_cast<Function *>(node))
                {
                    // Extern functions are signature-only; their implementations
                    // are provided by the linked runtime or library.
                    // Nothing to emit here; buildFunctionReturnTypes()
                    // already picked up its return type from
                    // fnNode->returnType regardless of isExtern, so
                    // calls to it are still typed correctly everywhere
                    // else in the file.
                    if (fnNode->isExtern)
                    {
                        continue;
                    }

                    std::map<std::string, std::string> scope =
                        buildScope(fnNode->params, fnNode->body, functionReturnTypes);
                    for (const auto &global : globalScope)
                        scope.emplace(global);

                    std::string returnType = convertTypeName(functionReturnTypes.at(fnNode->name));

                    std::string params;
                    for (std::size_t i = 0; i < fnNode->params.size(); ++i)
                    {
                        if (i > 0)
                            params += ", ";
                        std::string paramType = fnNode->params[i].type.empty()
                                                    ? "number"
                                                    : normalizeTypeName(fnNode->params[i].type);
                        params += convertTypeName(paramType) +
                                  (isDynamicArrayType(paramType) ? " *" : " ") + fnNode->params[i].name;
                    }
                    if (params.empty())
                        params = "void";

                    std::ostringstream funcStream;
                    funcStream << returnType << " " << fnNode->name << "(" << params << ") {\n";
                    for (Node *bodyStmt : fnNode->body)
                    {
                        std::string stmtCode = astToC(bodyStmt, scope, functionReturnTypes);
                        if (!stmtCode.empty())
                        {
                            if (stmtCode.back() != ';' && stmtCode.back() != '}')
                            {
                                stmtCode += ";";
                            }
                            funcStream << "    " << stmtCode << "\n";
                        }
                    }
                    funcStream << "}\n";
                    functionBlocks.push_back(funcStream.str());
                }
                else if (auto *sdecl = dynamic_cast<StructDecl *>(node))
                {
                    std::string code = astToC(node, mainScope, functionReturnTypes);
                    if (!code.empty())
                        preambleLines.push_back(code);
                }
                else if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
                {
                    std::string typeName = sourceTypeName(globalScope.at(decl->identifier));
                    std::string globalName = "quill_global_" + decl->identifier;
                    if (isMapType(typeName))
                    {
                        preambleLines.push_back(mapRuntimeName(mapValType(typeName)) + " " + globalName + " = {0};");
                    }
                    else if (isDynamicArrayType(typeName))
                    {
                        preambleLines.push_back(dynArrCType(arrayElemType(typeName)) + " " + globalName + " = {0};");
                        if (auto *literal = dynamic_cast<ArrayLiteral *>(decl->value))
                        {
                            std::string prefix = dynArrPrefix(arrayElemType(typeName));
                            for (Node *element : literal->elements)
                                mainLines.push_back(prefix + "_push(&" + globalName + ", " + astToC(element, mainScope, functionReturnTypes) + ");");
                        }
                        else if (decl->value)
                        {
                            mainLines.push_back(globalName + " = " + astToC(decl->value, mainScope, functionReturnTypes) + ";");
                        }
                    }
                    else if (isArrayTypeName(typeName))
                    {
                        std::size_t bracket = typeName.find('[');
                        preambleLines.push_back(convertTypeName(typeName.substr(0, bracket)) + " " + globalName + typeName.substr(bracket) + " = {0};");
                        if (auto *literal = dynamic_cast<ArrayLiteral *>(decl->value))
                        {
                            for (std::size_t i = 0; i < literal->elements.size(); ++i)
                                mainLines.push_back(globalName + "[" + std::to_string(i) + "] = " + astToC(literal->elements[i], mainScope, functionReturnTypes) + ";");
                        }
                    }
                    else
                    {
                        preambleLines.push_back(convertTypeName(typeName) + " " + globalName + " = {0};");
                        if (decl->value)
                            mainLines.push_back(globalName + " = " + astToC(decl->value, mainScope, functionReturnTypes) + ";");
                    }
                }
                else
                {
                    std::string lineCode = astToC(node, mainScope, functionReturnTypes);
                    if (!lineCode.empty())
                    {
                        if (lineCode.back() != ';' && lineCode.back() != '}')
                        {
                            lineCode += ";";
                        }
                        mainLines.push_back(lineCode);
                    }
                }
            }

            std::ostringstream out;
            out << "#include <stdio.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <string.h>\n#include <stdlib.h>\n#include <ctype.h>\n#include <time.h>\n#include <math.h>\n\n";
            out << "static int quill_random_seeded = 0;\nstatic void quill_ensure_random_seeded(void) { if (!quill_random_seeded) { srand((unsigned int)time(NULL)); quill_random_seeded = 1; } }\nstatic int quill_random_int(void) { quill_ensure_random_seeded(); return rand(); }\nstatic int quill_random_range_int(int lo, int hi) { quill_ensure_random_seeded(); if (lo > hi) { int t = lo; lo = hi; hi = t; } if (lo == hi) return lo; return lo + rand() % (hi - lo + 1); }\n\n";
            out << "static char* quill_concat(const char* a, const char* b) {\n    size_t lenA = a ? strlen(a) : 0;\n    size_t lenB = b ? strlen(b) : 0;\n    char* result = (char*)malloc(lenA + lenB + 1);\n    if (!result) { return NULL; }\n    if (lenA > 0) memcpy(result, a, lenA);\n    if (lenB > 0) memcpy(result + lenA, b, lenB);\n    result[lenA + lenB] = '\\0';\n    return result;\n}\n\n";
            out << "static char* quill_dup(const char* s) {\n    size_t len = strlen(s);\n    char* out = (char*)malloc(len + 1);\n    if (!out) { return NULL; }\n    memcpy(out, s, len + 1);\n    return out;\n}\n\n";
            out << "static char* quill_substring(const char* value, int start, int end) {\n    int length = (int)strlen(value);\n    if (start < 0) start = 0;\n    if (start > length) start = length;\n    if (end < start) end = start;\n    if (end > length) end = length;\n    size_t count = (size_t)(end - start);\n    char* result = (char*)malloc(count + 1);\n    if (!result) return NULL;\n    memcpy(result, value + start, count);\n    result[count] = '\\0';\n    return result;\n}\n\nstatic int quill_index_of(const char* value, const char* part) {\n    const char* found = strstr(value, part);\n    return found ? (int)(found - value) : -1;\n}\n\n";
            out << "static char* quill_itoa(long long v) {\n    char buffer[32];\n    snprintf(buffer, sizeof(buffer), \"%lld\", v);\n    return quill_dup(buffer);\n}\n\n";
            out << "static char* quill_ftoa(double v) {\n    char buffer[64];\n    snprintf(buffer, sizeof(buffer), \"%f\", v);\n    return quill_dup(buffer);\n}\n\n";

            out << R"QUILL_ARR(
            typedef struct { int *data; int length; int capacity; } quill_arr_int;
            typedef struct { double *data; int length; int capacity; } quill_arr_double;
            typedef struct { bool *data; int length; int capacity; } quill_arr_bool;
            typedef struct { const char **data; int length; int capacity; } quill_arr_str;

            static quill_arr_int quill_arr_int_new(void) {
                quill_arr_int a; a.data = NULL; a.length = 0; a.capacity = 0; return a;
            }
            static quill_arr_double quill_arr_double_new(void) {
                quill_arr_double a; a.data = NULL; a.length = 0; a.capacity = 0; return a;
            }
            static quill_arr_bool quill_arr_bool_new(void) {
                quill_arr_bool a; a.data = NULL; a.length = 0; a.capacity = 0; return a;
            }
            static quill_arr_str quill_arr_str_new(void) {
                quill_arr_str a; a.data = NULL; a.length = 0; a.capacity = 0; return a;
            }

            static void quill_arr_int_push(quill_arr_int *a, int v) {
                if (a->length >= a->capacity) {
                    int nc = a->capacity == 0 ? 8 : a->capacity * 2;
                    int *nd = (int*)realloc(a->data, (size_t)nc * sizeof(int));
                    if (!nd) return;
                    a->data = nd; a->capacity = nc;
                }
                a->data[a->length++] = v;
            }
            static void quill_arr_double_push(quill_arr_double *a, double v) {
                if (a->length >= a->capacity) {
                    int nc = a->capacity == 0 ? 8 : a->capacity * 2;
                    double *nd = (double*)realloc(a->data, (size_t)nc * sizeof(double));
                    if (!nd) return;
                    a->data = nd; a->capacity = nc;
                }
                a->data[a->length++] = v;
            }
            static void quill_arr_bool_push(quill_arr_bool *a, bool v) {
                if (a->length >= a->capacity) {
                    int nc = a->capacity == 0 ? 8 : a->capacity * 2;
                    bool *nd = (bool*)realloc(a->data, (size_t)nc * sizeof(bool));
                    if (!nd) return;
                    a->data = nd; a->capacity = nc;
                }
                a->data[a->length++] = v;
            }
            static void quill_arr_str_push(quill_arr_str *a, const char *v) {
                if (a->length >= a->capacity) {
                    int nc = a->capacity == 0 ? 8 : a->capacity * 2;
                    const char **nd = (const char**)realloc(a->data, (size_t)nc * sizeof(const char*));
                    if (!nd) return;
                    a->data = nd; a->capacity = nc;
                }
                a->data[a->length++] = v;
            }

            static int quill_arr_int_get(quill_arr_int *a, int i) {
                if (i < 0 || i >= a->length) return 0;
                return a->data[i];
            }
            static double quill_arr_double_get(quill_arr_double *a, int i) {
                if (i < 0 || i >= a->length) return 0.0;
                return a->data[i];
            }
            static bool quill_arr_bool_get(quill_arr_bool *a, int i) {
                if (i < 0 || i >= a->length) return false;
                return a->data[i];
            }
            static const char *quill_arr_str_get(quill_arr_str *a, int i) {
                if (i < 0 || i >= a->length) return "";
                return a->data[i] ? a->data[i] : "";
            }

            static void quill_arr_int_set(quill_arr_int *a, int i, int v) {
                if (i < 0 || i >= a->length) return;
                a->data[i] = v;
            }
            static void quill_arr_double_set(quill_arr_double *a, int i, double v) {
                if (i < 0 || i >= a->length) return;
                a->data[i] = v;
            }
            static void quill_arr_bool_set(quill_arr_bool *a, int i, bool v) {
                if (i < 0 || i >= a->length) return;
                a->data[i] = v;
            }
            static void quill_arr_str_set(quill_arr_str *a, int i, const char *v) {
                if (i < 0 || i >= a->length) return;
                a->data[i] = v;
            }

            static int quill_arr_int_len(quill_arr_int *a) { return a->length; }
            static int quill_arr_double_len(quill_arr_double *a) { return a->length; }
            static int quill_arr_bool_len(quill_arr_bool *a) { return a->length; }
            static int quill_arr_str_len(quill_arr_str *a) { return a->length; }

            static int quill_arr_int_pop(quill_arr_int *a) {
                if (a->length <= 0) return 0;
                return a->data[--a->length];
            }
            static double quill_arr_double_pop(quill_arr_double *a) {
                if (a->length <= 0) return 0.0;
                return a->data[--a->length];
            }
            static bool quill_arr_bool_pop(quill_arr_bool *a) {
                if (a->length <= 0) return false;
                return a->data[--a->length];
            }
            static const char *quill_arr_str_pop(quill_arr_str *a) {
                if (a->length <= 0) return "";
                return a->data[--a->length];
            }
            )QUILL_ARR";

                        out << R"QUILL_IO(
            /* --- argv --- */
            static int quill_argc = 0;
            static char **quill_argv_ptr = 0;
            static const char *quill_argv_at(int i) {
                if (!quill_argv_ptr || i < 0 || i >= quill_argc) return "";
                return quill_argv_ptr[i] ? quill_argv_ptr[i] : "";
            }

            /* --- file I/O --- */
            static char *quill_read_file(const char *path) {
                FILE *f = fopen(path, "rb");
                if (!f) return quill_dup("");
                fseek(f, 0, SEEK_END);
                long n = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (n < 0) { fclose(f); return quill_dup(""); }
                char *buf = (char*)malloc((size_t)n + 1);
                if (!buf) { fclose(f); return quill_dup(""); }
                size_t got = fread(buf, 1, (size_t)n, f);
                buf[got] = '\0';
                fclose(f);
                return buf;
            }
            static int quill_write_file(const char *path, const char *data) {
                FILE *f = fopen(path, "wb");
                if (!f) return -1;
                if (data) fputs(data, f);
                fclose(f);
                return 0;
            }
            static bool quill_file_exists(const char *path) {
                FILE *f = fopen(path, "rb");
                if (!f) return false;
                fclose(f);
                return true;
            }

            /* --- maps (string keys) --- */
            typedef struct quill_map_int_entry {
                char *key;
                int value;
                struct quill_map_int_entry *next;
            } quill_map_int_entry;
            typedef struct { quill_map_int_entry *head; int length; } quill_map_int;

            typedef struct quill_map_str_entry {
                char *key;
                const char *value;
                struct quill_map_str_entry *next;
            } quill_map_str_entry;
            typedef struct { quill_map_str_entry *head; int length; } quill_map_str;

            typedef struct quill_map_double_entry {
                char *key;
                double value;
                struct quill_map_double_entry *next;
            } quill_map_double_entry;
            typedef struct { quill_map_double_entry *head; int length; } quill_map_double;

            typedef struct quill_map_bool_entry {
                char *key;
                bool value;
                struct quill_map_bool_entry *next;
            } quill_map_bool_entry;
            typedef struct { quill_map_bool_entry *head; int length; } quill_map_bool;

            static quill_map_int quill_map_int_new(void) { quill_map_int m; m.head = 0; m.length = 0; return m; }
            static quill_map_str quill_map_str_new(void) { quill_map_str m; m.head = 0; m.length = 0; return m; }
            static quill_map_double quill_map_double_new(void) { quill_map_double m; m.head = 0; m.length = 0; return m; }
            static quill_map_bool quill_map_bool_new(void) { quill_map_bool m; m.head = 0; m.length = 0; return m; }

            static void quill_map_int_set(quill_map_int *m, const char *key, int val) {
                for (quill_map_int_entry *e = m->head; e; e = e->next) {
                    if (strcmp(e->key, key) == 0) { e->value = val; return; }
                }
                quill_map_int_entry *e = (quill_map_int_entry*)malloc(sizeof(*e));
                e->key = quill_dup(key); e->value = val; e->next = m->head; m->head = e; m->length++;
            }
            static int quill_map_int_get(quill_map_int *m, const char *key) {
                for (quill_map_int_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return e->value;
                return 0;
            }
            static bool quill_map_int_has(quill_map_int *m, const char *key) {
                for (quill_map_int_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return true;
                return false;
            }
            static void quill_map_int_del(quill_map_int *m, const char *key) {
                quill_map_int_entry **pp = &m->head;
                while (*pp) {
                    if (strcmp((*pp)->key, key) == 0) {
                        quill_map_int_entry *dead = *pp; *pp = dead->next;
                        free(dead->key); free(dead); m->length--; return;
                    }
                    pp = &(*pp)->next;
                }
            }
            static int quill_map_int_len(quill_map_int *m) { return m->length; }

            static void quill_map_str_set(quill_map_str *m, const char *key, const char *val) {
                for (quill_map_str_entry *e = m->head; e; e = e->next) {
                    if (strcmp(e->key, key) == 0) { e->value = val; return; }
                }
                quill_map_str_entry *e = (quill_map_str_entry*)malloc(sizeof(*e));
                e->key = quill_dup(key); e->value = val; e->next = m->head; m->head = e; m->length++;
            }
            static const char *quill_map_str_get(quill_map_str *m, const char *key) {
                for (quill_map_str_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return e->value ? e->value : "";
                return "";
            }
            static bool quill_map_str_has(quill_map_str *m, const char *key) {
                for (quill_map_str_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return true;
                return false;
            }
            static void quill_map_str_del(quill_map_str *m, const char *key) {
                quill_map_str_entry **pp = &m->head;
                while (*pp) {
                    if (strcmp((*pp)->key, key) == 0) {
                        quill_map_str_entry *dead = *pp; *pp = dead->next;
                        free(dead->key); free(dead); m->length--; return;
                    }
                    pp = &(*pp)->next;
                }
            }
            static int quill_map_str_len(quill_map_str *m) { return m->length; }

            static void quill_map_double_set(quill_map_double *m, const char *key, double val) {
                for (quill_map_double_entry *e = m->head; e; e = e->next) {
                    if (strcmp(e->key, key) == 0) { e->value = val; return; }
                }
                quill_map_double_entry *e = (quill_map_double_entry*)malloc(sizeof(*e));
                e->key = quill_dup(key); e->value = val; e->next = m->head; m->head = e; m->length++;
            }
            static double quill_map_double_get(quill_map_double *m, const char *key) {
                for (quill_map_double_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return e->value;
                return 0.0;
            }
            static bool quill_map_double_has(quill_map_double *m, const char *key) {
                for (quill_map_double_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return true;
                return false;
            }
            static void quill_map_double_del(quill_map_double *m, const char *key) {
                quill_map_double_entry **pp = &m->head;
                while (*pp) {
                    if (strcmp((*pp)->key, key) == 0) {
                        quill_map_double_entry *dead = *pp; *pp = dead->next;
                        free(dead->key); free(dead); m->length--; return;
                    }
                    pp = &(*pp)->next;
                }
            }
            static int quill_map_double_len(quill_map_double *m) { return m->length; }

            static void quill_map_bool_set(quill_map_bool *m, const char *key, bool val) {
                for (quill_map_bool_entry *e = m->head; e; e = e->next) {
                    if (strcmp(e->key, key) == 0) { e->value = val; return; }
                }
                quill_map_bool_entry *e = (quill_map_bool_entry*)malloc(sizeof(*e));
                e->key = quill_dup(key); e->value = val; e->next = m->head; m->head = e; m->length++;
            }
            static bool quill_map_bool_get(quill_map_bool *m, const char *key) {
                for (quill_map_bool_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return e->value;
                return false;
            }
            static bool quill_map_bool_has(quill_map_bool *m, const char *key) {
                for (quill_map_bool_entry *e = m->head; e; e = e->next)
                    if (strcmp(e->key, key) == 0) return true;
                return false;
            }
            static void quill_map_bool_del(quill_map_bool *m, const char *key) {
                quill_map_bool_entry **pp = &m->head;
                while (*pp) {
                    if (strcmp((*pp)->key, key) == 0) {
                        quill_map_bool_entry *dead = *pp; *pp = dead->next;
                        free(dead->key); free(dead); m->length--; return;
                    }
                    pp = &(*pp)->next;
                }
            }
            static int quill_map_bool_len(quill_map_bool *m) { return m->length; }
            )QUILL_IO";

            out << "static char* quill_char_str(char c) {";
            out << "      char* s = (char*)malloc(2);";
            out << "      if (s) { s[0] = c; s[1] = '\\0'; }";
            out << "       return s;";
            out << "}";



            for (const std::string &line : preambleLines)
                out << line << "\n";
            if (!preambleLines.empty())
                out << "\n";

            for (const std::string &fn : functionBlocks)
                out << fn << "\n";
            out << "int main(int argc, char **argv) {\n";
            out << "    quill_argc = argc;\n";
            out << "    quill_argv_ptr = argv;\n";
            for (const std::string &line : mainLines)
                out << "    " << line << "\n";
            out << "    return 0;\n}\n";
            return out.str();
        }
    };

} // namespace

std::string transpileSource(const std::string &source, const std::string &baseDir, bool typeCheck)
{
    Transpiler transpiler;
    return transpiler.transpile(source, baseDir, typeCheck);
}