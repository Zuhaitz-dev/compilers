#pragma once

#include "pascal/Lexer/TokenType.hpp"
#include <string>
#include <string_view>
#include <variant>

namespace Pascal
{
using TokenValue = std::variant<std::monostate, int, double, bool, std::string>;

struct SourceLocation
{
    size_t line = 1;
    size_t column = 1;
};

struct Token
{
    TokenType type = TokenType::Unknown;
    TokenValue value = std::monostate{};
    std::string_view lexeme{};
    SourceLocation loc{};
};

} // namespace Pascal