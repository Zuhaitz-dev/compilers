#include "pascal/Parser/Parser.hpp"

namespace Pascal
{

Parser::Parser(Lexer &lexer) : lexer_(lexer)
{
    advance();
}

void Parser::advance()
{
    current_token_ = lexer_.next_token();
}

bool Parser::check(TokenType type) const
{
    return current_token_.type == type;
}

bool Parser::match(TokenType type)
{
    if (check(type))
    {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, std::string_view error_message)
{
    if (check(type))
    {
        Token tok = current_token_;
        advance();
        return tok;
    }
    error(error_message);
}

[[noreturn]] void Parser::error(std::string_view message) const
{
    throw std::runtime_error(std::string(message) + " at line " +
                             std::to_string(current_token_.loc.line) + ", col " +
                             std::to_string(current_token_.loc.column));
}

std::unique_ptr<ProgramAST> Parser::parse_program()
{
    consume(TokenType::KwProgram, "Expected 'program' keyword");
    Token name_tok = consume(TokenType::Identifier, "Expected program name");
    consume(TokenType::Semicolon, "Expected ';' after program name");

    std::vector<ConstDecl> consts;
    std::vector<VarDecl> decls;
    std::vector<std::unique_ptr<SubDeclAST>> subs;

    while (true)
    {
        if (check(TokenType::KwConst))
        {
            auto c = parse_const_declarations();
            consts.insert(consts.end(), c.begin(), c.end());
        }
        else if (check(TokenType::KwVar))
        {
            auto d = parse_var_declarations();
            decls.insert(decls.end(), d.begin(), d.end());
        }
        else if (check(TokenType::KwProcedure) || check(TokenType::KwFunction))
        {
            subs.push_back(parse_subprogram());
        }
        else
        {
            break;
        }
    }

    auto body = parse_compound();
    consume(TokenType::Dot, "Expected '.' at end of program");

    return std::make_unique<ProgramAST>(
        std::string(name_tok.lexeme), std::move(consts), std::move(decls), std::move(subs),
        std::unique_ptr<CompoundStmtAST>(static_cast<CompoundStmtAST *>(body.release())));
}

TokenType Parser::parse_scalar_type()
{
    if (match(TokenType::KwInteger))
    {
        return TokenType::KwInteger;
    }
    if (match(TokenType::KwReal))
    {
        return TokenType::KwReal;
    }
    if (match(TokenType::KwBoolean))
    {
        return TokenType::KwBoolean;
    }
    if (match(TokenType::KwChar))
    {
        return TokenType::KwChar;
    }
    error("Expected a type name (integer, real, boolean, char)");
}

// array[lo..hi] of <scalar> | integer | real | boolean | char
TypeRef Parser::parse_type()
{
    if (match(TokenType::KwArray))
    {
        consume(TokenType::LBracket, "Expected '[' after 'array'");
        Token lo_tok = consume(TokenType::IntLiteral, "Expected integer lower bound in array type");
        consume(TokenType::DotDot, "Expected '..' in array bounds");
        Token hi_tok = consume(TokenType::IntLiteral, "Expected integer upper bound in array type");
        consume(TokenType::RBracket, "Expected ']' after array bounds");
        consume(TokenType::KwOf, "Expected 'of' in array type");
        TokenType elem = parse_scalar_type();

        int lo = std::get<int>(lo_tok.value);
        int hi = std::get<int>(hi_tok.value);
        if (hi < lo)
        {
            error("Array upper bound is smaller than lower bound");
        }
        return TypeRef::array_type(elem, lo, hi);
    }
    return TypeRef::scalar_type(parse_scalar_type());
}

std::vector<ConstDecl> Parser::parse_const_declarations()
{
    consume(TokenType::KwConst, "Expected 'const'");
    std::vector<ConstDecl> decls;

    while (check(TokenType::Identifier))
    {
        Token name = consume(TokenType::Identifier, "Expected constant name");
        consume(TokenType::Equal, "Expected '=' in constant declaration");
        TokenValue value = parse_const_value();
        consume(TokenType::Semicolon, "Expected ';' after constant declaration");
        decls.push_back({std::string(name.lexeme), std::move(value)});
    }

    return decls;
}

TokenValue Parser::parse_const_value()
{
    if (check(TokenType::IntLiteral) || check(TokenType::RealLiteral) ||
        check(TokenType::BooleanLiteral) || check(TokenType::StringLiteral))
    {
        TokenValue value = current_token_.value;
        advance();
        return value;
    }
    if (check(TokenType::Minus))
    {
        advance();
        if (check(TokenType::IntLiteral))
        {
            TokenValue value = -std::get<int>(current_token_.value);
            advance();
            return value;
        }
        if (check(TokenType::RealLiteral))
        {
            TokenValue value = -std::get<double>(current_token_.value);
            advance();
            return value;
        }
    }
    error("Expected a constant value (number, true/false, or string)");
}

std::vector<VarDecl> Parser::parse_var_declarations()
{
    consume(TokenType::KwVar, "Expected 'var'");
    std::vector<VarDecl> decls;

    while (check(TokenType::Identifier))
    {
        std::vector<std::string> var_names;
        var_names.push_back(std::string(current_token_.lexeme));
        advance();

        while (match(TokenType::Comma))
        {
            var_names.push_back(std::string(
                consume(TokenType::Identifier, "Expected variable name after ','").lexeme));
        }

        consume(TokenType::Colon, "Expected ':' after variable names");
        TypeRef type = parse_type();
        consume(TokenType::Semicolon, "Expected ';' after type declaration");

        for (const auto &name : var_names)
        {
            decls.push_back({name, type});
        }
    }

    return decls;
}

std::vector<ParamDecl> Parser::parse_param_list()
{
    std::vector<ParamDecl> params;
    if (!match(TokenType::LParen))
    {
        return params;
    }
    if (match(TokenType::RParen))
    {
        return params;
    }

    while (true)
    {
        bool is_var = match(TokenType::KwVar);
        std::vector<std::string> names;
        names.push_back(
            std::string(consume(TokenType::Identifier, "Expected parameter name").lexeme));
        while (match(TokenType::Comma))
        {
            names.push_back(std::string(
                consume(TokenType::Identifier, "Expected parameter name after ','").lexeme));
        }
        consume(TokenType::Colon, "Expected ':' after parameter names");
        TypeRef type = parse_type();
        for (auto &name : names)
        {
            params.push_back({std::move(name), type, is_var});
        }

        if (match(TokenType::Semicolon))
        {
            continue;
        }
        break;
    }

    consume(TokenType::RParen, "Expected ')' after parameter list");
    return params;
}

std::unique_ptr<SubDeclAST> Parser::parse_subprogram()
{
    bool is_function = match(TokenType::KwFunction);
    if (!is_function)
    {
        consume(TokenType::KwProcedure, "Expected 'procedure' or 'function'");
    }

    Token name = consume(TokenType::Identifier, "Expected subprogram name");
    auto params = parse_param_list();

    TypeRef ret_type = TypeRef::scalar_type(TokenType::KwInteger);
    if (is_function)
    {
        consume(TokenType::Colon, "Expected ':' before function return type");
        ret_type = parse_type();
    }

    consume(TokenType::Semicolon, "Expected ';' after subprogram header");

    std::vector<VarDecl> locals;
    if (check(TokenType::KwVar))
    {
        locals = parse_var_declarations();
    }

    StmtPtr body = parse_compound();
    consume(TokenType::Semicolon, "Expected ';' after subprogram body");

    return std::make_unique<SubDeclAST>(std::string(name.lexeme), is_function, ret_type,
                                        std::move(params), std::move(locals), std::move(body));
}

StmtPtr Parser::parse_compound()
{
    consume(TokenType::KwBegin, "Expected 'begin'");
    std::vector<StmtPtr> stmts;

    while (!check(TokenType::KwEnd) && !check(TokenType::Eof))
    {
        stmts.push_back(parse_statement());
        match(TokenType::Semicolon);
    }

    consume(TokenType::KwEnd, "Expected 'end'");
    return std::make_unique<CompoundStmtAST>(std::move(stmts));
}

StmtPtr Parser::parse_statement()
{
    if (check(TokenType::KwBegin))
    {
        return parse_compound();
    }
    if (check(TokenType::KwIf))
    {
        return parse_if();
    }
    if (check(TokenType::KwWhile))
    {
        return parse_while();
    }
    if (check(TokenType::KwFor))
    {
        return parse_for();
    }
    if (check(TokenType::KwRepeat))
    {
        return parse_repeat();
    }
    if (check(TokenType::KwCase))
    {
        return parse_case();
    }
    if (check(TokenType::KwWriteln))
    {
        return parse_writeln();
    }
    if (check(TokenType::Identifier))
    {
        return parse_assignment_or_call();
    }

    error("Unexpected token in statement");
}

StmtPtr Parser::parse_assignment_or_call()
{
    Token name = consume(TokenType::Identifier, "Expected variable or call name");

    if (match(TokenType::LParen))
    {
        std::vector<ExprPtr> args;
        if (!check(TokenType::RParen))
        {
            args.push_back(parse_expression());
            while (match(TokenType::Comma))
            {
                args.push_back(parse_expression());
            }
        }
        consume(TokenType::RParen, "Expected ')' after call");
        return std::make_unique<CallStmtAST>(std::string(name.lexeme), std::move(args));
    }

    if (match(TokenType::Assign))
    {
        auto target = std::make_unique<VariableExprAST>(std::string(name.lexeme));
        auto expr = parse_expression();
        return std::make_unique<AssignStmtAST>(std::move(target), std::move(expr));
    }

    if (match(TokenType::LBracket))
    {
        auto index = parse_expression();
        consume(TokenType::RBracket, "Expected ']' after array index");
        auto target = std::make_unique<IndexExprAST>(std::string(name.lexeme), std::move(index));
        consume(TokenType::Assign, "Expected ':=' after array element");
        auto expr = parse_expression();
        return std::make_unique<AssignStmtAST>(std::move(target), std::move(expr));
    }

    // Bare procedure call with no arguments: 'name;'
    return std::make_unique<CallStmtAST>(std::string(name.lexeme), std::vector<ExprPtr>{});
}

StmtPtr Parser::parse_if()
{
    consume(TokenType::KwIf, "Expected 'if'");
    auto cond = parse_expression();
    consume(TokenType::KwThen, "Expected 'then'");
    auto then_branch = parse_statement();

    StmtPtr else_branch = nullptr;
    if (match(TokenType::KwElse))
    {
        else_branch = parse_statement();
    }

    return std::make_unique<IfStmtAST>(std::move(cond), std::move(then_branch),
                                       std::move(else_branch));
}

StmtPtr Parser::parse_while()
{
    consume(TokenType::KwWhile, "Expected 'while'");
    auto cond = parse_expression();
    consume(TokenType::KwDo, "Expected 'do'");
    auto body = parse_statement();
    return std::make_unique<WhileStmtAST>(std::move(cond), std::move(body));
}

StmtPtr Parser::parse_for()
{
    consume(TokenType::KwFor, "Expected 'for'");
    Token var = consume(TokenType::Identifier, "Expected loop variable");
    consume(TokenType::Assign, "Expected ':=' in for loop");
    auto start = parse_expression();

    bool downto = match(TokenType::KwDownto);
    if (!downto)
    {
        consume(TokenType::KwTo, "Expected 'to' or 'downto'");
    }
    auto end = parse_expression();
    consume(TokenType::KwDo, "Expected 'do'");
    auto body = parse_statement();

    return std::make_unique<ForStmtAST>(std::string(var.lexeme), std::move(start), std::move(end),
                                        std::move(body), downto);
}

StmtPtr Parser::parse_repeat()
{
    consume(TokenType::KwRepeat, "Expected 'repeat'");
    std::vector<StmtPtr> stmts;

    while (!check(TokenType::KwUntil) && !check(TokenType::Eof))
    {
        stmts.push_back(parse_statement());
        match(TokenType::Semicolon);
    }

    consume(TokenType::KwUntil, "Expected 'until'");
    auto cond = parse_expression();
    return std::make_unique<RepeatStmtAST>(std::make_unique<CompoundStmtAST>(std::move(stmts)),
                                           std::move(cond));
}

StmtPtr Parser::parse_case()
{
    consume(TokenType::KwCase, "Expected 'case'");
    auto expr = parse_expression();
    consume(TokenType::KwOf, "Expected 'of' after case expression");

    std::vector<CaseArm> arms;
    StmtPtr else_branch = nullptr;

    while (!check(TokenType::KwEnd) && !check(TokenType::Eof))
    {
        if (match(TokenType::KwElse))
        {
            else_branch = parse_statement();
            match(TokenType::Semicolon);
            break;
        }

        CaseArm arm;
        while (true)
        {
            if (check(TokenType::IntLiteral) || check(TokenType::StringLiteral))
            {
                arm.labels.push_back(current_token_.value);
                advance();
            }
            else
            {
                error("Expected integer or char case label");
            }
            if (!match(TokenType::Comma))
            {
                break;
            }
        }
        consume(TokenType::Colon, "Expected ':' after case label");
        arm.stmt = parse_statement();
        arms.push_back(std::move(arm));
        match(TokenType::Semicolon);
    }

    consume(TokenType::KwEnd, "Expected 'end' after case");
    return std::make_unique<CaseStmtAST>(std::move(expr), std::move(arms), std::move(else_branch));
}

StmtPtr Parser::parse_writeln()
{
    consume(TokenType::KwWriteln, "Expected 'writeln'");
    std::vector<ExprPtr> args;

    if (match(TokenType::LParen))
    {
        if (!check(TokenType::RParen))
        {
            args.push_back(parse_expression());
            while (match(TokenType::Comma))
            {
                args.push_back(parse_expression());
            }
        }
        consume(TokenType::RParen, "Expected ')' after writeln arguments");
    }

    return std::make_unique<WritelnStmtAST>(std::move(args));
}

ExprPtr Parser::parse_expression()
{
    return parse_comparison();
}

ExprPtr Parser::parse_comparison()
{
    auto lhs = parse_additive();

    while (check(TokenType::Equal) || check(TokenType::NotEqual) || check(TokenType::LessThan) ||
           check(TokenType::LessEq) || check(TokenType::GreatThan) || check(TokenType::GreatEq))
    {
        TokenType op = current_token_.type;
        advance();
        auto rhs = parse_additive();
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

ExprPtr Parser::parse_additive()
{
    auto lhs = parse_term();

    while (check(TokenType::Plus) || check(TokenType::Minus) || check(TokenType::KwOr))
    {
        TokenType op = current_token_.type;
        advance();
        auto rhs = parse_term();
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

ExprPtr Parser::parse_term()
{
    auto lhs = parse_unary();

    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::KwDiv) ||
           check(TokenType::KwMod) || check(TokenType::KwAnd))
    {
        TokenType op = current_token_.type;
        advance();
        auto rhs = parse_unary();
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

ExprPtr Parser::parse_unary()
{
    if (match(TokenType::KwNot))
    {
        return std::make_unique<UnaryExprAST>(TokenType::KwNot, parse_unary());
    }
    if (match(TokenType::Minus))
    {
        return std::make_unique<UnaryExprAST>(TokenType::Minus, parse_unary());
    }
    return parse_primary();
}

ExprPtr Parser::parse_primary()
{
    if (check(TokenType::IntLiteral) || check(TokenType::RealLiteral) ||
        check(TokenType::BooleanLiteral) || check(TokenType::StringLiteral))
    {
        TokenValue val = current_token_.value;
        advance();
        return std::make_unique<LiteralExprAST>(std::move(val));
    }

    if (check(TokenType::Identifier))
    {
        Token name = consume(TokenType::Identifier, "Expected identifier");

        if (match(TokenType::LBracket))
        {
            auto index = parse_expression();
            consume(TokenType::RBracket, "Expected ']' after array index");
            return std::make_unique<IndexExprAST>(std::string(name.lexeme), std::move(index));
        }
        if (match(TokenType::LParen))
        {
            std::vector<ExprPtr> args;
            if (!check(TokenType::RParen))
            {
                args.push_back(parse_expression());
                while (match(TokenType::Comma))
                {
                    args.push_back(parse_expression());
                }
            }
            consume(TokenType::RParen, "Expected ')' after call");
            return std::make_unique<CallExprAST>(std::string(name.lexeme), std::move(args));
        }
        return std::make_unique<VariableExprAST>(std::string(name.lexeme));
    }

    if (match(TokenType::LParen))
    {
        auto expr = parse_expression();
        consume(TokenType::RParen, "Expected ')' after expression");
        return expr;
    }

    error("Unexpected token in expression");
}

} // namespace Pascal
