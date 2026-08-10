#pragma once

#include "pascal/AST/AST.hpp"

namespace Pascal
{

// For Integer, Real, Boolean, Char, and String literals.
class LiteralExprAST : public ExprAST
{
public:
    TokenValue value;

    explicit LiteralExprAST(TokenValue val) : value(std::move(val))
    {
    }
};

// Variable references (such as 'x').
class VariableExprAST : public ExprAST
{
public:
    std::string name;

    explicit VariableExprAST(std::string var_name) : name(std::move(var_name))
    {
    }
};

// Binary expressions (such as 'x >= 5' or 'a div b').
class BinaryExprAST : public ExprAST
{
public:
    TokenType op;
    ExprPtr lhs;
    ExprPtr rhs;

    BinaryExprAST(TokenType op, ExprPtr lhs, ExprPtr rhs)
        : op(op), lhs(std::move(lhs)), rhs(std::move(rhs))
    {
    }
};

// Unary expressions ('-x' or 'not b').
class UnaryExprAST : public ExprAST
{
public:
    TokenType op;
    ExprPtr operand;

    UnaryExprAST(TokenType op, ExprPtr operand) : op(op), operand(std::move(operand))
    {
    }
};

// Array indexing ('a[i]').
class IndexExprAST : public ExprAST
{
public:
    std::string array_name;
    ExprPtr index;

    IndexExprAST(std::string name, ExprPtr index)
        : array_name(std::move(name)), index(std::move(index))
    {
    }
};

// Function call expression ('f(a, b)').
class CallExprAST : public ExprAST
{
public:
    std::string callee;
    std::vector<ExprPtr> args;

    CallExprAST(std::string callee, std::vector<ExprPtr> args)
        : callee(std::move(callee)), args(std::move(args))
    {
    }
};

} // namespace Pascal
