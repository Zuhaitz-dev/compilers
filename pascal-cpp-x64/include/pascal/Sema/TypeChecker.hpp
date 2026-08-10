#pragma once

#include "pascal/AST/AST.hpp"
#include "pascal/AST/ExprNodes.hpp"
#include "pascal/AST/StmtNodes.hpp"
#include "pascal/Sema/SymbolTable.hpp"
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Pascal
{

class TypeChecker
{
public:
    void check_program(const ProgramAST &program);

private:
    SymbolTable symbols_;
    const SubDeclAST *current_function_ = nullptr;
    std::unordered_map<std::string, const SubDeclAST *> subprograms_;

    static TokenType type_of_token_value(const TokenValue &value);
    static bool is_numeric(TokenType t);
    static TokenType promote(TokenType a, TokenType b);
    bool is_lvalue(const ExprAST &expr) const;
    static TypeRef require_scalar(const TypeRef &tf, std::string_view what);
    static void require_boolean(const TypeRef &tf, std::string_view what);

    void check_subprogram(const SubDeclAST &sub);
    TypeRef check_expression(const ExprAST &expr);
    void check_call_args(const std::vector<ParamDecl> &params, const std::vector<ExprPtr> &args,
                         std::string_view callee);
    void check_statement(const StmtAST &stmt);
};

} // namespace Pascal
