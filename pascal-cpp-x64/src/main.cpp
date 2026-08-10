#include "pascal/Lexer/Lexer.hpp"
#include "pascal/Parser/Parser.hpp"
#include "pascal/Sema/TypeChecker.hpp"
#include "pascal/CodeGen/CodeGen.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void usage(const char *prog)
{
    std::cerr << "Usage: " << prog << " <file.pas> [--ir] [-o output.o]\n";
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 2;
    }

    std::string input_file;
    std::string output_file = "output.o";
    bool print_ir = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--ir")
        {
            print_ir = true;
        }
        else if (arg == "-o" && i + 1 < argc)
        {
            output_file = argv[++i];
        }
        else if (!arg.empty() && arg[0] != '-')
        {
            input_file = arg;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            usage(argv[0]);
            return 2;
        }
    }

    if (input_file.empty())
    {
        usage(argv[0]);
        return 2;
    }

    std::ifstream file(input_file);
    if (!file)
    {
        std::cerr << "Error: could not open '" << input_file << "'\n";
        return 2;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    try
    {
        Pascal::Lexer lexer(source);
        Pascal::Parser parser(lexer);
        auto ast = parser.parse_program();

        Pascal::TypeChecker sema;
        sema.check_program(*ast);

        Pascal::CodeGenerator codegen;
        auto module = codegen.emit_program(*ast);

        if (print_ir)
        {
            codegen.print_ir(*module);
        }

        codegen.emit_object_file(*module, output_file);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Compiler Error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
