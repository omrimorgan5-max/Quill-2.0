#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../parser/parser.cpp"
#include "../typechecker/typechecker.cpp"

Program parseQuillProgram(const std::string &source, const std::string &baseDir);
void typeCheckQuillProgram(const Program &program);

namespace
{
    bool isVectorType(const std::string &type)
    {
        return type.size() >= 2 && type.compare(type.size() - 2, 2, "[]") == 0;
    }

    bool isFixedArrayType(const std::string &type)
    {
        return type.size() >= 3 && type.back() == ']' && type[type.size() - 2] != '[' &&
               type.find('[') != std::string::npos;
    }

    std::string fixedArrayElementType(const std::string &type)
    {
        return isFixedArrayType(type) ? type.substr(0, type.find('[')) : type;
    }

    int fixedArrayLength(const std::string &type)
    {
        if (!isFixedArrayType(type))
            throw std::runtime_error("expected a fixed array type, got '" + type + "'");
        return std::stoi(type.substr(type.find('[') + 1, type.size() - type.find('[') - 2));
    }

    bool isMapType(const std::string &type)
    {
        return type.rfind("map[", 0) == 0;
    }

    std::string mapValueType(const std::string &type)
    {
        std::size_t comma = type.find(',');
        std::size_t close = type.rfind(']');
        if (!isMapType(type) || comma == std::string::npos || close == std::string::npos)
            throw std::runtime_error("invalid LLVM map type: " + type);
        return type.substr(comma + 1, close - comma - 1);
    }

    std::string mapSuffix(const std::string &type)
    {
        std::string value = mapValueType(type);
        if (value == "number") return "number";
        if (value == "double") return "double";
        if (value == "bool") return "bool";
        if (value == "string") return "string";
        throw std::runtime_error("LLVM maps do not support value type '" + value + "'");
    }

    bool isMutableContainerType(const std::string &type)
    {
        return isVectorType(type) || isMapType(type);
    }

    std::string vectorElementType(const std::string &type)
    {
        return isVectorType(type) ? type.substr(0, type.size() - 2) : type;
    }

    std::string vectorSuffix(const std::string &type)
    {
        std::string element = vectorElementType(type);
        if (element == "number") return "number";
        if (element == "double") return "double";
        if (element == "bool") return "bool";
        if (element == "string") return "string";
        throw std::runtime_error("LLVM backend does not support vector element type '" + element + "'");
    }

    std::string llvmStructName(const std::string &type)
    {
        std::string name;
        for (unsigned char character : type)
            name += (std::isalnum(character) || character == '_') ? static_cast<char>(character) : '_';
        return "%quill_struct_" + name;
    }

    std::string llvmType(const std::string &type)
    {
        if (isVectorType(type)) return "%quill_vec_" + vectorSuffix(type);
        if (isMapType(type)) return "%quill_map_" + mapSuffix(type);
        if (isFixedArrayType(type))
            return "[" + std::to_string(fixedArrayLength(type)) + " x " + llvmType(fixedArrayElementType(type)) + "]";
        if (type == "number") return "i32";
        if (type == "double") return "double";
        if (type == "bool") return "i1";
        if (type == "char") return "i8";
        if (type == "string") return "i8*";
        if (type == "void") return "void";
        if (type.rfind("map[", 0) != 0 && type.find('[') == std::string::npos)
            return llvmStructName(type);
        throw std::runtime_error("LLVM backend does not support type '" + type + "' yet");
    }

    std::string canonicalType(const std::string &type)
    {
        if (type.empty()) return "number";
        if (isVectorType(type))
        {
            vectorSuffix(type);
            return type;
        }
        if (isMapType(type))
        {
            if (type.substr(4, type.find(',') - 4) != "string")
                throw std::runtime_error("LLVM maps currently require string keys");
            mapSuffix(type);
            return type;
        }
        if (isFixedArrayType(type))
        {
            llvmType(type);
            return type;
        }
        if (type == "number" || type == "double" || type == "bool" || type == "char" ||
            type == "string" || type == "void")
            return type;
        if (type.rfind("map[", 0) != 0 && type.find('[') == std::string::npos)
            return type;
        throw std::runtime_error("LLVM backend does not support type '" + type + "' yet");
    }

    std::string functionSymbol(const std::string &name)
    {
        return "@quill_fn_" + name;
    }

    std::string globalSymbol(const std::string &name)
    {
        return "@quill_global_" + name;
    }

    struct LLVMValue
    {
        std::string type;
        std::string value;
        std::string address;
    };

    struct Binding
    {
        std::string type;
        std::string pointer;
        bool byReference = false;
    };

    struct FunctionSignature
    {
        std::string result;
        std::vector<std::string> parameters;
        bool isExtern = false;
    };

    class LLVMGenerator
    {
    public:
        LLVMGenerator(const Program &program, bool runTypeCheck) : program_(program)
        {
            if (runTypeCheck)
                typeCheckQuillProgram(program_);
            collectStructs();
            collectSignatures();
            collectGlobals();
            inferFunctionReturns();
        }

        std::string generate()
        {
            for (Node *node : program_.body)
            {
                if (auto *fn = dynamic_cast<Function *>(node))
                {
                    if (fn->isExtern)
                        emitExternalDeclaration(*fn);
                    else
                        functions_ << emitFunction(*fn);
                }
            }
            functions_ << emitMain();

            std::ostringstream module;
            module << "source_filename = \"quill-module\"\n\n";
            module << "declare i32 @printf(i8*, ...)\n";
            module << "declare i32 @scanf(i8*, ...)\n";
            module << "declare i8* @fgets(i8*, i32, i8*)\n";
            module << "@stdin = external global i8*\n";
            module << "@quill_argc = internal global i32 0\n";
            module << "@quill_argv = internal global i8** null\n";
            module << "@.quill.empty = private unnamed_addr constant [1 x i8] c\"\\00\", align 1\n";
            module << "@quill_random_seeded = internal global i1 false\n";
            module << "declare i64 @strlen(i8*)\n";
            module << "declare i32 @strcmp(i8*, i8*)\n\n";
            module << "declare i32 @strncmp(i8*, i8*, i64)\n";
            module << "declare i8* @strstr(i8*, i8*)\n\n";
            module << "declare i32 @rand()\n";
            module << "declare void @srand(i32)\n";
            module << "declare i64 @time(i64*)\n\n";
            module << "declare i8* @malloc(i64)\n";
            module << "declare i8* @memcpy(i8*, i8*, i64)\n";
            module << "declare i32 @snprintf(i8*, i64, i8*, ...)\n\n";
            module << "declare i8* @realloc(i8*, i64)\n\n";
            module << "declare i8* @fopen(i8*, i8*)\n";
            module << "declare i32 @fclose(i8*)\n";
            module << "declare i32 @fseek(i8*, i64, i32)\n";
            module << "declare i64 @ftell(i8*)\n";
            module << "declare i64 @fread(i8*, i64, i64, i8*)\n";
            module << "declare i64 @fwrite(i8*, i64, i64, i8*)\n\n";
            module << emitStringRuntime();
            module << emitRandomRuntime();
            module << emitFileRuntime();
            module << emitMapRuntime();
            module << emitVectorRuntime();
            for (const auto &structure : structFields_)
            {
                module << llvmStructName(structure.first) << " = type { ";
                for (std::size_t i = 0; i < structure.second.size(); ++i)
                {
                    if (i) module << ", ";
                    module << llvmType(structure.second[i].second);
                }
                module << " }\n";
            }
            module << "\n";
            for (const auto &global : globalTypes_)
                module << globalSymbol(global.first) << " = global " << llvmType(global.second) << " "
                       << zeroValue(global.second) << ", align " << alignment(global.second) << "\n";
            for (const std::string &literal : stringGlobals_)
                module << literal << "\n";
            module << "\n" << externalDeclarations_.str() << functions_.str();
            return module.str();
        }

    private:
        const Program &program_;
        std::map<std::string, FunctionSignature> signatures_;
        std::map<std::string, std::string> globalTypes_;
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> structFields_;
        std::map<std::string, std::string> stringPointers_;
        std::vector<std::string> stringGlobals_;
        std::ostringstream externalDeclarations_;
        std::ostringstream functions_;
        std::ostringstream body_;
        std::map<std::string, Binding> bindings_;
        std::vector<std::pair<std::string, std::string>> allocas_;
        std::vector<std::string> loopBreakTargets_;
        std::vector<std::string> loopContinueTargets_;
        std::string currentFunction_;
        std::string currentReturnType_;
        std::string currentBlockName_;
        int temporaryIndex_ = 0;
        int blockIndex_ = 0;
        bool blockTerminated_ = false;
        bool inMain_ = false;

        static std::string zeroValue(const std::string &type)
        {
            if (isVectorType(type) || isFixedArrayType(type) || isMapType(type) || (type != "number" && type != "double" && type != "bool" &&
                                       type != "char" && type != "string" && type != "void"))
                return "zeroinitializer";
            if (type == "string") return "null";
            if (type == "double") return "0.000000e+00";
            return "0";
        }

        static int alignment(const std::string &type)
        {
            if (isVectorType(type)) return 8;
            if (isMapType(type)) return 8;
            if (isFixedArrayType(type)) return alignment(fixedArrayElementType(type));
            if (type == "string") return 8;
            if (type == "double") return 8;
            if (type == "char" || type == "bool") return 1;
            return 4;
        }

