#pragma once

#include "pascal/Lexer/Lexer.hpp"
#include "pascal/AST/AST.hpp"
#include "pascal/AST/ExprNodes.hpp"
#include "pascal/AST/StmtNodes.hpp"
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>

namespace Pascal
{

class Parser
{
public:
    explicit Parser(Lexer &lexer);

    std::unique_ptr<ProgramAST> parse_program();

private:
    Lexer &lexer_;
    Token current_token_;

    void advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token consume(TokenType type, std::string_view error_message);
    [[noreturn]] void error(std::string_view message) const;

    // Types.
    TokenType parse_scalar_type();
    TypeRef parse_type();

    // Declarations.
    std::vector<ConstDecl> parse_const_declarations();
    TokenValue parse_const_value();
    std::vector<VarDecl> parse_var_declarations();
    std::vector<ParamDecl> parse_param_list();
    std::unique_ptr<SubDeclAST> parse_subprogram();

    // Statements.
    StmtPtr parse_statement();
    StmtPtr parse_assignment_or_call();
    StmtPtr parse_compound();
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_for();
    StmtPtr parse_repeat();
    StmtPtr parse_case();
    StmtPtr parse_writeln();

    // Expressions (Pascal precedence climbing).
    ExprPtr parse_expression();
    ExprPtr parse_comparison();
    ExprPtr parse_additive();
    ExprPtr parse_term();
    ExprPtr parse_unary();
    ExprPtr parse_primary();
};

} // namespace Pascal
