#include "pascal/Lexer/Lexer.hpp"
#include <unordered_map>
#include <cctype>
#include <algorithm>

namespace Pascal
{
namespace
{
const std::unordered_map<std::string_view, TokenType> &get_keyword_map()
{
    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"program", TokenType::KwProgram},
        {"var", TokenType::KwVar},
        {"const", TokenType::KwConst},
        {"begin", TokenType::KwBegin},
        {"end", TokenType::KwEnd},
        {"integer", TokenType::KwInteger},
        {"real", TokenType::KwReal},
        {"boolean", TokenType::KwBoolean},
        {"char", TokenType::KwChar},
        {"array", TokenType::KwArray},
        {"of", TokenType::KwOf},
        {"if", TokenType::KwIf},
        {"then", TokenType::KwThen},
        {"else", TokenType::KwElse},
        {"while", TokenType::KwWhile},
        {"do", TokenType::KwDo},
        {"for", TokenType::KwFor},
        {"to", TokenType::KwTo},
        {"downto", TokenType::KwDownto},
        {"repeat", TokenType::KwRepeat},
        {"until", TokenType::KwUntil},
        {"case", TokenType::KwCase},
        {"procedure", TokenType::KwProcedure},
        {"function", TokenType::KwFunction},
        {"div", TokenType::KwDiv},
        {"mod", TokenType::KwMod},
        {"and", TokenType::KwAnd},
        {"or", TokenType::KwOr},
        {"not", TokenType::KwNot},
        {"writeln", TokenType::KwWriteln}};

    return keywords;
}
} // unnamed namespace

Lexer::Lexer(std::string_view source) : source_(source)
{
}

bool Lexer::is_at_end() const
{
    return cursor_ >= source_.size();
}

char Lexer::peek() const
{
    if (is_at_end())
    {
        return '\0';
    }
    return source_[cursor_];
}

char Lexer::peek_next() const
{
    if (cursor_ + 1 >= source_.size())
    {
        return '\0';
    }
    return source_[cursor_ + 1];
}

char Lexer::advance()
{
    char c = source_[cursor_++];
    if ('\n' == c)
    {
        line_++;
        col_ = 1;
    }
    else
    {
        col_++;
    }

    return c;
}

bool Lexer::match(char expected)
{
    if (is_at_end() || source_[cursor_] != expected)
    {
        return false;
    }
    advance();
    return true;
}

SourceLocation Lexer::current_loc() const
{
    return {line_, col_};
}

void Lexer::skip_whitespace_and_comments()
{
    while (!is_at_end())
    {
        char c = peek();
        if (' ' == c || '\r' == c || '\t' == c || '\n' == c)
        {
            advance();
        }
        // Pascal style comments: { ... }
        else if ('{' == c)
        {
            while (!is_at_end() && peek() != '}')
            {
                advance();
            }
            if (!is_at_end())
            {
                advance();
            }
        }
        else if ('(' == c && '*' == peek_next())
        {
            advance();
            advance();
            while (!is_at_end() && !('*' == peek() && ')' == peek_next()))
            {
                advance();
            }
            if (!is_at_end())
            {
                advance();
                advance();
            }
        }
        else
        {
            break;
        }
    }
}