        static std::string emitVectorRuntime()
        {
            std::ostringstream runtime;
            const std::vector<std::pair<std::string, std::string>> types = {
            {"number", "i32"}, {"double", "double"}, {"bool", "i1"}, {"string", "i8*"}};

            for (const auto &type : types)
            runtime << "%quill_vec_" << type.first << " = type { " << type.second << "*, i32 }\n";
            runtime << "\n";

            for (const auto &type : types)
            {
            const std::string &suffix = type.first;
            const std::string &element = type.second;
            const std::string vector = "%quill_vec_" + suffix;
            const int size = suffix == "bool" ? 1 : (suffix == "string" || suffix == "double" ? 8 : 4);
            const std::string zero = suffix == "double" ? "0.000000e+00" : (suffix == "string" ? "null" : "0");

            runtime << "define internal void @quill_vec_push_" << suffix << "(" << vector << "* %v, "
                << element << " %value) {\nentry:\n"
                << "  %data.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 0\n"
                << "  %len.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 1\n"
                << "  %len = load i32, i32* %len.ptr, align 4\n"
                << "  %data = load " << element << "*, " << element << "** %data.ptr, align 8\n"
                << "  %raw = bitcast " << element << "* %data to i8*\n"
                << "  %next.len = add i32 %len, 1\n"
                << "  %byte.count = mul i32 %next.len, " << size << "\n"
                << "  %byte.count.wide = zext i32 %byte.count to i64\n"
                << "  %new.raw = call i8* @realloc(i8* %raw, i64 %byte.count.wide)\n"
                << "  %new.data = bitcast i8* %new.raw to " << element << "*\n"
                << "  store " << element << "* %new.data, " << element << "** %data.ptr, align 8\n"
                << "  %slot = getelementptr inbounds " << element << ", " << element << "* %new.data, i32 %len\n"
                << "  store " << element << " %value, " << element << "* %slot, align " << size << "\n"
                << "  store i32 %next.len, i32* %len.ptr, align 4\n"
                << "  ret void\n}\n\n";

            runtime << "define internal " << element << " @quill_vec_get_" << suffix << "(" << vector
                << "* %v, i32 %index) {\nentry:\n"
                << "  %len.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 1\n"
                << "  %len = load i32, i32* %len.ptr, align 4\n"
                << "  %nonnegative = icmp sge i32 %index, 0\n"
                << "  %inrange = icmp slt i32 %index, %len\n"
                << "  %valid = and i1 %nonnegative, %inrange\n"
                << "  br i1 %valid, label %load, label %default\nload:\n"
                << "  %data.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 0\n"
                << "  %data = load " << element << "*, " << element << "** %data.ptr, align 8\n"
                << "  %slot = getelementptr inbounds " << element << ", " << element << "* %data, i32 %index\n"
                << "  %value = load " << element << ", " << element << "* %slot, align " << size << "\n"
                << "  br label %merge\ndefault:\n  br label %merge\nmerge:\n"
                << "  %result = phi " << element << " [ %value, %load ], [ " << zero << ", %default ]\n"
                << "  ret " << element << " %result\n}\n\n";

            runtime << "define internal void @quill_vec_set_" << suffix << "(" << vector << "* %v, i32 %index, "
                << element << " %value) {\nentry:\n"
                << "  %len.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 1\n"
                << "  %len = load i32, i32* %len.ptr, align 4\n"
                << "  %nonnegative = icmp sge i32 %index, 0\n"
                << "  %inrange = icmp slt i32 %index, %len\n"
                << "  %valid = and i1 %nonnegative, %inrange\n"
                << "  br i1 %valid, label %store, label %done\nstore:\n"
                << "  %data.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 0\n"
                << "  %data = load " << element << "*, " << element << "** %data.ptr, align 8\n"
                << "  %slot = getelementptr inbounds " << element << ", " << element << "* %data, i32 %index\n"
                << "  store " << element << " %value, " << element << "* %slot, align " << size << "\n"
                << "  br label %done\ndone:\n  ret void\n}\n\n";

            runtime << "define internal i32 @quill_vec_len_" << suffix << "(" << vector << "* %v) {\nentry:\n"
                << "  %len.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 1\n"
                << "  %len = load i32, i32* %len.ptr, align 4\n  ret i32 %len\n}\n\n";

            runtime << "define internal " << element << " @quill_vec_pop_" << suffix << "(" << vector
                << "* %v) {\nentry:\n"
                << "  %len.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 1\n"
                << "  %len = load i32, i32* %len.ptr, align 4\n"
                << "  %has.value = icmp sgt i32 %len, 0\n"
                << "  br i1 %has.value, label %pop, label %empty\npop:\n"
                << "  %index = sub i32 %len, 1\n"
                << "  %data.ptr = getelementptr inbounds " << vector << ", " << vector << "* %v, i32 0, i32 0\n"
                << "  %data = load " << element << "*, " << element << "** %data.ptr, align 8\n"
                << "  %slot = getelementptr inbounds " << element << ", " << element << "* %data, i32 %index\n"
                << "  %value = load " << element << ", " << element << "* %slot, align " << size << "\n"
                << "  store i32 %index, i32* %len.ptr, align 4\n  ret " << element << " %value\n"
                << "empty:\n  ret " << element << " " << zero << "\n}\n\n";
            }
            return runtime.str();
        }

            static std::string emitMapRuntime()
            {
                std::ostringstream runtime;
                const std::vector<std::tuple<std::string, std::string, std::string, int>> types = {
                {"number", "i32", "0", 4},
                {"double", "double", "0.000000e+00", 8},
                {"bool", "i1", "0", 1},
                {"string", "i8*", "null", 8}};

                for (const auto &type : types)
                runtime << "%quill_map_" << std::get<0>(type) << " = type { i8**, " << std::get<1>(type) << "*, i32 }\n";
                runtime << "\n";

                for (const auto &type : types)
                {
                const std::string &suffix = std::get<0>(type);
                const std::string &valueType = std::get<1>(type);
                const std::string &zero = std::get<2>(type);
                const int valueSize = std::get<3>(type);
                const std::string mapType = "%quill_map_" + suffix;

                runtime << "define internal i32 @quill_map_find_" << suffix << "(" << mapType << "* %map, i8* %key) {\nentry:\n"
                    << "  %len.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 2\n"
                    << "  %len = load i32, i32* %len.ptr, align 4\n  br label %loop\nloop:\n"
                    << "  %index = phi i32 [ 0, %entry ], [ %next, %miss ]\n"
                    << "  %more = icmp slt i32 %index, %len\n  br i1 %more, label %compare, label %missing\ncompare:\n"
                    << "  %keys.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 0\n"
                    << "  %keys = load i8**, i8*** %keys.ptr, align 8\n"
                    << "  %key.slot = getelementptr inbounds i8*, i8** %keys, i32 %index\n"
                    << "  %stored.key = load i8*, i8** %key.slot, align 8\n"
                    << "  %comparison = call i32 @strcmp(i8* %stored.key, i8* %key)\n"
                    << "  %equal = icmp eq i32 %comparison, 0\n  br i1 %equal, label %found, label %miss\nmiss:\n"
                    << "  %next = add i32 %index, 1\n  br label %loop\nfound:\n  ret i32 %index\n"
                    << "missing:\n  ret i32 -1\n}\n\n";

                runtime << "define internal " << valueType << " @quill_map_get_" << suffix << "(" << mapType
                    << "* %map, i8* %key) {\nentry:\n"
                    << "  %index = call i32 @quill_map_find_" << suffix << "(" << mapType << "* %map, i8* %key)\n"
                    << "  %found = icmp sge i32 %index, 0\n  br i1 %found, label %load, label %missing\nload:\n"
                    << "  %values.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 1\n"
                    << "  %values = load " << valueType << "*, " << valueType << "** %values.ptr, align 8\n"
                    << "  %slot = getelementptr inbounds " << valueType << ", " << valueType << "* %values, i32 %index\n"
                    << "  %value = load " << valueType << ", " << valueType << "* %slot, align " << valueSize << "\n"
                    << "  br label %merge\nmissing:\n  br label %merge\nmerge:\n"
                    << "  %result = phi " << valueType << " [ %value, %load ], [ " << zero << ", %missing ]\n"
                    << "  ret " << valueType << " %result\n}\n\n";

                runtime << "define internal void @quill_map_set_" << suffix << "(" << mapType
                    << "* %map, i8* %key, " << valueType << " %value) {\nentry:\n"
                    << "  %index = call i32 @quill_map_find_" << suffix << "(" << mapType << "* %map, i8* %key)\n"
                    << "  %exists = icmp sge i32 %index, 0\n  br i1 %exists, label %update, label %append\nupdate:\n"
                    << "  %update.values.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 1\n"
                    << "  %update.values = load " << valueType << "*, " << valueType << "** %update.values.ptr, align 8\n"
                    << "  %update.slot = getelementptr inbounds " << valueType << ", " << valueType << "* %update.values, i32 %index\n"
                    << "  store " << valueType << " %value, " << valueType << "* %update.slot, align " << valueSize << "\n"
                    << "  ret void\nappend:\n"
                    << "  %len.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 2\n"
                    << "  %len = load i32, i32* %len.ptr, align 4\n  %new.len = add i32 %len, 1\n"
                    << "  %keys.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 0\n"
                    << "  %keys = load i8**, i8*** %keys.ptr, align 8\n  %keys.raw = bitcast i8** %keys to i8*\n"
                    << "  %keys.bytes = mul i32 %new.len, 8\n  %keys.bytes.wide = zext i32 %keys.bytes to i64\n"
                    << "  %new.keys.raw = call i8* @realloc(i8* %keys.raw, i64 %keys.bytes.wide)\n"
                    << "  %new.keys = bitcast i8* %new.keys.raw to i8**\n  store i8** %new.keys, i8*** %keys.ptr, align 8\n"
                    << "  %values.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 1\n"
                    << "  %values = load " << valueType << "*, " << valueType << "** %values.ptr, align 8\n"
                    << "  %values.raw = bitcast " << valueType << "* %values to i8*\n"
                    << "  %values.bytes = mul i32 %new.len, " << valueSize << "\n  %values.bytes.wide = zext i32 %values.bytes to i64\n"
                    << "  %new.values.raw = call i8* @realloc(i8* %values.raw, i64 %values.bytes.wide)\n"
                    << "  %new.values = bitcast i8* %new.values.raw to " << valueType << "*\n"
                    << "  store " << valueType << "* %new.values, " << valueType << "** %values.ptr, align 8\n"
                    << "  %key.slot = getelementptr inbounds i8*, i8** %new.keys, i32 %len\n  store i8* %key, i8** %key.slot, align 8\n"
                    << "  %value.slot = getelementptr inbounds " << valueType << ", " << valueType << "* %new.values, i32 %len\n"
                    << "  store " << valueType << " %value, " << valueType << "* %value.slot, align " << valueSize << "\n"
                    << "  store i32 %new.len, i32* %len.ptr, align 4\n  ret void\n}\n\n";

                runtime << "define internal i1 @quill_map_has_" << suffix << "(" << mapType << "* %map, i8* %key) {\nentry:\n"
                    << "  %index = call i32 @quill_map_find_" << suffix << "(" << mapType << "* %map, i8* %key)\n"
                    << "  %result = icmp sge i32 %index, 0\n  ret i1 %result\n}\n\n"
                    << "define internal i32 @quill_map_len_" << suffix << "(" << mapType << "* %map) {\nentry:\n"
                    << "  %len.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 2\n"
                    << "  %len = load i32, i32* %len.ptr, align 4\n  ret i32 %len\n}\n\n";

                runtime << "define internal void @quill_map_del_" << suffix << "(" << mapType << "* %map, i8* %key) {\nentry:\n"
                    << "  %index = call i32 @quill_map_find_" << suffix << "(" << mapType << "* %map, i8* %key)\n"
                    << "  %exists = icmp sge i32 %index, 0\n  br i1 %exists, label %setup, label %done\nsetup:\n"
                    << "  %len.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 2\n"
                    << "  %len = load i32, i32* %len.ptr, align 4\n  %first.source = add i32 %index, 1\n"
                    << "  br label %loop\nloop:\n  %source.index = phi i32 [ %first.source, %setup ], [ %next.source, %copy ]\n"
                    << "  %more = icmp slt i32 %source.index, %len\n  br i1 %more, label %copy, label %finish\ncopy:\n"
                    << "  %destination.index = sub i32 %source.index, 1\n"
                    << "  %keys.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 0\n"
                    << "  %keys = load i8**, i8*** %keys.ptr, align 8\n"
                    << "  %source.key.slot = getelementptr inbounds i8*, i8** %keys, i32 %source.index\n"
                    << "  %source.key = load i8*, i8** %source.key.slot, align 8\n"
                    << "  %destination.key.slot = getelementptr inbounds i8*, i8** %keys, i32 %destination.index\n"
                    << "  store i8* %source.key, i8** %destination.key.slot, align 8\n"
                    << "  %values.ptr = getelementptr inbounds " << mapType << ", " << mapType << "* %map, i32 0, i32 1\n"
                    << "  %values = load " << valueType << "*, " << valueType << "** %values.ptr, align 8\n"
                    << "  %source.value.slot = getelementptr inbounds " << valueType << ", " << valueType << "* %values, i32 %source.index\n"
                    << "  %source.value = load " << valueType << ", " << valueType << "* %source.value.slot, align " << valueSize << "\n"
                    << "  %destination.value.slot = getelementptr inbounds " << valueType << ", " << valueType << "* %values, i32 %destination.index\n"
                    << "  store " << valueType << " %source.value, " << valueType << "* %destination.value.slot, align " << valueSize << "\n"
                    << "  %next.source = add i32 %source.index, 1\n  br label %loop\nfinish:\n"
                    << "  %new.len = sub i32 %len, 1\n  store i32 %new.len, i32* %len.ptr, align 4\n  br label %done\ndone:\n"
                    << "  ret void\n}\n\n";
                }
                return runtime.str();
            }

