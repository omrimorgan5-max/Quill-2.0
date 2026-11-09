#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../lexer/lexer.cpp"

std::string transpileSource(const std::string &source, const std::string &baseDir, bool typeCheck);

namespace
{
    std::string replaceExtension(const std::string &path, const std::string &newExt)
    {
        std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return path + newExt;
        return path.substr(0, dot) + newExt;
    }

    std::string temporaryCPath()
    {
        auto directory = std::filesystem::temp_directory_path();
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path path = directory / ("quill-" + std::to_string(stamp) + ".c");
        return path.string();
    }
}

int main(int argc, char **argv)
{
    bool debug = false;
    bool version = false;
    bool emitC = false;
    bool typeCheck = false;
    bool format = false;
    std::string outputPath;
    std::vector<std::string> args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--debug")
            debug = true;
        else if (arg == "--version" || arg == "-v")
            version = true;
        else if (arg == "--interpret")
        {
        }
        else if (arg == "--emit-c" || arg == "--transpile")
            emitC = true;
        else if (arg == "--compile")
            emitC = false;
        else if (arg == "-tc" || arg == "--typeCheck")
            typeCheck = true;
        else if (arg == "-o" || arg == "--output")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << arg << "\n";
                return 1;
            }
            outputPath = argv[++i];
        }
        else if (arg == "-f" || arg == "--format")
            format = true;
        else
            args.push_back(arg);
    }

    if (version)
    {
        std::cout << "Quill version: 2.4.2\n";
        return 0;
    }

    if (args.size() != 1)
    {
        std::cerr << "Usage: quill [--emit-c|--compile] [-o output] <input.qsc>\n";
        return 1;
    }

    std::string inputPath = args[0];
    std::ifstream input(inputPath);
    if (!input.is_open())
    {
        std::cerr << "failed to open input file: " << inputPath << "\n";
        return 1;
    }

    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::size_t slashPos = inputPath.find_last_of('/');
    std::string baseDir = slashPos == std::string::npos ? "" : inputPath.substr(0, slashPos);

    if (debug)
    {
        Lexer debugLexer(source);
        std::vector<Token> debugTokens = debugLexer.tokenize();
        std::cout << "=== LEXING ===\n";
        for (std::size_t i = 0; i < debugTokens.size() && i < 30; ++i)
            std::cout << "  [" << i << "] type=" << static_cast<int>(debugTokens[i].type) << " value='" << debugTokens[i].value << "'\n";
    }

    std::string generatedSource;
    try
    {
        generatedSource = transpileSource(source, baseDir, typeCheck);
    }
    catch (const std::exception &ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::string binaryPath = outputPath.empty() ? replaceExtension(inputPath, "") : outputPath;
    std::string cPath = emitC
                            ? (outputPath.empty() ? replaceExtension(inputPath, ".c") : outputPath)
                            : temporaryCPath();

    std::ofstream outputFile(cPath);
    if (!outputFile.is_open())
    {
        std::cerr << "failed to open output file: " << cPath << "\n";
        return 1;
    }
    outputFile << generatedSource;
    outputFile.close();

    if (debug && emitC)
        std::cout << "=== OUTPUT ===\n  wrote: " << cPath << "\n";

    if (format)
    {
        std::string formatCommand = "clang-format -style=Google -i \"" + cPath + "\"";
        int result = std::system(formatCommand.c_str());
        if (result != 0)
        {
            if (!emitC)
                std::remove(cPath.c_str());
            std::cerr << "failed to format generated C: " << cPath << "\n";
            return 1;
        }
    }

    if (emitC)
        return 0;

    std::string gccCommand = "gcc -std=c11 -Wall -Wextra -pedantic -Wno-unused-function \"" + cPath + "\" -lm -o \"" + binaryPath + "\"";
    int result = std::system(gccCommand.c_str());
    std::remove(cPath.c_str());
    if (result != 0)
    {
        std::cerr << "failed to compile generated C\n";
        return 1;
    }

    if (debug)
        std::cout << "  compiled to: " << binaryPath << "\n";
    return 0;
}