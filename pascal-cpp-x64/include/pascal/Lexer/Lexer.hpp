#pragma once

#include "pascal/Lexer/Token.hpp"

namespace Pascal
{
class Lexer
{
public:
    explicit Lexer(std::string_view source);
    Token next_token();

private:
    std::string_view source_;
    size_t cursor_ = 0;
    size_t line_ = 1;
    size_t col_ = 1;

    bool is_at_end() const;
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);

    SourceLocation current_loc() const;
    void skip_whitespace_and_comments();

    Token lex_identifier_or_keyword(SourceLocation start_loc);
    Token lex_number(SourceLocation start_loc);
    Token lex_string(SourceLocation start_loc);

    Token make_token(TokenType type, std::string_view lexeme, SourceLocation loc,
                     TokenValue val = std::monostate{});
};
} // namespace Pascal