        static std::string emitStringRuntime()
        {
            return R"LLVM(
define internal i8* @quill_concat(i8* %left, i8* %right) {
entry:
    %left.len = call i64 @strlen(i8* %left)
    %right.len = call i64 @strlen(i8* %right)
    %combined.len = add i64 %left.len, %right.len
    %allocation.len = add i64 %combined.len, 1
    %result = call i8* @malloc(i64 %allocation.len)
    %left.copy = call i8* @memcpy(i8* %result, i8* %left, i64 %left.len)
    %right.start = getelementptr inbounds i8, i8* %result, i64 %left.len
    %right.copy = call i8* @memcpy(i8* %right.start, i8* %right, i64 %right.len)
    %terminator = getelementptr inbounds i8, i8* %result, i64 %combined.len
    store i8 0, i8* %terminator, align 1
    ret i8* %result
}

define internal i8* @quill_substring(i8* %text, i32 %start, i32 %end) {
entry:
    %length.wide = call i64 @strlen(i8* %text)
    %length = trunc i64 %length.wide to i32
    %start.negative = icmp slt i32 %start, 0
    %start.nonnegative = select i1 %start.negative, i32 0, i32 %start
    %start.too.large = icmp sgt i32 %start.nonnegative, %length
    %start.clamped = select i1 %start.too.large, i32 %length, i32 %start.nonnegative
    %end.before.start = icmp slt i32 %end, %start.clamped
    %end.at.least.start = select i1 %end.before.start, i32 %start.clamped, i32 %end
    %end.too.large = icmp sgt i32 %end.at.least.start, %length
    %end.clamped = select i1 %end.too.large, i32 %length, i32 %end.at.least.start
    %count = sub i32 %end.clamped, %start.clamped
    %count.wide = zext i32 %count to i64
    %allocation.size = add i64 %count.wide, 1
    %result = call i8* @malloc(i64 %allocation.size)
    %start.wide = zext i32 %start.clamped to i64
    %source = getelementptr inbounds i8, i8* %text, i64 %start.wide
    %copied = call i8* @memcpy(i8* %result, i8* %source, i64 %count.wide)
    %terminator = getelementptr inbounds i8, i8* %result, i64 %count.wide
    store i8 0, i8* %terminator, align 1
    ret i8* %result
}
)LLVM";
    }

        static std::string emitRandomRuntime()
        {
            return R"LLVM(
define internal void @quill_ensure_random_seeded() {
entry:
    %seeded = load i1, i1* @quill_random_seeded, align 1
    br i1 %seeded, label %done, label %initialize
initialize:
    %now = call i64 @time(i64* null)
    %seed = trunc i64 %now to i32
    call void @srand(i32 %seed)
    store i1 true, i1* @quill_random_seeded, align 1
    br label %done
done:
    ret void
}
)LLVM";
    }

        static std::string emitFileRuntime()
        {
            return R"LLVM(
@.quill.mode.read = private unnamed_addr constant [3 x i8] c"rb\00", align 1
@.quill.mode.write = private unnamed_addr constant [3 x i8] c"wb\00", align 1

define internal i8* @quill_read_file(i8* %path) {
entry:
    %file = call i8* @fopen(i8* %path, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.quill.mode.read, i32 0, i32 0))
    %missing = icmp eq i8* %file, null
    br i1 %missing, label %empty, label %measure
measure:
    %seek.end = call i32 @fseek(i8* %file, i64 0, i32 2)
    %length = call i64 @ftell(i8* %file)
    %seek.start = call i32 @fseek(i8* %file, i64 0, i32 0)
    %invalid.length = icmp slt i64 %length, 0
    br i1 %invalid.length, label %failed, label %read
read:
    %allocation.length = add i64 %length, 1
    %buffer = call i8* @malloc(i64 %allocation.length)
    %read.count = call i64 @fread(i8* %buffer, i64 1, i64 %length, i8* %file)
    %terminator = getelementptr inbounds i8, i8* %buffer, i64 %read.count
    store i8 0, i8* %terminator, align 1
    %close = call i32 @fclose(i8* %file)
    ret i8* %buffer
failed:
    %failed.close = call i32 @fclose(i8* %file)
    br label %empty
empty:
    ret i8* getelementptr inbounds ([1 x i8], [1 x i8]* @.quill.empty, i32 0, i32 0)
}

define internal i32 @quill_write_file(i8* %path, i8* %text) {
entry:
    %file = call i8* @fopen(i8* %path, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.quill.mode.write, i32 0, i32 0))
    %missing = icmp eq i8* %file, null
    br i1 %missing, label %failed, label %write
write:
    %length = call i64 @strlen(i8* %text)
    %written = call i64 @fwrite(i8* %text, i64 1, i64 %length, i8* %file)
    %close = call i32 @fclose(i8* %file)
    ret i32 0
failed:
    ret i32 -1
}

define internal i1 @quill_file_exists(i8* %path) {
entry:
    %file = call i8* @fopen(i8* %path, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.quill.mode.read, i32 0, i32 0))
    %missing = icmp eq i8* %file, null
    br i1 %missing, label %not.exists, label %exists
exists:
    %close = call i32 @fclose(i8* %file)
    ret i1 true
