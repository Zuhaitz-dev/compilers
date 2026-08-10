#pragma once

#include "pascal/AST/AST.hpp"
#include "pascal/AST/ExprNodes.hpp"

namespace Pascal
{

// Assignment statement ('x := expr;' or 'a[i] := expr;').
class AssignStmtAST : public StmtAST
{
public:
    ExprPtr target; // VariableExprAST or IndexExprAST (an lvalue).
    ExprPtr expr;

    AssignStmtAST(ExprPtr target, ExprPtr expr) : target(std::move(target)), expr(std::move(expr))
    {
    }
};

// Compound block ('begin stmt1; stmt2; end').
class CompoundStmtAST : public StmtAST
{
public:
    std::vector<StmtPtr> statements;

    CompoundStmtAST(std::vector<StmtPtr> stmts) : statements(std::move(stmts))
    {
    }
};

// If-Then-Else statement.
class IfStmtAST : public StmtAST
{
public:
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch; // May be nullptr.

    IfStmtAST(ExprPtr cond, StmtPtr then_b, StmtPtr else_b = nullptr)
        : condition(std::move(cond)), then_branch(std::move(then_b)), else_branch(std::move(else_b))
    {
    }
};

// While-Do loop.
class WhileStmtAST : public StmtAST
{
public:
    ExprPtr condition;
    StmtPtr body;

    WhileStmtAST(ExprPtr cond, StmtPtr body) : condition(std::move(cond)), body(std::move(body))
    {
    }
};

// For loop ('for i := start to|downto end do body').
class ForStmtAST : public StmtAST
{
public:
    std::string var_name;
    ExprPtr start;
    ExprPtr end;
    StmtPtr body;
    bool downto = false;

    ForStmtAST(std::string var_name, ExprPtr start, ExprPtr end, StmtPtr body, bool downto)
        : var_name(std::move(var_name)), start(std::move(start)), end(std::move(end)),
          body(std::move(body)), downto(downto)
    {
    }
};

// Repeat-Until loop.
class RepeatStmtAST : public StmtAST
{
public:
    StmtPtr body;
    ExprPtr condition;

    RepeatStmtAST(StmtPtr body, ExprPtr cond) : body(std::move(body)), condition(std::move(cond))
    {
    }
};

// Case statement. Each arm has one or more integer/char constant labels.
class CaseArm
{
public:
    std::vector<TokenValue> labels;
    StmtPtr stmt;
};

class CaseStmtAST : public StmtAST
{
public:
    ExprPtr expr;
    std::vector<CaseArm> arms;
    StmtPtr else_branch; // May be nullptr.

    CaseStmtAST(ExprPtr expr, std::vector<CaseArm> arms, StmtPtr else_branch = nullptr)
        : expr(std::move(expr)), arms(std::move(arms)), else_branch(std::move(else_branch))
    {
    }
};

// Writeln statement ('writeln(a, b, ...);'). Zero or more arguments.
class WritelnStmtAST : public StmtAST
{
public:
    std::vector<ExprPtr> args;

    explicit WritelnStmtAST(std::vector<ExprPtr> args) : args(std::move(args))
    {
    }
};

// Procedure or function call used as a statement.
class CallStmtAST : public StmtAST
{
public:
    std::string callee;
    std::vector<ExprPtr> args;

    CallStmtAST(std::string callee, std::vector<ExprPtr> args)
        : callee(std::move(callee)), args(std::move(args))
    {
    }
};

// A procedure or function declaration.
class SubDeclAST : public ASTNode
{
public:
    std::string name;
    bool is_function = false;
    TypeRef return_type;
    std::vector<ParamDecl> params;
    std::vector<VarDecl> locals;
    StmtPtr body;

    SubDeclAST(std::string name, bool is_function, TypeRef return_type,
               std::vector<ParamDecl> params, std::vector<VarDecl> locals, StmtPtr body)
        : name(std::move(name)), is_function(is_function), return_type(return_type),
          params(std::move(params)), locals(std::move(locals)), body(std::move(body))
    {
    }
};

// Top level Pascal program AST node.
class ProgramAST : public ASTNode
{
public:
    std::string name;
    std::vector<ConstDecl> constants;
    std::vector<VarDecl> declarations;
    std::vector<std::unique_ptr<SubDeclAST>> subprograms;
    std::unique_ptr<CompoundStmtAST> body;

    ProgramAST(std::string program_name, std::vector<ConstDecl> consts, std::vector<VarDecl> decls,
               std::vector<std::unique_ptr<SubDeclAST>> subs,
               std::unique_ptr<CompoundStmtAST> program_body)
        : name(std::move(program_name)), constants(std::move(consts)),
          declarations(std::move(decls)), subprograms(std::move(subs)),
          body(std::move(program_body))
    {
    }
};

} // namespace Pascal
