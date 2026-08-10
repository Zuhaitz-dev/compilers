#include "pascal/Sema/SymbolTable.hpp"

namespace Pascal
{

void SymbolTable::push_scope()
{
    scopes_.emplace_back();
}

void SymbolTable::pop_scope()
{
    if (!scopes_.empty())
    {
        scopes_.pop_back();
    }
}

bool SymbolTable::declare(const std::string &name, const Symbol &sym)
{
    if (scopes_.empty())
    {
        scopes_.emplace_back();
    }
    auto &scope = scopes_.back();
    if (scope.find(name) != scope.end())
    {
        return false;
    }
    scope[name] = sym;
    return true;
}

const Symbol *SymbolTable::lookup(const std::string &name) const
{
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
        {
            return &found->second;
        }
    }
    return nullptr;
}

} // namespace Pascal