not.exists:
    ret i1 false
}
)LLVM";
    }

        static std::string llvmBytes(const std::string &value)
        {
            static const char hex[] = "0123456789ABCDEF";
            std::ostringstream encoded;
            for (unsigned char byte : value)
            {
                if (byte >= 32 && byte <= 126 && byte != '"' && byte != '\\')
                    encoded << static_cast<char>(byte);
                else
                    encoded << '\\' << hex[byte >> 4] << hex[byte & 15];
            }
            encoded << "\\00";
            return encoded.str();
        }

        static std::string escapeName(const std::string &name)
        {
            std::string result;
            for (unsigned char character : name)
                result += (std::isalnum(character) || character == '_') ? static_cast<char>(character) : '_';
            return result.empty() ? "unnamed" : result;
        }

        static std::string formatDouble(double value)
        {
            std::ostringstream output;
            output << std::scientific << std::setprecision(17) << value;
            return output.str();
        }

        std::string internString(const std::string &value)
        {
            auto found = stringPointers_.find(value);
            if (found != stringPointers_.end())
                return found->second;

            std::string name = "@.quill.str." + std::to_string(stringPointers_.size());
            std::size_t length = value.size() + 1;
            stringGlobals_.push_back(name + " = private unnamed_addr constant [" + std::to_string(length) +
                                     " x i8] c\"" + llvmBytes(value) + "\", align 1");
            std::string pointer = "getelementptr inbounds ([" + std::to_string(length) + " x i8], [" +
                                  std::to_string(length) + " x i8]* " + name + ", i32 0, i32 0)";
            stringPointers_[value] = pointer;
            return pointer;
        }

        std::string newTemporary()
        {
            return "%t" + std::to_string(temporaryIndex_++);
        }

        std::string newBlock(const std::string &prefix)
        {
            return prefix + "." + std::to_string(blockIndex_++);
        }

        void instruction(const std::string &text)
        {
            if (blockTerminated_)
                return;
            body_ << "  " << text << "\n";
        }

        void label(const std::string &name)
        {
            body_ << name << ":\n";
            currentBlockName_ = name;
            blockTerminated_ = false;
        }

        void branch(const std::string &destination)
        {
            instruction("br label %" + destination);
            blockTerminated_ = true;
        }

        void collectSignatures()
        {
            for (Node *node : program_.body)
            {
                auto *fn = dynamic_cast<Function *>(node);
                if (!fn) continue;

                FunctionSignature signature;
                signature.result = fn->returnType.empty() ? "void" : canonicalType(fn->returnType);
                signature.isExtern = fn->isExtern;
                for (const Param &parameter : fn->params)
                    signature.parameters.push_back(canonicalType(parameter.type));
                signatures_[fn->name] = signature;
            }
        }

        void collectStructs()
        {
            for (Node *node : program_.body)
            {
                auto *structure = dynamic_cast<StructDecl *>(node);
                if (!structure) continue;
                std::vector<std::pair<std::string, std::string>> fields;
                for (const StructField &field : structure->fields)
                {
                    std::string type = canonicalType(field.type);
                    llvmType(type);
                    fields.push_back({field.name, type});
                }
                structFields_[structure->name] = fields;
            }
        }

        void collectInferenceScope(const std::vector<Node *> &statements,
                                   std::map<std::string, std::string> &scope)
        {
            for (Node *node : statements)
            {
                if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
                    scope[decl->identifier] = decl->declaredType.empty()
                                                  ? inferType(decl->value, scope)
                                                  : canonicalType(decl->declaredType);
                else if (auto *conditional = dynamic_cast<IfStatement *>(node))
                {
                    collectInferenceScope(conditional->consequent, scope);
                    collectInferenceScope(conditional->alternate, scope);
                }
                else if (auto *loop = dynamic_cast<WhileLoop *>(node))
                    collectInferenceScope(loop->body, scope);
                else if (auto *loop = dynamic_cast<ForLoop *>(node))
                {
                    scope[loop->iterator] = "number";
                    collectInferenceScope(loop->body, scope);
                }
            }
        }

        std::string inferReturns(const std::vector<Node *> &statements,
                                 const std::map<std::string, std::string> &scope)
        {
            std::string inferred = "void";
            for (Node *node : statements)
            {
                std::string candidate = "void";
                if (auto *ret = dynamic_cast<ReturnStatement *>(node))
                    candidate = inferType(ret->value, scope);
                else if (auto *conditional = dynamic_cast<IfStatement *>(node))
                {
                    candidate = inferReturns(conditional->consequent, scope);
                    std::string alternate = inferReturns(conditional->alternate, scope);
                    if (candidate == "void") candidate = alternate;
                    else if (alternate != "void" && candidate != alternate)
                    {
                        if ((candidate == "number" && alternate == "double") ||
                            (candidate == "double" && alternate == "number"))
                            candidate = "double";
                    }
                }
                else if (auto *loop = dynamic_cast<WhileLoop *>(node))
                    candidate = inferReturns(loop->body, scope);
                else if (auto *loop = dynamic_cast<ForLoop *>(node))
                    candidate = inferReturns(loop->body, scope);

                if (inferred == "void")
                    inferred = candidate;
                else if (candidate != "void" && candidate != inferred &&
                         ((candidate == "number" && inferred == "double") ||
                          (candidate == "double" && inferred == "number")))
                    inferred = "double";
            }
            return inferred;
        }

        void inferFunctionReturns()
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                for (Node *node : program_.body)
                {
                    auto *fn = dynamic_cast<Function *>(node);
                    if (!fn || fn->isExtern || !fn->returnType.empty()) continue;
                    std::map<std::string, std::string> scope = globalTypes_;
                    for (std::size_t i = 0; i < fn->params.size(); ++i)
                        scope[fn->params[i].name] = signatures_.at(fn->name).parameters[i];
                    collectInferenceScope(fn->body, scope);
                    std::string inferred = inferReturns(fn->body, scope);
                    if (inferred != "void")
                        signatures_[fn->name].result = inferred;
                }
            }
        }

        std::string inferType(Node *node, const std::map<std::string, std::string> &scope) const
        {
            if (!node) return "void";
            if (dynamic_cast<LiteralInt *>(node)) return "number";
            if (dynamic_cast<LiteralDoudle *>(node)) return "double";
            if (dynamic_cast<LiteralString *>(node)) return "string";
            if (dynamic_cast<LiteralChar *>(node)) return "char";
            if (dynamic_cast<LiteralBool *>(node)) return "bool";
            if (auto *identifier = dynamic_cast<Identifier *>(node))
            {
                auto found = scope.find(identifier->name);
                if (found != scope.end()) return found->second;
                auto global = globalTypes_.find(identifier->name);
                return global == globalTypes_.end() ? "number" : global->second;
            }
            if (auto *literal = dynamic_cast<StructLiteral *>(node))
                return literal->name;
            if (auto *field = dynamic_cast<FieldAccess *>(node))
            {
                auto structure = structFields_.find(inferType(field->object, scope));
                if (structure == structFields_.end())
                    throw std::runtime_error("unknown struct type in LLVM field access");
                for (const auto &entry : structure->second)
                    if (entry.first == field->field) return entry.second;
                throw std::runtime_error("unknown field in LLVM backend: " + field->field);
            }
            if (auto *binary = dynamic_cast<BinaryExpression *>(node))
            {
                if (binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == ">" ||
                    binary->op == "<=" || binary->op == ">=" || binary->op == "&&" || binary->op == "||")
                    return "bool";
                std::string left = inferType(binary->left, scope);
                std::string right = inferType(binary->right, scope);
                if (left == "double" || right == "double") return "double";
                return left;
            }
            if (auto *unary = dynamic_cast<UnaryExpression *>(node))
                return unary->op == "!" ? "bool" : inferType(unary->operand, scope);
            if (auto *call = dynamic_cast<CallExpression *>(node))
            {
                auto found = signatures_.find(call->callee);
                return found == signatures_.end() ? "number" : found->second.result;
            }
            if (auto *literal = dynamic_cast<ArrayLiteral *>(node))
            {
                if (literal->elements.empty()) return "number[]";
                return inferType(literal->elements.front(), scope) + "[]";
            }
            if (auto *index = dynamic_cast<IndexExpression *>(node))
            {
                std::string objectType = inferType(index->object, scope);
                if (isMapType(objectType)) return mapValueType(objectType);
                if (isVectorType(objectType)) return vectorElementType(objectType);
                if (objectType == "string") return "number";
            }
            throw std::runtime_error("LLVM backend cannot infer this expression type yet");
        }

        void collectGlobals()
        {
            for (Node *node : program_.body)
            {
                if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
                {
                    std::string type = decl->declaredType.empty()
                                           ? (decl->value ? inferType(decl->value, {}) : "number")
                                           : canonicalType(decl->declaredType);
                    llvmType(type);
                    globalTypes_[decl->identifier] = type;
                }
            }
        }

        void addAlloca(const std::string &name, const std::string &type)
        {
            if (bindings_.find(name) != bindings_.end())
                return;
            std::string pointer = "%slot." + escapeName(name);
            bindings_[name] = Binding{type, pointer};
            allocas_.push_back({pointer, type});
        }

        void collectLocals(const std::vector<Node *> &statements, std::map<std::string, std::string> &scope)
        {
            for (Node *node : statements)
            {
                if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
                {
                    std::string type = decl->declaredType.empty() ? inferType(decl->value, scope) : canonicalType(decl->declaredType);
                    llvmType(type);
                    scope[decl->identifier] = type;
                    addAlloca(decl->identifier, type);
                }
                else if (auto *ifStmt = dynamic_cast<IfStatement *>(node))
                {
                    collectLocals(ifStmt->consequent, scope);
                    collectLocals(ifStmt->alternate, scope);
                }
                else if (auto *loop = dynamic_cast<WhileLoop *>(node))
                    collectLocals(loop->body, scope);
                else if (auto *loop = dynamic_cast<ForLoop *>(node))
                {
                    std::string iteratorType = "number";
                    if (loop->collection)
                    {
                        std::string collectionType = inferType(loop->collection, scope);
                        if (isVectorType(collectionType)) iteratorType = vectorElementType(collectionType);
                        else if (isFixedArrayType(collectionType)) iteratorType = fixedArrayElementType(collectionType);
                    }
                    scope[loop->iterator] = iteratorType;
                    addAlloca(loop->iterator, iteratorType);
                    collectLocals(loop->body, scope);
                }
            }
        }

        LLVMValue loadBinding(const std::string &name)
        {
            auto local = bindings_.find(name);
            if (local != bindings_.end())
            {
                std::string result = newTemporary();
                instruction(result + " = load " + llvmType(local->second.type) + ", " + llvmType(local->second.type) +
                            "* " + local->second.pointer + ", align " + std::to_string(alignment(local->second.type)));
                    return {local->second.type, result,
                        (isMutableContainerType(local->second.type) || isFixedArrayType(local->second.type)) ? local->second.pointer : ""};
            }
            auto global = globalTypes_.find(name);
            if (global != globalTypes_.end())
            {
                std::string result = newTemporary();
                instruction(result + " = load " + llvmType(global->second) + ", " + llvmType(global->second) +
                            "* " + globalSymbol(name) + ", align " + std::to_string(alignment(global->second)));
                return {global->second, result,
                    (isMutableContainerType(global->second) || isFixedArrayType(global->second)) ? globalSymbol(name) : ""};
            }
            throw std::runtime_error("undefined identifier in LLVM backend: " + name);
        }

        void storeBinding(const std::string &name, LLVMValue value)
        {
            auto local = bindings_.find(name);
            if (local != bindings_.end())
            {
                value = castValue(value, local->second.type);
                instruction("store " + llvmType(value.type) + " " + value.value + ", " + llvmType(value.type) +
                            "* " + local->second.pointer + ", align " + std::to_string(alignment(value.type)));
                return;
            }
            auto global = globalTypes_.find(name);
            if (global != globalTypes_.end())
            {
                value = castValue(value, global->second);
                instruction("store " + llvmType(value.type) + " " + value.value + ", " + llvmType(value.type) +
                            "* " + globalSymbol(name) + ", align " + std::to_string(alignment(value.type)));
                return;
            }
            throw std::runtime_error("assignment to undefined identifier in LLVM backend: " + name);
        }

        std::string bindingAddress(const std::string &name) const
        {
            auto local = bindings_.find(name);
            if (local != bindings_.end()) return local->second.pointer;
            auto global = globalTypes_.find(name);
            if (global != globalTypes_.end()) return globalSymbol(name);
            throw std::runtime_error("undefined vector in LLVM backend: " + name);
        }

        std::string bindingType(const std::string &name) const
        {
            auto local = bindings_.find(name);
            if (local != bindings_.end()) return local->second.type;
            auto global = globalTypes_.find(name);
            if (global != globalTypes_.end()) return global->second;
            throw std::runtime_error("undefined identifier in LLVM backend: " + name);
        }

        std::string vectorAddress(Node *node)
        {
            if (auto *identifier = dynamic_cast<Identifier *>(node))
                return bindingAddress(identifier->name);
            LLVMValue value = emitExpression(node);
            if (value.address.empty())
                throw std::runtime_error("LLVM vector operation requires an addressable vector");
            return value.address;
        }

        LLVMValue castValue(LLVMValue value, const std::string &target)
        {
            if (value.type == target)
                return value;
            if (value.type == "number" && target == "double")
            {
                std::string result = newTemporary();
                instruction(result + " = sitofp i32 " + value.value + " to double");
                return {target, result};
            }
            if (value.type == "double" && target == "number")
            {
                std::string result = newTemporary();
                instruction(result + " = fptosi double " + value.value + " to i32");
                return {target, result};
            }
            if (value.type == "char" && target == "number")
            {
                std::string result = newTemporary();
                instruction(result + " = sext i8 " + value.value + " to i32");
                return {target, result};
            }
            if (value.type == "number" && target == "char")
            {
                std::string result = newTemporary();
                instruction(result + " = trunc i32 " + value.value + " to i8");
                return {target, result};
            }
            if (value.type == "bool" && target == "number")
            {
                std::string result = newTemporary();
                instruction(result + " = zext i1 " + value.value + " to i32");
                return {target, result};
            }
            throw std::runtime_error("LLVM backend cannot convert " + value.type + " to " + target);
        }

        LLVMValue asCondition(LLVMValue value)
        {
            if (value.type == "bool") return value;
            std::string result = newTemporary();
            if (value.type == "double")
                instruction(result + " = fcmp one double " + value.value + ", 0.000000e+00");
            else if (value.type == "number")
                instruction(result + " = icmp ne i32 " + value.value + ", 0");
            else if (value.type == "char")
                instruction(result + " = icmp ne i8 " + value.value + ", 0");
            else
                throw std::runtime_error("LLVM condition must be a scalar value");
            return {"bool", result};
        }

        LLVMValue emitExpression(Node *node, const std::string &expectedType = "")
        {
            if (!node) return {"void", ""};
            if (auto *literal = dynamic_cast<LiteralInt *>(node))
                return {"number", std::to_string(literal->value)};
            if (auto *literal = dynamic_cast<LiteralDoudle *>(node))
                return {"double", formatDouble(literal->value)};
            if (auto *literal = dynamic_cast<LiteralBool *>(node))
                return {"bool", literal->value ? "1" : "0"};
            if (auto *literal = dynamic_cast<LiteralChar *>(node))
                return {"char", std::to_string(static_cast<unsigned char>(literal->value))};
            if (auto *literal = dynamic_cast<LiteralString *>(node))
                return {"string", internString(literal->value)};
            if (auto *literal = dynamic_cast<ArrayLiteral *>(node))
            {
                if (isFixedArrayType(expectedType))
                {
                    int count = fixedArrayLength(expectedType);
                    std::string elementType = fixedArrayElementType(expectedType);
                    if (literal->elements.size() > static_cast<std::size_t>(count))
                        throw std::runtime_error("array literal exceeds its declared size");
                    std::string aggregate = "zeroinitializer";
                    for (std::size_t i = 0; i < literal->elements.size(); ++i)
                    {
                        LLVMValue value = castValue(emitExpression(literal->elements[i]), elementType);
                        std::string updated = newTemporary();
                        instruction(updated + " = insertvalue " + llvmType(expectedType) + " " + aggregate + ", " +
                                    llvmType(elementType) + " " + value.value + ", " + std::to_string(i));
                        aggregate = updated;
                    }
                    return {expectedType, aggregate};
                }
                std::string type = isVectorType(expectedType) ? expectedType : inferType(literal, {});
                std::string elementType = vectorElementType(type);
                std::string suffix = vectorSuffix(type);
                std::string address = newTemporary() + ".vec";
                instruction(address + " = alloca " + llvmType(type) + ", align 8");
                instruction("store " + llvmType(type) + " zeroinitializer, " + llvmType(type) + "* " + address + ", align 8");
                for (Node *element : literal->elements)
                {
                    LLVMValue value = castValue(emitExpression(element), elementType);
                    instruction("call void @quill_vec_push_" + suffix + "(" + llvmType(type) + "* " + address + ", " +
                                llvmType(elementType) + " " + value.value + ")");
                }
                std::string result = newTemporary();
                instruction(result + " = load " + llvmType(type) + ", " + llvmType(type) + "* " + address + ", align 8");
                return {type, result, address};
            }
            if (auto *literal = dynamic_cast<StructLiteral *>(node))
            {
                auto structure = structFields_.find(literal->name);
                if (structure == structFields_.end())
                    throw std::runtime_error("unknown struct type in LLVM literal: " + literal->name);
                std::string aggregate = "zeroinitializer";
                for (std::size_t i = 0; i < literal->fieldNames.size(); ++i)
                {
                    std::size_t fieldIndex = structure->second.size();
                    for (std::size_t field = 0; field < structure->second.size(); ++field)
                    {
                        if (structure->second[field].first == literal->fieldNames[i])
                        {
                            fieldIndex = field;
                            break;
                        }
                    }
                    if (fieldIndex == structure->second.size())
                        throw std::runtime_error("unknown field in LLVM struct literal: " + literal->fieldNames[i]);
                    const std::string &fieldType = structure->second[fieldIndex].second;
                    LLVMValue fieldValue = castValue(emitExpression(literal->fieldValues[i], fieldType), fieldType);
                    std::string updated = newTemporary();
                    instruction(updated + " = insertvalue " + llvmType(literal->name) + " " + aggregate + ", " +
                                llvmType(fieldType) + " " + fieldValue.value + ", " + std::to_string(fieldIndex));
                    aggregate = updated;
                }
                return {literal->name, aggregate};
            }
            if (auto *field = dynamic_cast<FieldAccess *>(node))
            {
                LLVMValue object = emitExpression(field->object);
                auto structure = structFields_.find(object.type);
                if (structure == structFields_.end())
                    throw std::runtime_error("field access requires a struct value in LLVM backend");
                for (std::size_t i = 0; i < structure->second.size(); ++i)
                {
                    if (structure->second[i].first != field->field) continue;
                    const std::string &fieldType = structure->second[i].second;
                    std::string result = newTemporary();
                    instruction(result + " = extractvalue " + llvmType(object.type) + " " + object.value + ", " +
                                std::to_string(i));
                    return {fieldType, result};
                }
                throw std::runtime_error("unknown field in LLVM backend: " + field->field);
            }
            if (auto *identifier = dynamic_cast<Identifier *>(node))
                return loadBinding(identifier->name);
            if (auto *index = dynamic_cast<IndexExpression *>(node))
            {
                LLVMValue object = emitExpression(index->object);
                if (isMapType(object.type))
                {
                    LLVMValue key = emitExpression(index->index);
                    std::string result = newTemporary();
                    instruction(result + " = call " + llvmType(mapValueType(object.type)) + " @quill_map_get_" +
                                mapSuffix(object.type) + "(" + llvmType(object.type) + "* " +
                                (object.address.empty() ? vectorAddress(index->object) : object.address) +
                                ", i8* " + key.value + ")");
                    return {mapValueType(object.type), result};
                }
                LLVMValue offset = castValue(emitExpression(index->index), "number");
                if (isVectorType(object.type))
                {
                    std::string result = newTemporary();
                    instruction(result + " = call " + llvmType(vectorElementType(object.type)) + " @quill_vec_get_" +
                                vectorSuffix(object.type) + "(" + llvmType(object.type) + "* " +
                                (object.address.empty() ? vectorAddress(index->object) : object.address) +
                                ", i32 " + offset.value + ")");
                    return {vectorElementType(object.type), result};
                }
                if (isFixedArrayType(object.type))
                {
                    std::string elementType = fixedArrayElementType(object.type);
                    std::string pointer = newTemporary();
                    instruction(pointer + " = getelementptr inbounds " + llvmType(object.type) + ", " +
                                llvmType(object.type) + "* " + object.address + ", i32 0, i32 " + offset.value);
                    std::string result = newTemporary();
                    instruction(result + " = load " + llvmType(elementType) + ", " + llvmType(elementType) + "* " +
                                pointer + ", align " + std::to_string(alignment(elementType)));
                    return {elementType, result};
                }
                if (object.type == "string")
                {
                    std::string characterPointer = newTemporary();
                    instruction(characterPointer + " = getelementptr inbounds i8, i8* " + object.value + ", i32 " + offset.value);
                    std::string character = newTemporary();
                    instruction(character + " = load i8, i8* " + characterPointer + ", align 1");
                    std::string result = newTemporary();
                    instruction(result + " = zext i8 " + character + " to i32");
                    return {"number", result};
                }
                throw std::runtime_error("LLVM indexing requires a vector or string");
            }
            if (auto *unary = dynamic_cast<UnaryExpression *>(node))
            {
                LLVMValue operand = emitExpression(unary->operand);
                std::string result = newTemporary();
                if (unary->op == "!")
                {
                    LLVMValue condition = asCondition(operand);
                    instruction(result + " = xor i1 " + condition.value + ", true");
                    return {"bool", result};
                }
                if (unary->op == "-")
                {
                    if (operand.type == "double")
                        instruction(result + " = fneg double " + operand.value);
                    else
                    {
                        operand = castValue(operand, "number");
                        instruction(result + " = sub i32 0, " + operand.value);
                        operand.type = "number";
                    }
                    return {operand.type, result};
                }
                if (unary->op == "~")
                {
                    operand = castValue(operand, "number");
                    instruction(result + " = xor i32 " + operand.value + ", -1");
                    return {"number", result};
                }
                throw std::runtime_error("unsupported unary operator in LLVM backend: " + unary->op);
            }
            if (auto *binary = dynamic_cast<BinaryExpression *>(node))
                return emitBinary(*binary);
            if (auto *call = dynamic_cast<CallExpression *>(node))
                return emitCall(*call);
            throw std::runtime_error("LLVM backend does not support this expression yet");
        }

        LLVMValue emitLogical(const BinaryExpression &binary)
        {
            LLVMValue left = asCondition(emitExpression(binary.left));
            std::string originBlock = currentBlockName_;
            std::string rightBlock = newBlock("logic.rhs");
            std::string mergeBlock = newBlock("logic.end");
            if (binary.op == "&&")
                instruction("br i1 " + left.value + ", label %" + rightBlock + ", label %" + mergeBlock);
            else
                instruction("br i1 " + left.value + ", label %" + mergeBlock + ", label %" + rightBlock);
            blockTerminated_ = true;

            label(rightBlock);
            LLVMValue right = asCondition(emitExpression(binary.right));
            std::string rightEndBlock = currentBlockName_;
            branch(mergeBlock);

            label(mergeBlock);
            std::string result = newTemporary();
            std::string shortValue = binary.op == "&&" ? "false" : "true";
            instruction(result + " = phi i1 [ " + shortValue + ", %" + originBlock + " ], [ " +
                        right.value + ", %" + rightEndBlock + " ]");
            return {"bool", result};
        }

        LLVMValue emitBinary(const BinaryExpression &binary)
        {
            if (binary.op == "&&" || binary.op == "||")
                return emitLogical(binary);
            LLVMValue left = emitExpression(binary.left);
            LLVMValue right = emitExpression(binary.right);
            const std::string &op = binary.op;

            if (op == "+" && (left.type == "string" || right.type == "string" || left.type == "char" || right.type == "char"))
            {
                left = asString(left);
                right = asString(right);
                std::string result = newTemporary();
                instruction(result + " = call i8* @quill_concat(i8* " + left.value + ", i8* " + right.value + ")");
                return {"string", result};
            }

            if (left.type == "string" && right.type == "string" && (op == "==" || op == "!="))
            {
                std::string compared = newTemporary();
                instruction(compared + " = call i32 @strcmp(i8* " + left.value + ", i8* " + right.value + ")");
                std::string result = newTemporary();
                instruction(result + " = icmp " + (op == "==" ? "eq" : "ne") + " i32 " + compared + ", 0");
                return {"bool", result};
            }

            if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")
            {
                std::string type = left.type == "double" || right.type == "double" ? "double" : left.type;
                left = castValue(left, type);
                right = castValue(right, type);
                std::string predicate;
                std::string opcode;
                if (type == "double")
                {
                    opcode = "fcmp";
                    if (op == "==") predicate = "oeq";
                    else if (op == "!=") predicate = "une";
                    else if (op == "<") predicate = "olt";
                    else if (op == ">") predicate = "ogt";
                    else if (op == "<=") predicate = "ole";
                    else predicate = "oge";
                }
                else
                {
                    opcode = "icmp";
                    if (op == "==") predicate = "eq";
                    else if (op == "!=") predicate = "ne";
                    else if (op == "<") predicate = "slt";
                    else if (op == ">") predicate = "sgt";
                    else if (op == "<=") predicate = "sle";
                    else predicate = "sge";
                }
                std::string result = newTemporary();
                instruction(result + " = " + opcode + " " + predicate + " " + llvmType(type) + " " +
                            left.value + ", " + right.value);
                return {"bool", result};
            }

            std::string type = left.type == "double" || right.type == "double" ? "double" : "number";
            left = castValue(left, type);
            right = castValue(right, type);
            std::string operation;
            if (type == "double")
            {
                if (op == "+") operation = "fadd";
                else if (op == "-") operation = "fsub";
                else if (op == "*") operation = "fmul";
                else if (op == "/") operation = "fdiv";
                else throw std::runtime_error("unsupported floating-point operator in LLVM backend: " + op);
            }
            else
            {
                if (op == "+") operation = "add";
                else if (op == "-") operation = "sub";
                else if (op == "*") operation = "mul";
                else if (op == "/") operation = "sdiv";
                else if (op == "%") operation = "srem";
                else if (op == "&") operation = "and";
                else if (op == "|") operation = "or";
                else if (op == "^") operation = "xor";
                else if (op == "<<") operation = "shl";
                else if (op == ">>") operation = "ashr";
                else throw std::runtime_error("unsupported integer operator in LLVM backend: " + op);
            }
            std::string result = newTemporary();
            instruction(result + " = " + operation + " " + llvmType(type) + " " + left.value + ", " + right.value);
            return {type, result};
        }

        LLVMValue emitCall(const CallExpression &call)
        {
            if (call.callee == "input" && call.arguments.size() == 1)
            {
                auto *identifier = dynamic_cast<Identifier *>(call.arguments[0]);
                if (!identifier)
                    throw std::runtime_error("input() expects a named variable in LLVM backend");
                std::string type = bindingType(identifier->name);
                std::string address = bindingAddress(identifier->name);
                std::string format;
                if (type == "number") format = "%d";
                else if (type == "double") format = "%lf";
                else if (type == "char") format = " %c";
                else if (type == "string")
                {
                    std::string buffer = newTemporary() + ".input";
                    instruction(buffer + " = call i8* @malloc(i64 512)");
                    std::string inputStream = newTemporary();
                    instruction(inputStream + " = load i8*, i8** @stdin, align 8");
                    instruction("call i8* @fgets(i8* " + buffer + ", i32 512, i8* " + inputStream + ")");
                    instruction("store i8* " + buffer + ", i8** " + address + ", align 8");
                    return {"void", ""};
                }
                else
                    throw std::runtime_error("input() does not support this type in LLVM backend");
                instruction("call i32 (i8*, ...) @scanf(i8* " + internString(format) + ", " + llvmType(type) +
                            "* " + address + ")");
                return {"void", ""};
            }
            if (call.callee == "argc" && call.arguments.empty())
            {
                std::string result = newTemporary();
                instruction(result + " = load i32, i32* @quill_argc, align 4");
                return {"number", result};
            }
            if (call.callee == "argv" && call.arguments.size() == 1)
            {
                LLVMValue index = castValue(emitExpression(call.arguments[0]), "number");
                std::string argcValue = newTemporary();
                instruction(argcValue + " = load i32, i32* @quill_argc, align 4");
                std::string nonnegative = newTemporary();
                instruction(nonnegative + " = icmp sge i32 " + index.value + ", 0");
                std::string inrange = newTemporary();
                instruction(inrange + " = icmp slt i32 " + index.value + ", " + argcValue);
                std::string valid = newTemporary();
                instruction(valid + " = and i1 " + nonnegative + ", " + inrange);
                std::string loadBlock = newBlock("argv.load");
                std::string emptyBlock = newBlock("argv.empty");
                std::string mergeBlock = newBlock("argv.end");
                instruction("br i1 " + valid + ", label %" + loadBlock + ", label %" + emptyBlock);
                blockTerminated_ = true;
                label(loadBlock);
                std::string arguments = newTemporary();
                instruction(arguments + " = load i8**, i8*** @quill_argv, align 8");
                std::string slot = newTemporary();
                instruction(slot + " = getelementptr inbounds i8*, i8** " + arguments + ", i32 " + index.value);
                std::string value = newTemporary();
                instruction(value + " = load i8*, i8** " + slot + ", align 8");
                branch(mergeBlock);
                label(emptyBlock);
                branch(mergeBlock);
                label(mergeBlock);
                std::string result = newTemporary();
                instruction(result + " = phi i8* [ " + value + ", %" + loadBlock + " ], [ " +
                            internString("") + ", %" + emptyBlock + " ]");
                return {"string", result};
            }
            if (call.callee == "read_file" && call.arguments.size() == 1)
            {
                LLVMValue path = emitExpression(call.arguments[0]);
                std::string result = newTemporary();
                instruction(result + " = call i8* @quill_read_file(i8* " + path.value + ")");
                return {"string", result};
            }
            if (call.callee == "write_file" && call.arguments.size() == 2)
            {
                LLVMValue path = emitExpression(call.arguments[0]);
                LLVMValue text = emitExpression(call.arguments[1]);
                std::string result = newTemporary();
                instruction(result + " = call i32 @quill_write_file(i8* " + path.value + ", i8* " + text.value + ")");
                return {"number", result};
            }
            if (call.callee == "file_exists" && call.arguments.size() == 1)
            {
                LLVMValue path = emitExpression(call.arguments[0]);
                std::string result = newTemporary();
                instruction(result + " = call i1 @quill_file_exists(i8* " + path.value + ")");
                return {"bool", result};
            }
            if (call.callee == "random_int" && call.arguments.empty())
            {
                instruction("call void @quill_ensure_random_seeded()");
                std::string result = newTemporary();
                instruction(result + " = call i32 @rand()");
                return {"number", result};
            }
            if (call.callee == "random_range_int" && call.arguments.size() == 2)
            {
                instruction("call void @quill_ensure_random_seeded()");
                LLVMValue low = castValue(emitExpression(call.arguments[0]), "number");
                LLVMValue high = castValue(emitExpression(call.arguments[1]), "number");
                std::string swapped = newTemporary();
                instruction(swapped + " = icmp sgt i32 " + low.value + ", " + high.value);
                std::string minValue = newTemporary();
                std::string maxValue = newTemporary();
                instruction(minValue + " = select i1 " + swapped + ", i32 " + high.value + ", i32 " + low.value);
                instruction(maxValue + " = select i1 " + swapped + ", i32 " + low.value + ", i32 " + high.value);
                std::string random = newTemporary();
                instruction(random + " = call i32 @rand()");
                std::string width = newTemporary();
                instruction(width + " = sub i32 " + maxValue + ", " + minValue);
                std::string inclusiveWidth = newTemporary();
                instruction(inclusiveWidth + " = add i32 " + width + ", 1");
                std::string remainder = newTemporary();
                instruction(remainder + " = srem i32 " + random + ", " + inclusiveWidth);
                std::string result = newTemporary();
                instruction(result + " = add i32 " + minValue + ", " + remainder);
                return {"number", result};
            }
            if (call.callee == "toInt" && call.arguments.size() == 1)
                return castValue(emitExpression(call.arguments[0]), "number");
            if (call.callee == "toDouble" && call.arguments.size() == 1)
                return castValue(emitExpression(call.arguments[0]), "double");
            if (call.callee == "toChar" && call.arguments.size() == 1)
                return castValue(emitExpression(call.arguments[0]), "char");
            if (call.callee == "toString" && call.arguments.size() == 1)
                return asString(emitExpression(call.arguments[0]));
            if (call.callee == "charAt" && call.arguments.size() == 2)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue index = castValue(emitExpression(call.arguments[1]), "number");
                std::string pointer = newTemporary();
                instruction(pointer + " = getelementptr inbounds i8, i8* " + text.value + ", i32 " + index.value);
                std::string result = newTemporary();
                instruction(result + " = load i8, i8* " + pointer + ", align 1");
                return {"char", result};
            }
            if (call.callee == "substring" && call.arguments.size() == 3)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue start = castValue(emitExpression(call.arguments[1]), "number");
                LLVMValue end = castValue(emitExpression(call.arguments[2]), "number");
                std::string result = newTemporary();
                instruction(result + " = call i8* @quill_substring(i8* " + text.value + ", i32 " + start.value +
                            ", i32 " + end.value + ")");
                return {"string", result};
            }
            if (call.callee == "contains" && call.arguments.size() == 2)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue part = emitExpression(call.arguments[1]);
                std::string found = newTemporary();
                instruction(found + " = call i8* @strstr(i8* " + text.value + ", i8* " + part.value + ")");
                std::string result = newTemporary();
                instruction(result + " = icmp ne i8* " + found + ", null");
                return {"bool", result};
            }
            if (call.callee == "startsWith" && call.arguments.size() == 2)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue prefix = emitExpression(call.arguments[1]);
                std::string length = newTemporary();
                instruction(length + " = call i64 @strlen(i8* " + prefix.value + ")");
                std::string comparison = newTemporary();
                instruction(comparison + " = call i32 @strncmp(i8* " + text.value + ", i8* " + prefix.value +
                            ", i64 " + length + ")");
                std::string result = newTemporary();
                instruction(result + " = icmp eq i32 " + comparison + ", 0");
                return {"bool", result};
            }
            if (call.callee == "endsWith" && call.arguments.size() == 2)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue suffix = emitExpression(call.arguments[1]);
                std::string textLength = newTemporary();
                std::string suffixLength = newTemporary();
                instruction(textLength + " = call i64 @strlen(i8* " + text.value + ")");
                instruction(suffixLength + " = call i64 @strlen(i8* " + suffix.value + ")");
                std::string longEnough = newTemporary();
                instruction(longEnough + " = icmp uge i64 " + textLength + ", " + suffixLength);
                std::string start = newTemporary();
                instruction(start + " = sub i64 " + textLength + ", " + suffixLength);
                std::string tail = newTemporary();
                instruction(tail + " = getelementptr i8, i8* " + text.value + ", i64 " + start);
                std::string comparison = newTemporary();
                instruction(comparison + " = call i32 @strcmp(i8* " + tail + ", i8* " + suffix.value + ")");
                std::string matches = newTemporary();
                instruction(matches + " = icmp eq i32 " + comparison + ", 0");
                std::string result = newTemporary();
                instruction(result + " = and i1 " + longEnough + ", " + matches);
                return {"bool", result};
            }
            if (call.callee == "indexOf" && call.arguments.size() == 2)
            {
                LLVMValue text = emitExpression(call.arguments[0]);
                LLVMValue part = emitExpression(call.arguments[1]);
                std::string found = newTemporary();
                instruction(found + " = call i8* @strstr(i8* " + text.value + ", i8* " + part.value + ")");
                std::string exists = newTemporary();
                instruction(exists + " = icmp ne i8* " + found + ", null");
                std::string foundAddress = newTemporary();
                std::string baseAddress = newTemporary();
                instruction(foundAddress + " = ptrtoint i8* " + found + " to i64");
                instruction(baseAddress + " = ptrtoint i8* " + text.value + " to i64");
                std::string distance = newTemporary();
                instruction(distance + " = sub i64 " + foundAddress + ", " + baseAddress);
                std::string index = newTemporary();
                instruction(index + " = trunc i64 " + distance + " to i32");
                std::string result = newTemporary();
                instruction(result + " = select i1 " + exists + ", i32 " + index + ", i32 -1");
                return {"number", result};
            }
            if (call.callee == "len" && call.arguments.size() == 1)
            {
                LLVMValue value = emitExpression(call.arguments[0]);
                if (isMapType(value.type))
                {
                    std::string result = newTemporary();
                    instruction(result + " = call i32 @quill_map_len_" + mapSuffix(value.type) + "(" +
                                llvmType(value.type) + "* " + (value.address.empty() ? vectorAddress(call.arguments[0]) : value.address) + ")");
                    return {"number", result};
                }
                if (isVectorType(value.type))
                {
                    std::string result = newTemporary();
                    instruction(result + " = call i32 @quill_vec_len_" + vectorSuffix(value.type) + "(" +
                                llvmType(value.type) + "* " + (value.address.empty() ? vectorAddress(call.arguments[0]) : value.address) + ")");
                    return {"number", result};
                }
                if (isFixedArrayType(value.type))
                    return {"number", std::to_string(fixedArrayLength(value.type))};
                if (value.type != "string")
                    throw std::runtime_error("LLVM backend currently supports len(string) only");
                std::string length = newTemporary();
                instruction(length + " = call i64 @strlen(i8* " + value.value + ")");
                std::string result = newTemporary();
                instruction(result + " = trunc i64 " + length + " to i32");
                return {"number", result};
            }
            if (call.callee == "map_has" && call.arguments.size() == 2)
            {
                LLVMValue map = emitExpression(call.arguments[0]);
                if (!isMapType(map.type)) throw std::runtime_error("map_has expects a map");
                LLVMValue key = emitExpression(call.arguments[1]);
                std::string result = newTemporary();
                instruction(result + " = call i1 @quill_map_has_" + mapSuffix(map.type) + "(" + llvmType(map.type) +
                            "* " + (map.address.empty() ? vectorAddress(call.arguments[0]) : map.address) +
                            ", i8* " + key.value + ")");
                return {"bool", result};
            }
            if (call.callee == "map_del" && call.arguments.size() == 2)
            {
                LLVMValue map = emitExpression(call.arguments[0]);
                if (!isMapType(map.type)) throw std::runtime_error("map_del expects a map");
                LLVMValue key = emitExpression(call.arguments[1]);
                instruction("call void @quill_map_del_" + mapSuffix(map.type) + "(" + llvmType(map.type) + "* " +
                            (map.address.empty() ? vectorAddress(call.arguments[0]) : map.address) +
                            ", i8* " + key.value + ")");
                return {"void", ""};
            }
            if (call.callee == "map_len" && call.arguments.size() == 1)
            {
                LLVMValue map = emitExpression(call.arguments[0]);
                if (!isMapType(map.type)) throw std::runtime_error("map_len expects a map");
                std::string result = newTemporary();
                instruction(result + " = call i32 @quill_map_len_" + mapSuffix(map.type) + "(" + llvmType(map.type) +
                            "* " + (map.address.empty() ? vectorAddress(call.arguments[0]) : map.address) + ")");
                return {"number", result};
            }
            if (call.callee == "push" && call.arguments.size() == 2)
            {
                LLVMValue vector = emitExpression(call.arguments[0]);
                if (!isVectorType(vector.type))
                    throw std::runtime_error("push expects a vector in LLVM backend");
                std::string elementType = vectorElementType(vector.type);
                LLVMValue element = castValue(emitExpression(call.arguments[1]), elementType);
                instruction("call void @quill_vec_push_" + vectorSuffix(vector.type) + "(" + llvmType(vector.type) +
                            "* " + (vector.address.empty() ? vectorAddress(call.arguments[0]) : vector.address) + ", " +
                            llvmType(element.type) + " " + element.value + ")");
                return {"void", ""};
            }
            if (call.callee == "pop" && call.arguments.size() == 1)
            {
                LLVMValue vector = emitExpression(call.arguments[0]);
                if (!isVectorType(vector.type))
                    throw std::runtime_error("pop expects a vector in LLVM backend");
                std::string result = newTemporary();
                instruction(result + " = call " + llvmType(vectorElementType(vector.type)) + " @quill_vec_pop_" +
                            vectorSuffix(vector.type) + "(" + llvmType(vector.type) + "* " +
                            (vector.address.empty() ? vectorAddress(call.arguments[0]) : vector.address) + ")");
                return {vectorElementType(vector.type), result};
            }

            auto signature = signatures_.find(call.callee);
            if (signature == signatures_.end())
                throw std::runtime_error("unknown function in LLVM backend: " + call.callee);
            if (call.arguments.size() != signature->second.parameters.size())
                throw std::runtime_error("wrong argument count calling '" + call.callee + "'");

            std::ostringstream arguments;
            for (std::size_t i = 0; i < call.arguments.size(); ++i)
            {
                if (i) arguments << ", ";
                const std::string &parameterType = signature->second.parameters[i];
                if (isMutableContainerType(parameterType))
                {
                    LLVMValue value = emitExpression(call.arguments[i], parameterType);
                    std::string address = value.address.empty() ? vectorAddress(call.arguments[i]) : value.address;
                    arguments << llvmType(parameterType) << "* " << address;
                }
                else
                {
                    LLVMValue value = castValue(emitExpression(call.arguments[i]), parameterType);
                    arguments << llvmType(value.type) << " " << value.value;
                }
            }
            std::string result = signature->second.result == "void" ? "" : newTemporary() + " = ";
            std::string symbol = signature->second.isExtern ? "@" + call.callee : functionSymbol(call.callee);
            instruction(result + "call " + llvmType(signature->second.result) + " " + symbol + "(" + arguments.str() + ")");
            return {signature->second.result, result.empty() ? "" : result.substr(0, result.find(" ="))};
        }

        LLVMValue asString(LLVMValue value)
        {
            if (value.type == "string") return value;
            if (value.type == "bool")
            {
                std::string result = newTemporary();
                instruction(result + " = select i1 " + value.value + ", i8* " + internString("true") +
                            ", i8* " + internString("false"));
                return {"string", result};
            }
            if (value.type == "char")
            {
                std::string buffer = newTemporary() + ".char";
                instruction(buffer + " = call i8* @malloc(i64 2)");
                instruction("store i8 " + value.value + ", i8* " + buffer + ", align 1");
                std::string terminator = newTemporary();
                instruction(terminator + " = getelementptr inbounds i8, i8* " + buffer + ", i32 1");
                instruction("store i8 0, i8* " + terminator + ", align 1");
                return {"string", buffer};
            }
            if (value.type == "number" || value.type == "double")
            {
                std::string buffer = newTemporary() + ".text";
                instruction(buffer + " = call i8* @malloc(i64 64)");
                std::string format = value.type == "double" ? "%f" : "%d";
                std::string argumentType = llvmType(value.type);
                instruction("call i32 (i8*, i64, i8*, ...) @snprintf(i8* " + buffer + ", i64 64, i8* " +
                            internString(format) + ", " + argumentType + " " + value.value + ")");
                return {"string", buffer};
            }
            throw std::runtime_error("LLVM backend cannot convert " + value.type + " to string");
        }

        void emitPrint(Node *expression)
        {
            LLVMValue value = emitExpression(expression);
            std::string format;
            if (value.type == "string") format = "%s";
            else if (value.type == "double") format = "%f";
            else if (value.type == "char") format = "%c";
            else format = "%d";

            if (value.type == "bool")
                value = castValue(value, "number");
            else if (value.type == "char")
                value = castValue(value, "number");

            instruction("call i32 (i8*, ...) @printf(i8* " + internString(format) + ", " +
                        llvmType(value.type) + " " + value.value + ")");
        }

        void emitStatements(const std::vector<Node *> &statements)
        {
            for (Node *node : statements)
            {
                if (blockTerminated_) break;
                emitStatement(node);
            }
        }

        void emitStatement(Node *node)
        {
            if (!node) return;
            if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
            {
                if (!decl->value) return;
                storeBinding(decl->identifier, emitExpression(decl->value, bindingType(decl->identifier)));
                return;
            }
            if (auto *assign = dynamic_cast<Assignment *>(node))
            {
                storeBinding(assign->identifier, emitExpression(assign->value, bindingType(assign->identifier)));
                return;
            }
            if (auto *fieldAssignment = dynamic_cast<FieldAssignment *>(node))
            {
                auto *objectIdentifier = dynamic_cast<Identifier *>(fieldAssignment->object);
                if (!objectIdentifier)
                    throw std::runtime_error("LLVM field assignment currently requires a named struct variable");
                LLVMValue object = loadBinding(objectIdentifier->name);
                auto structure = structFields_.find(object.type);
                if (structure == structFields_.end())
                    throw std::runtime_error("field assignment requires a struct value in LLVM backend");
                for (std::size_t i = 0; i < structure->second.size(); ++i)
                {
                    if (structure->second[i].first != fieldAssignment->field) continue;
                    const std::string &fieldType = structure->second[i].second;
                    LLVMValue value = castValue(emitExpression(fieldAssignment->value, fieldType), fieldType);
                    std::string updated = newTemporary();
                    instruction(updated + " = insertvalue " + llvmType(object.type) + " " + object.value + ", " +
                                llvmType(fieldType) + " " + value.value + ", " + std::to_string(i));
                    storeBinding(objectIdentifier->name, {object.type, updated});
                    return;
                }
                throw std::runtime_error("unknown field in LLVM backend: " + fieldAssignment->field);
            }
            if (auto *indexAssignment = dynamic_cast<IndexAssignment *>(node))
            {
                LLVMValue vector = emitExpression(indexAssignment->object);
                if (isMapType(vector.type))
                {
                    LLVMValue key = emitExpression(indexAssignment->index);
                    std::string valueType = mapValueType(vector.type);
                    LLVMValue value = castValue(emitExpression(indexAssignment->value, valueType), valueType);
                    instruction("call void @quill_map_set_" + mapSuffix(vector.type) + "(" + llvmType(vector.type) +
                                "* " + (vector.address.empty() ? vectorAddress(indexAssignment->object) : vector.address) +
                                ", i8* " + key.value + ", " + llvmType(valueType) + " " + value.value + ")");
                    return;
                }
                LLVMValue index = castValue(emitExpression(indexAssignment->index), "number");
                if (isFixedArrayType(vector.type))
                {
                    std::string elementType = fixedArrayElementType(vector.type);
                    LLVMValue value = castValue(emitExpression(indexAssignment->value, elementType), elementType);
                    std::string pointer = newTemporary();
                    instruction(pointer + " = getelementptr inbounds " + llvmType(vector.type) + ", " +
                                llvmType(vector.type) + "* " + vector.address + ", i32 0, i32 " + index.value);
                    instruction("store " + llvmType(elementType) + " " + value.value + ", " +
                                llvmType(elementType) + "* " + pointer + ", align " +
                                std::to_string(alignment(elementType)));
                    return;
                }
                if (!isVectorType(vector.type))
                    throw std::runtime_error("LLVM indexed assignment requires a vector or fixed array");
                std::string elementType = vectorElementType(vector.type);
                LLVMValue value = castValue(emitExpression(indexAssignment->value), elementType);
                instruction("call void @quill_vec_set_" + vectorSuffix(vector.type) + "(" + llvmType(vector.type) +
                            "* " + (vector.address.empty() ? vectorAddress(indexAssignment->object) : vector.address) +
                            ", i32 " + index.value + ", " + llvmType(elementType) + " " + value.value + ")");
                return;
            }
            if (auto *print = dynamic_cast<PrintStatement *>(node))
            {
                emitPrint(print->expression);
                return;
            }
            if (auto *expression = dynamic_cast<ExpressionStatement *>(node))
            {
                emitExpression(expression->expression);
                return;
            }
            if (auto *call = dynamic_cast<CallExpression *>(node))
            {
                emitCall(*call);
                return;
            }
            if (auto *ret = dynamic_cast<ReturnStatement *>(node))
            {
                if (ret->value)
                {
                    LLVMValue value = castValue(emitExpression(ret->value, currentReturnType_), currentReturnType_);
                    instruction("ret " + llvmType(currentReturnType_) + " " + value.value);
                }
                else
                    instruction("ret void");
                blockTerminated_ = true;
                return;
            }
            if (auto *increment = dynamic_cast<Increment *>(node))
            {
                LLVMValue value = loadBinding(increment->identifier);
                value = castValue(value, "number");
                std::string updated = newTemporary();
                instruction(updated + " = add i32 " + value.value + ", 1");
                storeBinding(increment->identifier, {"number", updated});
                return;
            }
            if (auto *decrement = dynamic_cast<Decrement *>(node))
            {
                LLVMValue value = loadBinding(decrement->identifier);
                value = castValue(value, "number");
                std::string updated = newTemporary();
                instruction(updated + " = sub i32 " + value.value + ", 1");
                storeBinding(decrement->identifier, {"number", updated});
                return;
            }
            if (auto *control = dynamic_cast<LoopControl *>(node))
            {
                if (loopBreakTargets_.empty())
                    throw std::runtime_error("loop control outside a loop");
                branch(control->keyword == "break" ? loopBreakTargets_.back() : loopContinueTargets_.back());
                return;
            }
            if (auto *ifStmt = dynamic_cast<IfStatement *>(node))
            {
                emitIf(*ifStmt);
                return;
            }
            if (auto *loop = dynamic_cast<WhileLoop *>(node))
            {
                emitWhile(*loop);
                return;
            }
            if (auto *loop = dynamic_cast<ForLoop *>(node))
            {
                emitFor(*loop);
                return;
            }
            if (dynamic_cast<Function *>(node) || dynamic_cast<StructDecl *>(node))
                return;
            if (dynamic_cast<ImportStatement *>(node))
                throw std::runtime_error("unexpanded import reached LLVM backend");
            throw std::runtime_error("LLVM backend does not support this statement yet");
        }

        void emitIf(const IfStatement &statement)
        {
            LLVMValue condition = asCondition(emitExpression(statement.condition));
            std::string thenBlock = newBlock("if.then");
            std::string elseBlock = newBlock("if.else");
            std::string mergeBlock = newBlock("if.end");
            instruction("br i1 " + condition.value + ", label %" + thenBlock + ", label %" + elseBlock);
            blockTerminated_ = true;

            label(thenBlock);
            emitStatements(statement.consequent);
            if (!blockTerminated_) branch(mergeBlock);

            label(elseBlock);
            emitStatements(statement.alternate);
            if (!blockTerminated_) branch(mergeBlock);

            label(mergeBlock);
        }

        void emitWhile(const WhileLoop &loop)
        {
            std::string conditionBlock = newBlock("while.cond");
            std::string bodyBlock = newBlock("while.body");
            std::string endBlock = newBlock("while.end");
            branch(conditionBlock);
            label(conditionBlock);
            LLVMValue condition = asCondition(emitExpression(loop.condition));
            instruction("br i1 " + condition.value + ", label %" + bodyBlock + ", label %" + endBlock);
            blockTerminated_ = true;
            label(bodyBlock);
            loopBreakTargets_.push_back(endBlock);
            loopContinueTargets_.push_back(conditionBlock);
            emitStatements(loop.body);
            loopContinueTargets_.pop_back();
            loopBreakTargets_.pop_back();
            if (!blockTerminated_) branch(conditionBlock);
            label(endBlock);
        }

        void emitFor(const ForLoop &loop)
        {
            if (loop.collection)
            {
                emitForEach(loop);
                return;
            }
            if (!loop.start || !loop.end)
                throw std::runtime_error("invalid range loop in LLVM backend");
            LLVMValue start = castValue(emitExpression(loop.start), "number");
            LLVMValue end = castValue(emitExpression(loop.end), "number");
            storeBinding(loop.iterator, start);

            std::string conditionBlock = newBlock("for.cond");
            std::string bodyBlock = newBlock("for.body");
            std::string latchBlock = newBlock("for.next");
            std::string endBlock = newBlock("for.end");
            branch(conditionBlock);
            label(conditionBlock);
            LLVMValue current = loadBinding(loop.iterator);
            std::string condition = newTemporary();
            instruction(condition + " = icmp slt i32 " + current.value + ", " + end.value);
            instruction("br i1 " + condition + ", label %" + bodyBlock + ", label %" + endBlock);
            blockTerminated_ = true;
            label(bodyBlock);
            loopBreakTargets_.push_back(endBlock);
            loopContinueTargets_.push_back(latchBlock);
            emitStatements(loop.body);
            loopContinueTargets_.pop_back();
            loopBreakTargets_.pop_back();
            if (!blockTerminated_) branch(latchBlock);
            label(latchBlock);
            current = loadBinding(loop.iterator);
            std::string incremented = newTemporary();
            instruction(incremented + " = add i32 " + current.value + ", 1");
            storeBinding(loop.iterator, {"number", incremented});
            branch(conditionBlock);
            label(endBlock);
        }

        void emitForEach(const ForLoop &loop)
        {
            LLVMValue collection = emitExpression(loop.collection);
            if (!isVectorType(collection.type) && !isFixedArrayType(collection.type) && collection.type != "string")
                throw std::runtime_error("LLVM foreach currently supports vectors, fixed arrays, and strings");

            std::string indexAddress = newTemporary() + ".foreach";
            instruction(indexAddress + " = alloca i32, align 4");
            instruction("store i32 0, i32* " + indexAddress + ", align 4");
            std::string conditionBlock = newBlock("foreach.cond");
            std::string bodyBlock = newBlock("foreach.body");
            std::string latchBlock = newBlock("foreach.next");
            std::string endBlock = newBlock("foreach.end");
            branch(conditionBlock);

            label(conditionBlock);
            std::string index = newTemporary();
            instruction(index + " = load i32, i32* " + indexAddress + ", align 4");
            std::string length;
            if (isVectorType(collection.type))
            {
                length = newTemporary();
                instruction(length + " = call i32 @quill_vec_len_" + vectorSuffix(collection.type) + "(" +
                            llvmType(collection.type) + "* " + collection.address + ")");
            }
            else if (isFixedArrayType(collection.type))
            {
                length = std::to_string(fixedArrayLength(collection.type));
            }
            else
            {
                std::string lengthWide = newTemporary();
                instruction(lengthWide + " = call i64 @strlen(i8* " + collection.value + ")");
                length = newTemporary();
                instruction(length + " = trunc i64 " + lengthWide + " to i32");
            }
            std::string condition = newTemporary();
            instruction(condition + " = icmp slt i32 " + index + ", " + length);
            instruction("br i1 " + condition + ", label %" + bodyBlock + ", label %" + endBlock);
            blockTerminated_ = true;

            label(bodyBlock);
            LLVMValue element;
            if (isVectorType(collection.type))
            {
                std::string value = newTemporary();
                std::string elementType = vectorElementType(collection.type);
                instruction(value + " = call " + llvmType(elementType) + " @quill_vec_get_" +
                            vectorSuffix(collection.type) + "(" + llvmType(collection.type) + "* " +
                            collection.address + ", i32 " + index + ")");
                element = {elementType, value};
            }
            else if (isFixedArrayType(collection.type))
            {
                std::string elementType = fixedArrayElementType(collection.type);
                std::string pointer = newTemporary();
                instruction(pointer + " = getelementptr inbounds " + llvmType(collection.type) + ", " +
                            llvmType(collection.type) + "* " + collection.address + ", i32 0, i32 " + index);
                std::string value = newTemporary();
                instruction(value + " = load " + llvmType(elementType) + ", " + llvmType(elementType) + "* " +
                            pointer + ", align " + std::to_string(alignment(elementType)));
                element = {elementType, value};
            }
            else
            {
                std::string pointer = newTemporary();
                instruction(pointer + " = getelementptr inbounds i8, i8* " + collection.value + ", i32 " + index);
                std::string value = newTemporary();
                instruction(value + " = load i8, i8* " + pointer + ", align 1");
                std::string widened = newTemporary();
                instruction(widened + " = zext i8 " + value + " to i32");
                element = {"number", widened};
            }
            storeBinding(loop.iterator, element);
            loopBreakTargets_.push_back(endBlock);
            loopContinueTargets_.push_back(latchBlock);
            emitStatements(loop.body);
            loopContinueTargets_.pop_back();
            loopBreakTargets_.pop_back();
            if (!blockTerminated_) branch(latchBlock);

            label(latchBlock);
            std::string currentIndex = newTemporary();
            instruction(currentIndex + " = load i32, i32* " + indexAddress + ", align 4");
            std::string nextIndex = newTemporary();
            instruction(nextIndex + " = add i32 " + currentIndex + ", 1");
            instruction("store i32 " + nextIndex + ", i32* " + indexAddress + ", align 4");
            branch(conditionBlock);
            label(endBlock);
        }

        std::string emitFunction(const Function &function)
        {
            std::ostringstream savedBody;
            savedBody.swap(body_);
            auto savedBindings = bindings_;
            auto savedAllocas = allocas_;
            auto savedBreaks = loopBreakTargets_;
            auto savedContinues = loopContinueTargets_;
            std::string savedFunction = currentFunction_;
            std::string savedReturn = currentReturnType_;
            std::string savedBlockName = currentBlockName_;
            int savedTemporary = temporaryIndex_;
            int savedBlock = blockIndex_;
            bool savedTerminated = blockTerminated_;

            bindings_.clear();
            allocas_.clear();
            loopBreakTargets_.clear();
            loopContinueTargets_.clear();
            currentFunction_ = function.name;
            currentReturnType_ = signatures_.at(function.name).result;
            currentBlockName_ = "entry";
            temporaryIndex_ = 0;
            blockIndex_ = 0;
            blockTerminated_ = false;

            std::map<std::string, std::string> scope = globalTypes_;
            std::ostringstream parameters;
            for (std::size_t i = 0; i < function.params.size(); ++i)
            {
                std::string type = signatures_.at(function.name).parameters[i];
                scope[function.params[i].name] = type;
                if (i) parameters << ", ";
                if (isMutableContainerType(type))
                {
                    std::string pointer = "%arg" + std::to_string(i);
                    bindings_[function.params[i].name] = Binding{type, pointer, true};
                    parameters << llvmType(type) << "* " << pointer;
                }
                else
                {
                    addAlloca(function.params[i].name, type);
                    parameters << llvmType(type) << " %arg" << i;
                }
            }
            collectLocals(function.body, scope);

            body_ << "define " << llvmType(currentReturnType_) << " " << functionSymbol(function.name)
                  << "(" << parameters.str() << ") {\nentry:\n";
            for (const auto &allocation : allocas_)
                body_ << "  " << allocation.first << " = alloca " << llvmType(allocation.second)
                      << ", align " << alignment(allocation.second) << "\n";
            for (std::size_t i = 0; i < function.params.size(); ++i)
            {
                const Param &parameter = function.params[i];
                const Binding &binding = bindings_.at(parameter.name);
                if (binding.byReference)
                    continue;
                body_ << "  store " << llvmType(binding.type) << " %arg" << i << ", " << llvmType(binding.type)
                      << "* " << binding.pointer << ", align " << alignment(binding.type) << "\n";
            }
            emitStatements(function.body);
            if (!blockTerminated_)
            {
                if (currentReturnType_ == "void")
                    instruction("ret void");
                else
                    instruction("ret " + llvmType(currentReturnType_) + " " + zeroValue(currentReturnType_));
                blockTerminated_ = true;
            }
            body_ << "}\n\n";
            std::string result = body_.str();

            body_.str("");
            body_.clear();
            body_.swap(savedBody);
            bindings_ = std::move(savedBindings);
            allocas_ = std::move(savedAllocas);
            loopBreakTargets_ = std::move(savedBreaks);
            loopContinueTargets_ = std::move(savedContinues);
            currentFunction_ = std::move(savedFunction);
            currentReturnType_ = std::move(savedReturn);
            currentBlockName_ = std::move(savedBlockName);
            temporaryIndex_ = savedTemporary;
            blockIndex_ = savedBlock;
            blockTerminated_ = savedTerminated;
            return result;
        }

        std::string emitMain()
        {
            std::ostringstream savedBody;
            savedBody.swap(body_);
            auto savedBindings = bindings_;
            auto savedAllocas = allocas_;
            std::string savedFunction = currentFunction_;
            std::string savedReturn = currentReturnType_;
            std::string savedBlockName = currentBlockName_;
            int savedTemporary = temporaryIndex_;
            int savedBlock = blockIndex_;
            bool savedTerminated = blockTerminated_;

            bindings_.clear();
            allocas_.clear();
            currentFunction_ = "main";
            currentReturnType_ = "number";
            currentBlockName_ = "entry";
            temporaryIndex_ = 0;
            blockIndex_ = 0;
            blockTerminated_ = false;
            inMain_ = true;

            std::map<std::string, std::string> mainScope = globalTypes_;
            for (Node *node : program_.body)
            {
                if (dynamic_cast<IfStatement *>(node) || dynamic_cast<WhileLoop *>(node) || dynamic_cast<ForLoop *>(node))
                {
                    std::vector<Node *> nested{node};
                    collectLocals(nested, mainScope);
                }
            }

            body_ << "define i32 @main(i32 %argc, i8** %argv) {\nentry:\n";
            instruction("store i32 %argc, i32* @quill_argc, align 4");
            instruction("store i8** %argv, i8*** @quill_argv, align 8");
            for (const auto &allocation : allocas_)
                body_ << "  " << allocation.first << " = alloca " << llvmType(allocation.second)
                      << ", align " << alignment(allocation.second) << "\n";
            for (Node *node : program_.body)
            {
                if (auto *decl = dynamic_cast<VariableDeclaration *>(node))
                {
                    if (decl->value)
                        storeBinding(decl->identifier, emitExpression(decl->value, bindingType(decl->identifier)));
                }
                else if (dynamic_cast<Function *>(node) || dynamic_cast<StructDecl *>(node))
                    continue;
                else
                    emitStatement(node);
            }
            if (!blockTerminated_)
                instruction("ret i32 0");
            body_ << "}\n";
            std::string result = body_.str();

            body_.str("");
            body_.clear();
            body_.swap(savedBody);
            bindings_ = std::move(savedBindings);
            allocas_ = std::move(savedAllocas);
            currentFunction_ = std::move(savedFunction);
            currentReturnType_ = std::move(savedReturn);
            currentBlockName_ = std::move(savedBlockName);
            temporaryIndex_ = savedTemporary;
            blockIndex_ = savedBlock;
            blockTerminated_ = savedTerminated;
            inMain_ = false;
            return result;
        }

        void emitExternalDeclaration(const Function &function)
        {
            const FunctionSignature &signature = signatures_.at(function.name);
            externalDeclarations_ << "declare " << llvmType(signature.result) << " @" << function.name << "(";
            for (std::size_t i = 0; i < signature.parameters.size(); ++i)
            {
                if (i) externalDeclarations_ << ", ";
                externalDeclarations_ << llvmType(signature.parameters[i]);
            }
            externalDeclarations_ << ")\n";
        }
    };
}

std::string transpileToLLVM(const std::string &source, const std::string &baseDir, bool typeCheck)
{
    Program program = parseQuillProgram(source, baseDir);
    LLVMGenerator generator(program, typeCheck);
    return generator.generate();
}