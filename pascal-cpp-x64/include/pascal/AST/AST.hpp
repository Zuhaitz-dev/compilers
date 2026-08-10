#pragma once

#include "pascal/Lexer/Token.hpp"
#include "pascal/Lexer/TokenType.hpp"
#include <memory>
#include <vector>
#include <string>

namespace Pascal
{

// A scalar type or a 1-D array of a scalar type.
struct TypeRef
{
    enum class Kind
    {
        Scalar,
        Array
    };

    Kind kind = Kind::Scalar;
    TokenType scalar = TokenType::KwInteger; // For scalars.
    TokenType elem = TokenType::KwInteger;   // For arrays.
    int lo = 0;                              // Array lower bound (inclusive).
    int hi = 0;                              // Array upper bound (inclusive).

    static TypeRef scalar_type(TokenType t)
    {
        TypeRef ty;
        ty.kind = Kind::Scalar;
        ty.scalar = t;
        return ty;
    }

    static TypeRef array_type(TokenType elem_type, int lo, int hi)
    {
        TypeRef ty;
        ty.kind = Kind::Array;
        ty.elem = elem_type;
        ty.lo = lo;
        ty.hi = hi;
        return ty;
    }

    bool is_array() const
    {
        return kind == Kind::Array;
    }

    int size() const
    {
        return is_array() ? hi - lo + 1 : 1;
    }

    bool operator==(const TypeRef &) const = default;
};

// Base class for all the AST nodes.
class ASTNode
{
public:
    virtual ~ASTNode() = default;
};

// The base class for expressions: evaluates to a value.
class ExprAST : public ASTNode
{
};

// The base class for statements: evaluates to an action.
class StmtAST : public ASTNode
{
};

using ExprPtr = std::unique_ptr<ExprAST>;
using StmtPtr = std::unique_ptr<StmtAST>;

// Variable declaration entry ('x, y : integer;').
struct VarDecl
{
    std::string name;
    TypeRef type;
};

// Constant declaration entry ('const n = 10;').
struct ConstDecl
{
    std::string name;
    TokenValue value;
};

// Parameter declaration ('[var] name : type').
struct ParamDecl
{
    std::string name;
    TypeRef type;
    bool is_var = false;
};

} // namespace Pascal
