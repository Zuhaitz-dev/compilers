#include "pascal/Sema/TypeChecker.hpp"

namespace Pascal
{

TokenType TypeChecker::type_of_token_value(const TokenValue &value)
{
    if (std::holds_alternative<int>(value))
    {
        return TokenType::KwInteger;
    }
    if (std::holds_alternative<double>(value))
    {
        return TokenType::KwReal;
    }
    if (std::holds_alternative<bool>(value))
    {
        return TokenType::KwBoolean;
    }
    if (std::holds_alternative<std::string>(value))
    {
        return std::get<std::string>(value).size() == 1 ? TokenType::KwChar
                                                        : TokenType::StringLiteral;
    }
    return TokenType::KwInteger;
}

bool TypeChecker::is_numeric(TokenType t)
{
    return t == TokenType::KwInteger || t == TokenType::KwReal;
}

TokenType TypeChecker::promote(TokenType a, TokenType b)
{
    if (a == TokenType::KwReal || b == TokenType::KwReal)
    {
        return TokenType::KwReal;
    }
    return TokenType::KwInteger;
}

bool TypeChecker::is_lvalue(const ExprAST &expr) const
{
    if (auto var = dynamic_cast<const VariableExprAST *>(&expr))
    {
        const Symbol *sym = symbols_.lookup(var->name);
        if (!sym)
        {
            return false;
        }
        return sym->kind == Symbol::Kind::Variable || sym->kind == Symbol::Kind::Param;
    }
    return dynamic_cast<const IndexExprAST *>(&expr) != nullptr;
}

TypeRef TypeChecker::require_scalar(const TypeRef &tf, std::string_view what)
{
    if (tf.is_array())
    {
        throw std::runtime_error("Semantic Error: array value used where a scalar is required (" +
                                 std::string(what) + ")");
    }
    return tf;
}

void TypeChecker::require_boolean(const TypeRef &tf, std::string_view what)
{
    if (tf.scalar != TokenType::KwBoolean)
    {
        throw std::runtime_error("Semantic Error: " + std::string(what) +
                                 " condition must be boolean");
    }
}

void TypeChecker::check_program(const ProgramAST &program)
{
    symbols_.push_scope();
    current_function_ = nullptr;

    // Constants.
    for (const auto &c : program.constants)
    {
        Symbol sym;
        sym.kind = Symbol::Kind::Const;
        sym.type = TypeRef::scalar_type(type_of_token_value(c.value));
        sym.const_value = c.value;
        if (!symbols_.declare(c.name, sym))
        {
            throw std::runtime_error("Semantic Error: Duplicate declaration '" + c.name + "'");
        }
    }

    // Variables.
    for (const auto &d : program.declarations)
    {
        Symbol sym;
        sym.kind = Symbol::Kind::Variable;
        sym.type = d.type;
        if (!symbols_.declare(d.name, sym))
        {
            throw std::runtime_error("Semantic Error: Duplicate declaration '" + d.name + "'");
        }
    }

    // Procedures and functions (so they can call each other / recurse).
    for (const auto &sub : program.subprograms)
    {
        Symbol sym;
        sym.kind = sub->is_function ? Symbol::Kind::Function : Symbol::Kind::Procedure;
        sym.type = sub->return_type;
        if (!symbols_.declare(sub->name, sym))
        {
            throw std::runtime_error("Semantic Error: Duplicate declaration '" + sub->name + "'");
        }
        subprograms_[sub->name] = sub.get();
    }

    for (const auto &sub : program.subprograms)
    {
        check_subprogram(*sub);
    }

    if (program.body)
    {
        check_statement(*program.body);
    }

    symbols_.pop_scope();
}

void TypeChecker::check_subprogram(const SubDeclAST &sub)
{
    symbols_.push_scope();

    for (const auto &p : sub.params)
    {
        Symbol sym;
        sym.kind = Symbol::Kind::Param;
        sym.type = p.type;
        sym.is_var_param = p.is_var;
        if (!symbols_.declare(p.name, sym))
        {
            throw std::runtime_error("Semantic Error: Duplicate parameter '" + p.name + "' in '" +
                                     sub.name + "'");
        }
    }

    for (const auto &l : sub.locals)
    {
        Symbol sym;
        sym.kind = Symbol::Kind::Variable;
        sym.type = l.type;
        if (!symbols_.declare(l.name, sym))
        {
            throw std::runtime_error("Semantic Error: Duplicate local variable '" + l.name +
                                     "' in '" + sub.name + "'");
        }
    }

    const SubDeclAST *saved = current_function_;
    current_function_ = sub.is_function ? &sub : nullptr;

    if (sub.body)
    {
        check_statement(*sub.body);
    }

    current_function_ = saved;
    symbols_.pop_scope();
}

TypeRef TypeChecker::check_expression(const ExprAST &expr)
{
    if (auto lit = dynamic_cast<const LiteralExprAST *>(&expr))
    {
        return TypeRef::scalar_type(type_of_token_value(lit->value));
    }

    if (auto var = dynamic_cast<const VariableExprAST *>(&expr))
    {
        const Symbol *sym = symbols_.lookup(var->name);
        if (!sym)
        {
            throw std::runtime_error("Semantic Error: Use of undeclared identifier '" + var->name +
                                     "'");
        }
        if (sym->kind == Symbol::Kind::Function || sym->kind == Symbol::Kind::Procedure)
        {
            throw std::runtime_error("Semantic Error: '" + var->name +
                                     "' is a subprogram and must be called");
        }
        return sym->type;
    }

    if (auto idx = dynamic_cast<const IndexExprAST *>(&expr))
    {
        const Symbol *sym = symbols_.lookup(idx->array_name);
        if (!sym)
        {
            throw std::runtime_error("Semantic Error: Use of undeclared array '" + idx->array_name +
                                     "'");
        }
        if (!sym->type.is_array())
        {
            throw std::runtime_error("Semantic Error: '" + idx->array_name + "' is not an array");
        }
        TypeRef index_type = check_expression(*idx->index);
        if (index_type.scalar != TokenType::KwInteger)
        {
            throw std::runtime_error("Semantic Error: Array index must be an integer");
        }
        return TypeRef::scalar_type(sym->type.elem);
    }

    if (auto un = dynamic_cast<const UnaryExprAST *>(&expr))
    {
        TypeRef op = require_scalar(check_expression(*un->operand), "unary operator");
        if (un->op == TokenType::Minus)
        {
            if (!is_numeric(op.scalar))
            {
                throw std::runtime_error("Semantic Error: unary '-' requires a numeric operand");
            }
            return op;
        }
        if (un->op == TokenType::KwNot)
        {
            if (op.scalar != TokenType::KwBoolean && op.scalar != TokenType::KwInteger)
            {
                throw std::runtime_error(
                    "Semantic Error: 'not' requires a boolean or integer operand");
            }
            return op;
        }
    }

    if (auto bin = dynamic_cast<const BinaryExprAST *>(&expr))
    {
        TypeRef lt = require_scalar(check_expression(*bin->lhs), "binary operator");
        TypeRef rt = require_scalar(check_expression(*bin->rhs), "binary operator");

        switch (bin->op)
        {
        case TokenType::KwDiv:
        case TokenType::KwMod:
            if (lt.scalar != TokenType::KwInteger || rt.scalar != TokenType::KwInteger)
            {
                throw std::runtime_error(
                    "Semantic Error: 'div' and 'mod' require integer operands");
            }
            return TypeRef::scalar_type(TokenType::KwInteger);

        case TokenType::KwAnd:
        case TokenType::KwOr:
            if (lt.scalar != rt.scalar ||
                (lt.scalar != TokenType::KwBoolean && lt.scalar != TokenType::KwInteger))
            {
                throw std::runtime_error("Semantic Error: 'and'/'or' require matching boolean "
                                         "or integer operands");
            }
            return lt;

        case TokenType::Slash:
            if (!is_numeric(lt.scalar) || !is_numeric(rt.scalar))
            {
                throw std::runtime_error("Semantic Error: '/' requires numeric operands");
            }
            return TypeRef::scalar_type(TokenType::KwReal);

        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
            if (!is_numeric(lt.scalar) || !is_numeric(rt.scalar))
            {
                throw std::runtime_error("Semantic Error: arithmetic requires numeric operands");
            }
            return TypeRef::scalar_type(promote(lt.scalar, rt.scalar));

        case TokenType::Equal:
        case TokenType::NotEqual:
        case TokenType::LessThan:
        case TokenType::LessEq:
        case TokenType::GreatThan:
        case TokenType::GreatEq:
            if (lt.scalar != rt.scalar && !(is_numeric(lt.scalar) && is_numeric(rt.scalar)))
            {
                throw std::runtime_error("Semantic Error: type mismatch in comparison");
            }
            return TypeRef::scalar_type(TokenType::KwBoolean);

        default:
            throw std::runtime_error("Semantic Error: Unsupported binary operator");
        }
    }

    if (auto call = dynamic_cast<const CallExprAST *>(&expr))
    {
        auto it = subprograms_.find(call->callee);
        if (it == subprograms_.end())
        {
            throw std::runtime_error("Semantic Error: Call to undeclared subprogram '" +
                                     call->callee + "'");
        }
        if (!it->second->is_function)
        {
            throw std::runtime_error("Semantic Error: Procedure '" + call->callee +
                                     "' cannot be used as an expression");
        }
        check_call_args(it->second->params, call->args, call->callee);
        return it->second->return_type;
    }

    throw std::runtime_error("Semantic Error: Unknown expression node");
}

void TypeChecker::check_call_args(const std::vector<ParamDecl> &params,
                                  const std::vector<ExprPtr> &args, std::string_view callee)
{
    if (params.size() != args.size())
    {
        throw std::runtime_error("Semantic Error: Wrong number of arguments to '" +
                                 std::string(callee) + "'");
    }

    for (size_t i = 0; i < params.size(); ++i)
    {
        const ParamDecl &p = params[i];
        TypeRef at = check_expression(*args[i]);

        if (p.is_var)
        {
            if (!is_lvalue(*args[i]))
            {
                throw std::runtime_error("Semantic Error: '" + std::string(callee) +
                                         "' expects a variable (var) argument");
            }
            if (!(at == p.type))
            {
                throw std::runtime_error("Semantic Error: Type mismatch in var argument to '" +
                                         std::string(callee) + "'");
            }
        }
        else
        {
            if (at.is_array())
            {
                throw std::runtime_error("Semantic Error: cannot pass an array by value to '" +
                                         std::string(callee) + "'");
            }
            if (p.type.is_array())
            {
                throw std::runtime_error(
                    "Semantic Error: array value parameter not supported in '" +
                    std::string(callee) + "'");
            }
            if (at.scalar != p.type.scalar)
            {
                bool widening =
                    (p.type.scalar == TokenType::KwReal && at.scalar == TokenType::KwInteger);
                if (!widening)
                {
                    throw std::runtime_error("Semantic Error: Type mismatch in argument to '" +
                                             std::string(callee) + "'");
                }
            }
        }
    }
}

void TypeChecker::check_statement(const StmtAST &stmt)
{
    if (auto assign = dynamic_cast<const AssignStmtAST *>(&stmt))
    {
        std::string target_name;
        bool is_index = false;
        if (auto v = dynamic_cast<const VariableExprAST *>(assign->target.get()))
        {
            target_name = v->name;
        }
        else if (auto ix = dynamic_cast<const IndexExprAST *>(assign->target.get()))
        {
            target_name = ix->array_name;
            is_index = true;
        }
        else
        {
            throw std::runtime_error("Semantic Error: Invalid assignment target");
        }

        // Assignment to the current function name sets its result.
        if (!is_index && current_function_ && current_function_->name == target_name)
        {
            TypeRef et = require_scalar(check_expression(*assign->expr), "function result");
            TypeRef rt = current_function_->return_type;
            if (et.scalar != rt.scalar &&
                !(rt.scalar == TokenType::KwReal && et.scalar == TokenType::KwInteger))
            {
                throw std::runtime_error("Semantic Error: Function result type mismatch in '" +
                                         current_function_->name + "'");
            }
            return;
        }

        const Symbol *sym = symbols_.lookup(target_name);
        if (!sym)
        {
            throw std::runtime_error("Semantic Error: Assignment to undeclared variable '" +
                                     target_name + "'");
        }
        if (sym->kind == Symbol::Kind::Const)
        {
            throw std::runtime_error("Semantic Error: Cannot assign to constant '" + target_name +
                                     "'");
        }
        if (sym->kind == Symbol::Kind::Function || sym->kind == Symbol::Kind::Procedure)
        {
            throw std::runtime_error("Semantic Error: Cannot assign to subprogram name '" +
                                     target_name + "'");
        }

        if (is_index)
        {
            if (!sym->type.is_array())
            {
                throw std::runtime_error("Semantic Error: '" + target_name + "' is not an array");
            }
            const auto *ix = static_cast<const IndexExprAST *>(assign->target.get());
            TypeRef it = require_scalar(check_expression(*ix->index), "array index");
            if (it.scalar != TokenType::KwInteger)
            {
                throw std::runtime_error("Semantic Error: Array index must be an integer");
            }
            TypeRef et = require_scalar(check_expression(*assign->expr), "assignment");
            if (et.scalar != sym->type.elem &&
                !(sym->type.elem == TokenType::KwReal && et.scalar == TokenType::KwInteger))
            {
                throw std::runtime_error("Semantic Error: Type mismatch in assignment to '" +
                                         target_name + "'");
            }
            return;
        }

        if (sym->type.is_array())
        {
            throw std::runtime_error("Semantic Error: Cannot assign to array '" + target_name +
                                     "'");
        }

        TypeRef et = require_scalar(check_expression(*assign->expr), "assignment");
        if (et.scalar != sym->type.scalar &&
            !(sym->type.scalar == TokenType::KwReal && et.scalar == TokenType::KwInteger))
        {
            throw std::runtime_error("Semantic Error: Type mismatch in assignment to '" +
                                     target_name + "'");
        }
    }
    else if (auto compound = dynamic_cast<const CompoundStmtAST *>(&stmt))
    {
        for (const auto &s : compound->statements)
        {
            if (s)
            {
                check_statement(*s);
            }
        }
    }
    else if (auto ifs = dynamic_cast<const IfStmtAST *>(&stmt))
    {
        require_boolean(require_scalar(check_expression(*ifs->condition), "if"), "if");
        if (ifs->then_branch)
        {
            check_statement(*ifs->then_branch);
        }
        if (ifs->else_branch)
        {
            check_statement(*ifs->else_branch);
        }
    }
    else if (auto wh = dynamic_cast<const WhileStmtAST *>(&stmt))
    {
        require_boolean(require_scalar(check_expression(*wh->condition), "while"), "while");
        if (wh->body)
        {
            check_statement(*wh->body);
        }
    }
    else if (auto fo = dynamic_cast<const ForStmtAST *>(&stmt))
    {
        const Symbol *sym = symbols_.lookup(fo->var_name);
        if (!sym || (sym->kind != Symbol::Kind::Variable && sym->kind != Symbol::Kind::Param))
        {
            throw std::runtime_error("Semantic Error: for loop variable '" + fo->var_name +
                                     "' must be a variable");
        }
        if (sym->type.is_array() || sym->type.scalar != TokenType::KwInteger)
        {
            throw std::runtime_error("Semantic Error: for loop variable '" + fo->var_name +
                                     "' must be an integer variable");
        }
        TypeRef st = require_scalar(check_expression(*fo->start), "for loop");
        TypeRef et = require_scalar(check_expression(*fo->end), "for loop");
        if (st.scalar != TokenType::KwInteger || et.scalar != TokenType::KwInteger)
        {
            throw std::runtime_error("Semantic Error: for loop bounds must be integer");
        }
        if (fo->body)
        {
            check_statement(*fo->body);
        }
    }
    else if (auto rep = dynamic_cast<const RepeatStmtAST *>(&stmt))
    {
        if (rep->body)
        {
            check_statement(*rep->body);
        }
        require_boolean(require_scalar(check_expression(*rep->condition), "until"), "until");
    }
    else if (auto cas = dynamic_cast<const CaseStmtAST *>(&stmt))
    {
        TypeRef et = require_scalar(check_expression(*cas->expr), "case");
        if (et.scalar != TokenType::KwInteger && et.scalar != TokenType::KwChar)
        {
            throw std::runtime_error("Semantic Error: case selector must be an integer or char");
        }
        for (const auto &arm : cas->arms)
        {
            for (const auto &label : arm.labels)
            {
                if (et.scalar == TokenType::KwInteger && !std::holds_alternative<int>(label))
                {
                    throw std::runtime_error("Semantic Error: case label type mismatch");
                }
                if (et.scalar == TokenType::KwChar &&
                    !(std::holds_alternative<std::string>(label) &&
                      std::get<std::string>(label).size() == 1))
                {
                    throw std::runtime_error("Semantic Error: case label type mismatch");
                }
            }
            if (arm.stmt)
            {
                check_statement(*arm.stmt);
            }
        }
        if (cas->else_branch)
        {
            check_statement(*cas->else_branch);
        }
    }
    else if (auto wr = dynamic_cast<const WritelnStmtAST *>(&stmt))
    {
        for (const auto &a : wr->args)
        {
            TypeRef at = require_scalar(check_expression(*a), "writeln");
            if (at.scalar != TokenType::KwInteger && at.scalar != TokenType::KwReal &&
                at.scalar != TokenType::KwBoolean && at.scalar != TokenType::KwChar &&
                at.scalar != TokenType::StringLiteral)
            {
                throw std::runtime_error(
                    "Semantic Error: writeln cannot print a value of that type");
            }
        }
    }
    else if (auto call = dynamic_cast<const CallStmtAST *>(&stmt))
    {
        auto it = subprograms_.find(call->callee);
        if (it == subprograms_.end())
        {
            throw std::runtime_error("Semantic Error: Call to undeclared subprogram '" +
                                     call->callee + "'");
        }
        check_call_args(it->second->params, call->args, call->callee);
    }
}

} // namespace Pascal
