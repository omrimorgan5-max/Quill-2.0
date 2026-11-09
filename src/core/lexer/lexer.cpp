#pragma once

#include <cctype>
#include <string>
#include <vector>

#include "../types/types.cpp"

class Lexer {
public:
    explicit Lexer(std::string input) : input_(std::move(input)), pos_(0) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;

        while (pos_ < input_.size()) {
            char c = input_[pos_];

            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
                continue;
            }

            if (c == ';') {
                ++pos_;
                continue;
            }

            if (c == '#') {
                ++pos_;
                while (pos_ < input_.size() && input_[pos_] != '\n') {
                    ++pos_;
                }
                tokens.push_back(Token{TokenType::Comment, "comment"});
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokens.push_back(readIdentifier());
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(readNumber());
                continue;
            }

            if (c == '"') {
                tokens.push_back(readString());
                continue;
            }

            if (c == '\'') {
                tokens.push_back(readChar());
                continue;
            }

            if (c == '+' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '+') {
                tokens.push_back(Token{TokenType::Incrementation, "++"});
                pos_ += 2;
                continue;
            }

            if (c == '-' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '-') {
                tokens.push_back(Token{TokenType::Incrementation, "--"});
                pos_ += 2;
                continue;
            }

            if (c == '-' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
                tokens.push_back(Token{TokenType::Operator, "->"});
                pos_ += 2;
                continue;
            }

            if (c == '<' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
                tokens.push_back(Token{TokenType::Operator, "<="});
                pos_ += 2;
                continue;
            }

            if (c == '>' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
                tokens.push_back(Token{TokenType::Operator, ">="});
                pos_ += 2;
                continue;
            }

            if (c == '<' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '<') {
                tokens.push_back(Token{TokenType::Operator, "<<"});
                pos_ += 2;
                continue;
            }

            if (c == '>' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
                tokens.push_back(Token{TokenType::Operator, ">>"});
                pos_ += 2;
                continue;
            }

            if (c == '.' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '.') {
                tokens.push_back(Token{TokenType::Operator, ".."});
                pos_ += 2;
                continue;
            }

            if (c == '=' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
                tokens.push_back(Token{TokenType::Operator, "=="});
                pos_ += 2;
                continue;
            }

            if (c == '!' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
                tokens.push_back(Token{TokenType::Operator, "!="});
                pos_ += 2;
                continue;
            }

            if (c == '&' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '&') {
                tokens.push_back(Token{TokenType::Operator, "&&"});
                pos_ += 2;
                continue;
            }

            if (c == '|' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '|') {
                tokens.push_back(Token{TokenType::Operator, "||"});
                pos_ += 2;
                continue;
            }

            if (c == ':' || c == ',' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '>' || c == '<' || c == '!' || c == '=' || c == '&' || c == '|' || c == '^' || c == '~' || c == '.') {
                tokens.push_back(Token{TokenType::Operator, std::string(1, c)});
                ++pos_;
                continue;
            }

            tokens.push_back(Token{TokenType::Operator, std::string(1, c)});
            ++pos_;
        }

        tokens.push_back(Token{TokenType::EOF_T, ""});
        return tokens;
    }

private:
    Token readIdentifier() {
        size_t start = pos_;
        while (pos_ < input_.size() && (std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) {
            ++pos_;
        }

        std::string text = input_.substr(start, pos_ - start);
        if (text == "let" || text == "mut" || text == "const" || text == "printf" || text == "say" ||
            text == "func" || text == "fn" || text == "if" || text == "else" || text == "elif" || text == "while" ||
            text == "for" || text == "in" || text == "struct" || text == "map" ||
            text == "return" || text == "true" || text == "false" || text == "import" || text == "extern" ||
            text == "break" || text == "continue") {
            return Token{TokenType::Keyword, text};
        }

        return Token{TokenType::Identifier, text};
    }

    Token readNumber() {
        size_t start = pos_;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }

        // Only treat '.' as a float decimal if a digit follows.
        // Otherwise `0..5` (for-range) would be eaten as `0.` + `.` + `5`.
        if (pos_ < input_.size() && input_[pos_] == '.' &&
            pos_ + 1 < input_.size() &&
            std::isdigit(static_cast<unsigned char>(input_[pos_ + 1]))) {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
            return Token{TokenType::Double, input_.substr(start, pos_ - start)};
        }

        return Token{TokenType::Number, input_.substr(start, pos_ - start)};
    }

    Token readString() {
        ++pos_;
        std::string out;

        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
                ++pos_;
                char next = input_[pos_];
                if (next == 'n') out += '\n';
                else if (next == 't') out += '\t';
                else if (next == '"') out += '"';
                else if (next == '\\') out += '\\';
                else out += next;
            } else {
                out += input_[pos_];
            }
            ++pos_;
        }

        if (pos_ < input_.size() && input_[pos_] == '"') {
            ++pos_;
        }

        return Token{TokenType::String, out};
    }

    Token readChar() {
        ++pos_;                          // skip opening '
        std::string out;

        if (pos_ >= input_.size()) {
            // unclosed or empty – decide how you want to error
            return Token{TokenType::Char, ""};
        }

        if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
            ++pos_;
            char next = input_[pos_];
            if (next == 'n')      out = "\n";
            else if (next == 't') out = "\t";
            else if (next == '\'') out = "'";
            else if (next == '\\') out = "\\";
            else if (next == '0')  out = std::string(1, '\0');
            else                   out = std::string(1, next);  // unknown escape → take the char
            ++pos_;
        } else {
            out = std::string(1, input_[pos_]);
            ++pos_;
        }

        // consume closing '
        if (pos_ < input_.size() && input_[pos_] == '\'') {
            ++pos_;
        }

        return Token{TokenType::Char, out};
    }

    std::string input_;
    size_t pos_;
};