#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../lexer/lexer.cpp"

std::string transpileSource(const std::string &source, const std::string &baseDir, bool typeCheck);
std::string transpileToLLVM(const std::string &source, const std::string &baseDir, bool typeCheck);

namespace
{
    std::string replaceExtension(const std::string &path, const std::string &newExt)
    {
        std::size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return path + newExt;
        return path.substr(0, dot) + newExt;
    }

    std::string temporaryPath(const std::string &extension)
    {
        auto directory = std::filesystem::temp_directory_path();
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path path = directory / ("quill-" + std::to_string(stamp) + extension);
        return path.string();
    }
}

int main(int argc, char **argv)
{
    bool debug = false;
    bool version = false;
    bool emitSource = false;
    bool typeCheck = false;
    bool format = false;
    std::string backend = "llvm";
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
        {
            backend = "c";
            emitSource = true;
        }
        else if (arg == "--emit-llvm")
        {
            backend = "llvm";
            emitSource = true;
        }
        else if (arg == "--backend")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for --backend\n";
                return 1;
            }
            backend = argv[++i];
            if (backend != "c" && backend != "llvm")
            {
                std::cerr << "unsupported backend: " << backend << " (expected c or llvm)\n";
                return 1;
            }
        }
        else if (arg == "--compile")
            emitSource = false;
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
        std::cerr << "Usage: quill [--backend c|llvm] [--emit-c|--emit-llvm|--compile] [-o output] <input.qsc>\n";
        return 1;
    }

    if (format && backend == "llvm")
    {
        std::cerr << "--format is only supported for the C backend\n";
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
        generatedSource = backend == "llvm"
                      ? transpileToLLVM(source, baseDir, typeCheck)
                      : transpileSource(source, baseDir, typeCheck);
    }
    catch (const std::exception &ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::string binaryPath = outputPath.empty() ? replaceExtension(inputPath, "") : outputPath;
    std::string extension = backend == "llvm" ? ".ll" : ".c";
    std::string sourcePath = emitSource
                                 ? (outputPath.empty() ? replaceExtension(inputPath, extension) : outputPath)
                                 : temporaryPath(extension);

    std::ofstream outputFile(sourcePath);
    if (!outputFile.is_open())
    {
        std::cerr << "failed to open output file: " << sourcePath << "\n";
        return 1;
    }
    outputFile << generatedSource;
    outputFile.close();

    if (debug && emitSource)
        std::cout << "=== OUTPUT ===\n  wrote: " << sourcePath << "\n";

    if (format)
    {
        std::string formatCommand = "clang-format -style=Google -i \"" + sourcePath + "\"";
        int result = std::system(formatCommand.c_str());
        if (result != 0)
        {
            if (!emitSource)
                std::remove(sourcePath.c_str());
            std::cerr << "failed to format generated source: " << sourcePath << "\n";
            return 1;
        }
    }

    if (emitSource)
        return 0;

    std::string compileCommand = backend == "llvm"
                                     ? "clang -Wno-override-module -O2 \"" + sourcePath + "\" -o \"" + binaryPath + "\""
                                     : "gcc -std=c11 -Wall -Wextra -pedantic -Wno-unused-function \"" + sourcePath + "\" -lm -o \"" + binaryPath + "\"";
    int result = std::system(compileCommand.c_str());
    std::remove(sourcePath.c_str());
    if (result != 0)
    {
        std::cerr << "failed to compile generated " << (backend == "llvm" ? "LLVM IR" : "C") << "\n";
        return 1;
    }

    if (debug)
        std::cout << "  compiled to: " << binaryPath << "\n";
    return 0;
}