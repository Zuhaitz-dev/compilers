#pragma once

#include "pascal/AST/AST.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Pascal
{

struct Symbol
{
    enum class Kind
    {
        Variable,
        Const,
        Param,
        Function,
        Procedure
    };

    Kind kind = Kind::Variable;
    TypeRef type;
    bool is_var_param = false;
    TokenValue const_value;
};

// A stack of scopes: global scope plus one scope per subprogram.
class SymbolTable
{
public:
    void push_scope();
    void pop_scope();

    // Returns false on duplicate in the current scope.
    bool declare(const std::string &name, const Symbol &sym);

    // Searches inner-to-outer; nullptr if not found.
    const Symbol *lookup(const std::string &name) const;

private:
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

} // namespace Pascal