Token Lexer::next_token()
{
    skip_whitespace_and_comments();

    if (is_at_end())
    {
        return make_token(TokenType::Eof, "", current_loc());
    }

    SourceLocation start_loc = current_loc();
    char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || '_' == c)
    {
        return lex_identifier_or_keyword(start_loc);
    }

    if (std::isdigit(static_cast<unsigned char>(c)))
    {
        return lex_number(start_loc);
    }

    if ('\'' == c)
    {
        return lex_string(start_loc);
    }

    switch (c)
    {
    case '+':
        return make_token(TokenType::Plus, source_.substr(cursor_ - 1, 1), start_loc);
    case '-':
        return make_token(TokenType::Minus, source_.substr(cursor_ - 1, 1), start_loc);
    case '*':
        return make_token(TokenType::Star, source_.substr(cursor_ - 1, 1), start_loc);
    case '/':
        return make_token(TokenType::Slash, source_.substr(cursor_ - 1, 1), start_loc);
    case '=':
        return make_token(TokenType::Equal, source_.substr(cursor_ - 1, 1), start_loc);
    case ';':
        return make_token(TokenType::Semicolon, source_.substr(cursor_ - 1, 1), start_loc);
    case ',':
        return make_token(TokenType::Comma, source_.substr(cursor_ - 1, 1), start_loc);
    case '(':
        return make_token(TokenType::LParen, source_.substr(cursor_ - 1, 1), start_loc);
    case ')':
        return make_token(TokenType::RParen, source_.substr(cursor_ - 1, 1), start_loc);
    case '[':
        return make_token(TokenType::LBracket, source_.substr(cursor_ - 1, 1), start_loc);
    case ']':
        return make_token(TokenType::RBracket, source_.substr(cursor_ - 1, 1), start_loc);
    case ':':
        if (match('='))
        {
            return make_token(TokenType::Assign, source_.substr(cursor_ - 2, 2), start_loc);
        }
        return make_token(TokenType::Colon, source_.substr(cursor_ - 1, 1), start_loc);

    case '<':
        if (match('>'))
        {
            return make_token(TokenType::NotEqual, source_.substr(cursor_ - 2, 2), start_loc);
        }
        if (match('='))
        {
            return make_token(TokenType::LessEq, source_.substr(cursor_ - 2, 2), start_loc);
        }
        return make_token(TokenType::LessThan, source_.substr(cursor_ - 1, 1), start_loc);

    case '>':
        if (match('='))
        {
            return make_token(TokenType::GreatEq, source_.substr(cursor_ - 2, 2), start_loc);
        }
        return make_token(TokenType::GreatThan, source_.substr(cursor_ - 1, 1), start_loc);

    case '.':
        if (match('.'))
        {
            return make_token(TokenType::DotDot, source_.substr(cursor_ - 2, 2), start_loc);
        }
        return make_token(TokenType::Dot, source_.substr(cursor_ - 1, 1), start_loc);
    }

    return make_token(TokenType::Unknown, source_.substr(cursor_ - 1, 1), start_loc);
}

Token Lexer::lex_identifier_or_keyword(SourceLocation start_loc)
{
    size_t start_pos = cursor_ - 1;

    while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
    {
        advance();
    }

    std::string_view lexeme = source_.substr(start_pos, cursor_ - start_pos);

    // Case-insensitive keyword lookup.
    std::string lower_lexeme;
    lower_lexeme.reserve(lexeme.size());
    for (char ch : lexeme)
    {
        lower_lexeme += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    const auto &keywords = get_keyword_map();

    auto it = keywords.find(lower_lexeme);
    if (it != keywords.end())
    {
        return make_token(it->second, lexeme, start_loc, lower_lexeme);
    }

    if ("true" == lower_lexeme || "false" == lower_lexeme)
    {
        bool val = ("true" == lower_lexeme);
        return make_token(TokenType::BooleanLiteral, lexeme, start_loc, val);
    }

    return make_token(TokenType::Identifier, lexeme, start_loc, std::string(lexeme));
}

Token Lexer::lex_number(SourceLocation start_loc)
{
    size_t start_pos = cursor_ - 1;
    bool is_real = false;

    while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek())))
    {
        advance();
    }

    if ('.' == peek() && std::isdigit(static_cast<unsigned char>(peek_next())))
    {
        is_real = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek())))
        {
            advance();
        }
    }

    std::string_view lexeme = source_.substr(start_pos, cursor_ - start_pos);

    if (is_real)
    {
        double val = std::stod(std::string(lexeme));
        return make_token(TokenType::RealLiteral, lexeme, start_loc, val);
    }
    else
    {
        int val = std::stoi(std::string(lexeme));
        return make_token(TokenType::IntLiteral, lexeme, start_loc, val);
    }
}

Token Lexer::lex_string(SourceLocation start_loc)
{
    size_t start_pos = cursor_ - 1; // Opening quote.

    std::string value;
    while (!is_at_end())
    {
        char c = advance();
        if ('\'' == c)
        {
            if (!is_at_end() && '\'' == peek())
            {
                advance(); // Doubled quote: '' -> a single '.
                value += '\'';
            }
            else
            {
                break; // Closing quote.
            }
        }
        else
        {
            value += c;
        }
    }

    std::string_view lexeme = source_.substr(start_pos, cursor_ - start_pos);
    return make_token(TokenType::StringLiteral, lexeme, start_loc, std::move(value));
}

Token Lexer::make_token(TokenType type, std::string_view lexeme, SourceLocation loc, TokenValue val)
{
    return Token{type, val, lexeme, loc};
}

} // namespace Pascal
