#include "pascal/CodeGen/CodeGen.hpp"

// --- LLVM Target & CodeGen Headers (LLVM 17+) ---
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>

#include <iostream>

namespace Pascal
{

CodeGenerator::CodeGenerator()
{
    context_ = std::make_unique<llvm::LLVMContext>();
    builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
}

llvm::Type *CodeGenerator::llvm_type_of(const TypeRef &type)
{
    if (type.is_array())
    {
        llvm::Type *elem = llvm_type_of(TypeRef::scalar_type(type.elem));
        return llvm::ArrayType::get(elem, static_cast<uint64_t>(type.size()));
    }
    switch (type.scalar)
    {
    case TokenType::KwInteger:
        return builder_->getInt32Ty();
    case TokenType::KwReal:
        return builder_->getDoubleTy();
    case TokenType::KwBoolean:
        return builder_->getInt1Ty();
    case TokenType::KwChar:
        return builder_->getInt8Ty();
    default:
        return builder_->getInt32Ty();
    }
}

llvm::FunctionCallee CodeGenerator::get_or_declare_printf(llvm::Module *mod)
{
    llvm::FunctionType *printf_type = llvm::FunctionType::get(
        builder_->getInt32Ty(), llvm::PointerType::getUnqual(*context_), true);
    return mod->getOrInsertFunction("printf", printf_type);
}

llvm::Value *CodeGenerator::const_literal(const TokenValue &value)
{
    if (auto pVal = std::get_if<int>(&value))
    {
        return builder_->getInt32(*pVal);
    }
    if (auto pVal = std::get_if<double>(&value))
    {
        return llvm::ConstantFP::get(*context_, llvm::APFloat(*pVal));
    }
    if (auto pVal = std::get_if<bool>(&value))
    {
        return builder_->getInt1(*pVal);
    }
    if (auto pVal = std::get_if<std::string>(&value))
    {
        if (pVal->size() == 1)
        {
            return builder_->getInt8(static_cast<uint8_t>((*pVal)[0]));
        }
        return builder_->CreateGlobalString(*pVal, "strlit");
    }
    throw std::runtime_error("CodeGen Error: Unsupported literal");
}

llvm::Value *CodeGenerator::coerce_to_type(llvm::Value *v, TokenType to)
{
    if (v->getType()->isIntegerTy(32) && to == TokenType::KwReal)
    {
        return builder_->CreateSIToFP(v, builder_->getDoubleTy());
    }
    return v;
}

std::unique_ptr<llvm::Module> CodeGenerator::emit_program(const ProgramAST &program)
{
    auto module = std::make_unique<llvm::Module>("PascalModule", *context_);

    get_or_declare_printf(module.get());

    consts_.clear();
    for (const auto &c : program.constants)
    {
        consts_[c.name] = c.value;
    }

    subprograms_.clear();
    values_.clear();
    var_types_.clear();

    // Program variables become zero-initialized globals so that
    // procedures and functions can access them.
    for (const auto &d : program.declarations)
    {
        llvm::Type *ty = llvm_type_of(d.type);
        auto *gv = new llvm::GlobalVariable(*module, ty, false, llvm::GlobalValue::ExternalLinkage,
                                            llvm::Constant::getNullValue(ty), d.name);
        values_[d.name] = gv;
        var_types_[d.name] = d.type;
    }

    for (const auto &sub : program.subprograms)
    {
        subprograms_[sub->name] = sub.get();
        llvm_functions_[sub->name] = nullptr;
    }

    // Create all subprogram signatures first so calls (and recursion) resolve.
    for (const auto &sub : program.subprograms)
    {
        std::vector<llvm::Type *> param_types;
        for (const auto &p : sub->params)
        {
            llvm::Type *ty = llvm_type_of(p.type);
            if (p.is_var)
            {
                ty = llvm::PointerType::getUnqual(*context_);
            }
            param_types.push_back(ty);
        }
        llvm::Type *ret = sub->is_function ? llvm_type_of(sub->return_type) : builder_->getVoidTy();
        llvm::FunctionType *ft = llvm::FunctionType::get(ret, param_types, false);
        llvm::Function *func =
            llvm::Function::Create(ft, llvm::Function::ExternalLinkage, sub->name, module.get());
        llvm_functions_[sub->name] = func;
    }

    // Fill in subprogram bodies.
    for (const auto &sub : program.subprograms)
    {
        codegen_subprogram(*sub);
    }

    // Top-level program body goes into main().
    llvm::FunctionType *main_type = llvm::FunctionType::get(builder_->getInt32Ty(), false);
    llvm::Function *main_func =
        llvm::Function::Create(main_type, llvm::Function::ExternalLinkage, "main", module.get());
    llvm::BasicBlock *main_entry = llvm::BasicBlock::Create(*context_, "entry", main_func);
    builder_->SetInsertPoint(main_entry);

    if (program.body)
    {
        codegen_stmt(*program.body);
    }
    builder_->CreateRet(builder_->getInt32(0));
    llvm::verifyFunction(*main_func);

    return module;
}

void CodeGenerator::codegen_subprogram(const SubDeclAST &sub)
{
    llvm::Function *func = llvm_functions_[sub.name];

    llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context_, "entry", func);
    builder_->SetInsertPoint(entry);

    // Snapshot the global scope so params/locals (which may shadow globals)
    // are fully restored when the subprogram finishes.
    auto saved_values = values_;
    auto saved_types = var_types_;

    auto declare_storage = [&](const std::string &name, llvm::Value *addr, const TypeRef &type)
    {
        values_[name] = addr;
        var_types_[name] = type;
    };

    // Parameters.
    size_t i = 0;
    for (auto arg = func->arg_begin(); arg != func->arg_end(); ++arg, ++i)
    {
        const ParamDecl &p = sub.params[i];
        if (p.is_var)
        {
            arg->setName(p.name);
            declare_storage(p.name, arg, p.type);
        }
        else
        {
            llvm::AllocaInst *alloca =
                builder_->CreateAlloca(llvm_type_of(p.type), nullptr, p.name);
            builder_->CreateStore(arg, alloca);
            declare_storage(p.name, alloca, p.type);
        }
    }

    // Function result slot (assigned via "fname := expr").
    if (sub.is_function)
    {
        llvm::Type *rty = llvm_type_of(sub.return_type);
        llvm::AllocaInst *ret_slot = builder_->CreateAlloca(rty, nullptr, sub.name + ".result");
        builder_->CreateStore(llvm::Constant::getNullValue(rty), ret_slot);
        declare_storage(sub.name, ret_slot, sub.return_type);
    }

    // Local variables.
    for (const auto &l : sub.locals)
    {
        llvm::AllocaInst *alloca = builder_->CreateAlloca(llvm_type_of(l.type), nullptr, l.name);
        declare_storage(l.name, alloca, l.type);
    }

    llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(*context_, "body", func);
    builder_->CreateBr(body_bb);
    builder_->SetInsertPoint(body_bb);

    if (sub.body)
    {
        codegen_stmt(*sub.body);
    }

    if (sub.is_function)
    {
        llvm::Value *result =
            builder_->CreateLoad(llvm_type_of(sub.return_type), values_[sub.name]);
        builder_->CreateRet(result);
    }
    else
    {
        builder_->CreateRetVoid();
    }

    values_ = std::move(saved_values);
    var_types_ = std::move(saved_types);

    llvm::verifyFunction(*func);
}

llvm::Value *CodeGenerator::codegen_address(const ExprAST &expr)
{
    if (auto var = dynamic_cast<const VariableExprAST *>(&expr))
    {
        auto it = values_.find(var->name);
        if (it == values_.end())
        {
            throw std::runtime_error("CodeGen Error: No storage for variable " + var->name);
        }
        return it->second;
    }

    if (auto idx = dynamic_cast<const IndexExprAST *>(&expr))
    {
        auto it = values_.find(idx->array_name);
        if (it == values_.end())
        {
            throw std::runtime_error("CodeGen Error: No storage for array " + idx->array_name);
        }
        TypeRef arr_type = var_types_.at(idx->array_name);
        llvm::Value *base = it->second;

        llvm::Value *index = codegen_expr(*idx->index);
        if (index->getType()->isIntegerTy(1))
        {
            index = builder_->CreateZExt(index, builder_->getInt32Ty());
        }

        llvm::Value *zero = builder_->getInt32(0);
        llvm::Value *offset = builder_->CreateSub(index, builder_->getInt32(arr_type.lo));
        return builder_->CreateInBoundsGEP(llvm_type_of(arr_type), base, {zero, offset});
    }

    throw std::runtime_error("CodeGen Error: Invalid lvalue");
}

llvm::Value *CodeGenerator::codegen_expr(const ExprAST &expr)
{
    if (auto lit = dynamic_cast<const LiteralExprAST *>(&expr))
    {
        return const_literal(lit->value);
    }

    if (auto var = dynamic_cast<const VariableExprAST *>(&expr))
    {
        auto c = consts_.find(var->name);
        if (c != consts_.end())
        {
            return const_literal(c->second);
        }
        auto it = values_.find(var->name);
        if (it == values_.end())
        {
            throw std::runtime_error("CodeGen Error: Unknown variable " + var->name);
        }
        TypeRef type = var_types_.at(var->name);
        if (type.is_array())
        {
            throw std::runtime_error("CodeGen Error: array used as a scalar: " + var->name);
        }
        return builder_->CreateLoad(llvm_type_of(type), it->second, var->name);
    }

    if (auto idx = dynamic_cast<const IndexExprAST *>(&expr))
    {
        TypeRef elem = TypeRef::scalar_type(var_types_.at(idx->array_name).elem);
        return builder_->CreateLoad(llvm_type_of(elem), codegen_address(expr), "elem");
    }

    if (auto un = dynamic_cast<const UnaryExprAST *>(&expr))
    {
        llvm::Value *operand = codegen_expr(*un->operand);
        if (un->op == TokenType::Minus)
        {
            return operand->getType()->isDoubleTy() ? builder_->CreateFNeg(operand)
                                                    : builder_->CreateNeg(operand);
        }
        if (un->op == TokenType::KwNot)
        {
            if (operand->getType()->isIntegerTy(1))
            {
                return builder_->CreateNot(operand);
            }
            return builder_->CreateXor(operand,
                                       llvm::ConstantInt::getAllOnesValue(operand->getType()));
        }
        throw std::runtime_error("CodeGen Error: Unsupported unary operator");
    }

    if (auto bin = dynamic_cast<const BinaryExprAST *>(&expr))
    {
        llvm::Value *lhs = codegen_expr(*bin->lhs);
        llvm::Value *rhs = codegen_expr(*bin->rhs);

        bool is_float = lhs->getType()->isDoubleTy() || rhs->getType()->isDoubleTy();
        if (is_float)
        {
            if (lhs->getType()->isIntegerTy() && !lhs->getType()->isIntegerTy(1))
            {
                lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
            }
            if (rhs->getType()->isIntegerTy() && !rhs->getType()->isIntegerTy(1))
            {
                rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
            }
        }

        switch (bin->op)
        {
        case TokenType::Plus:
            return is_float ? builder_->CreateFAdd(lhs, rhs, "addtmp")
                            : builder_->CreateAdd(lhs, rhs, "addtmp");
        case TokenType::Minus:
            return is_float ? builder_->CreateFSub(lhs, rhs, "subtmp")
                            : builder_->CreateSub(lhs, rhs, "subtmp");
        case TokenType::Star:
            return is_float ? builder_->CreateFMul(lhs, rhs, "multmp")
                            : builder_->CreateMul(lhs, rhs, "multmp");
        case TokenType::Slash:
            if (lhs->getType()->isIntegerTy())
            {
                lhs = builder_->CreateSIToFP(lhs, builder_->getDoubleTy());
            }
            if (rhs->getType()->isIntegerTy())
            {
                rhs = builder_->CreateSIToFP(rhs, builder_->getDoubleTy());
            }
            return builder_->CreateFDiv(lhs, rhs, "divtmp");

        case TokenType::KwDiv:
            return builder_->CreateSDiv(lhs, rhs, "divtmp");
        case TokenType::KwMod:
            return builder_->CreateSRem(lhs, rhs, "modtmp");
        case TokenType::KwAnd:
            return builder_->CreateAnd(lhs, rhs, "andtmp");
        case TokenType::KwOr:
            return builder_->CreateOr(lhs, rhs, "ortmp");

        case TokenType::Equal:
            return is_float ? builder_->CreateFCmpOEQ(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpEQ(lhs, rhs, "cmptmp");
        case TokenType::NotEqual:
            return is_float ? builder_->CreateFCmpONE(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpNE(lhs, rhs, "cmptmp");
        case TokenType::LessThan:
            return is_float ? builder_->CreateFCmpOLT(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpSLT(lhs, rhs, "cmptmp");
        case TokenType::LessEq:
            return is_float ? builder_->CreateFCmpOLE(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpSLE(lhs, rhs, "cmptmp");
        case TokenType::GreatThan:
            return is_float ? builder_->CreateFCmpOGT(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpSGT(lhs, rhs, "cmptmp");
        case TokenType::GreatEq:
            return is_float ? builder_->CreateFCmpOGE(lhs, rhs, "cmptmp")
                            : builder_->CreateICmpSGE(lhs, rhs, "cmptmp");

        default:
            throw std::runtime_error("CodeGen Error: Unsupported binary operator");
        }
    }

    if (auto call = dynamic_cast<const CallExprAST *>(&expr))
    {
        const SubDeclAST *sub = subprograms_.at(call->callee);
        std::vector<llvm::Value *> args;
        codegen_call_args(sub->params, call->args, args);
        llvm::Function *callee = builder_->GetInsertBlock()->getModule()->getFunction(call->callee);
        return builder_->CreateCall(callee, args, "calltmp");
    }

    throw std::runtime_error("CodeGen Error: Invalid expression");
}

void CodeGenerator::codegen_call_args(const std::vector<ParamDecl> &params,
                                      const std::vector<ExprPtr> &args,
                                      std::vector<llvm::Value *> &out)
{
    for (size_t i = 0; i < params.size(); ++i)
    {
        const ParamDecl &p = params[i];
        if (p.is_var)
        {
            out.push_back(codegen_address(*args[i]));
        }
        else
        {
            llvm::Value *val = codegen_expr(*args[i]);
            if (p.type.scalar == TokenType::KwReal && val->getType()->isIntegerTy(32))
            {
                val = builder_->CreateSIToFP(val, builder_->getDoubleTy());
            }
            out.push_back(val);
        }
    }
}

void CodeGenerator::codegen_writeln(const std::vector<ExprPtr> &args)
{
    llvm::Module *mod = builder_->GetInsertBlock()->getModule();
    llvm::FunctionCallee printf_fn = get_or_declare_printf(mod);

    if (args.empty())
    {
        llvm::Value *fmt = builder_->CreateGlobalString("\n", "fmt_nl");
        builder_->CreateCall(printf_fn, {fmt});
        return;
    }

    for (size_t i = 0; i < args.size(); ++i)
    {
        llvm::Value *val = codegen_expr(*args[i]);
        const char *spec = "%d";
        if (val->getType()->isDoubleTy())
        {
            spec = "%f";
        }
        else if (val->getType()->isIntegerTy(1))
        {
            val = builder_->CreateZExt(val, builder_->getInt32Ty());
        }
        else if (val->getType()->isIntegerTy(8))
        {
            spec = "%c";
        }
        else if (val->getType()->isPointerTy())
        {
            spec = "%s";
        }

        std::string fmt_str = std::string(spec) + (i + 1 == args.size() ? "\n" : "");
        llvm::Value *fmt = builder_->CreateGlobalString(fmt_str, "fmt");
        builder_->CreateCall(printf_fn, {fmt, val});
    }
}

void CodeGenerator::codegen_stmt(const StmtAST &stmt)
{
    if (auto assign = dynamic_cast<const AssignStmtAST *>(&stmt))
    {
        llvm::Value *addr = codegen_address(*assign->target);
        llvm::Value *val = codegen_expr(*assign->expr);

        TokenType target_type = TokenType::KwInteger;
        if (auto v = dynamic_cast<const VariableExprAST *>(assign->target.get()))
        {
            target_type = var_types_.at(v->name).scalar;
        }
        else if (auto ix = dynamic_cast<const IndexExprAST *>(assign->target.get()))
        {
            target_type = var_types_.at(ix->array_name).elem;
        }
        val = coerce_to_type(val, target_type);
        builder_->CreateStore(val, addr);
    }
    else if (auto compound = dynamic_cast<const CompoundStmtAST *>(&stmt))
    {
        for (const auto &child : compound->statements)
        {
            if (child)
            {
                codegen_stmt(*child);
            }
        }
    }
    else if (auto ifs = dynamic_cast<const IfStmtAST *>(&stmt))
    {
        llvm::Value *cond = codegen_expr(*ifs->condition);

        llvm::Function *func = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *then_bb = llvm::BasicBlock::Create(*context_, "then", func);
        llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(*context_, "else");
        llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context_, "ifcont");

        builder_->CreateCondBr(cond, then_bb, else_bb);

        builder_->SetInsertPoint(then_bb);
        if (ifs->then_branch)
        {
            codegen_stmt(*ifs->then_branch);
        }
        builder_->CreateBr(merge_bb);

        func->insert(func->end(), else_bb);
        builder_->SetInsertPoint(else_bb);
        if (ifs->else_branch)
        {
            codegen_stmt(*ifs->else_branch);
        }
        builder_->CreateBr(merge_bb);

        func->insert(func->end(), merge_bb);
        builder_->SetInsertPoint(merge_bb);
    }
    else if (auto wh = dynamic_cast<const WhileStmtAST *>(&stmt))
    {
        llvm::Function *func = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *cond_bb = llvm::BasicBlock::Create(*context_, "whilecond", func);
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(*context_, "whilebody");
        llvm::BasicBlock *end_bb = llvm::BasicBlock::Create(*context_, "whileend");

        builder_->CreateBr(cond_bb);
        builder_->SetInsertPoint(cond_bb);
        llvm::Value *cond = codegen_expr(*wh->condition);
        builder_->CreateCondBr(cond, body_bb, end_bb);

        func->insert(func->end(), body_bb);
        builder_->SetInsertPoint(body_bb);
        if (wh->body)
        {
            codegen_stmt(*wh->body);
        }
        builder_->CreateBr(cond_bb);

        func->insert(func->end(), end_bb);
        builder_->SetInsertPoint(end_bb);
    }
    else if (auto fo = dynamic_cast<const ForStmtAST *>(&stmt))
    {
        llvm::Value *var_addr = values_.at(fo->var_name);
        llvm::Value *start = codegen_expr(*fo->start);
        llvm::Value *end = codegen_expr(*fo->end);
        builder_->CreateStore(start, var_addr);

        llvm::Function *func = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *cond_bb = llvm::BasicBlock::Create(*context_, "forcond", func);
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(*context_, "forbody");
        llvm::BasicBlock *end_bb = llvm::BasicBlock::Create(*context_, "forend");

        builder_->CreateBr(cond_bb);
        builder_->SetInsertPoint(cond_bb);
        llvm::Value *cur = builder_->CreateLoad(builder_->getInt32Ty(), var_addr, fo->var_name);
        llvm::Value *done =
            fo->downto ? builder_->CreateICmpSLT(cur, end) : builder_->CreateICmpSGT(cur, end);
        builder_->CreateCondBr(done, end_bb, body_bb);

        func->insert(func->end(), body_bb);
        builder_->SetInsertPoint(body_bb);
        if (fo->body)
        {
            codegen_stmt(*fo->body);
        }
        llvm::Value *cur2 = builder_->CreateLoad(builder_->getInt32Ty(), var_addr);
        llvm::Value *next = fo->downto ? builder_->CreateSub(cur2, builder_->getInt32(1))
                                       : builder_->CreateAdd(cur2, builder_->getInt32(1));
        builder_->CreateStore(next, var_addr);
        builder_->CreateBr(cond_bb);

        func->insert(func->end(), end_bb);
        builder_->SetInsertPoint(end_bb);
    }
    else if (auto rep = dynamic_cast<const RepeatStmtAST *>(&stmt))
    {
        llvm::Function *func = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *body_bb = llvm::BasicBlock::Create(*context_, "repbody", func);
        llvm::BasicBlock *cond_bb = llvm::BasicBlock::Create(*context_, "repcond");
        llvm::BasicBlock *end_bb = llvm::BasicBlock::Create(*context_, "repend");

        builder_->CreateBr(body_bb);
        builder_->SetInsertPoint(body_bb);
        if (rep->body)
        {
            codegen_stmt(*rep->body);
        }
        builder_->CreateBr(cond_bb);

        func->insert(func->end(), cond_bb);
        builder_->SetInsertPoint(cond_bb);
        llvm::Value *cond = codegen_expr(*rep->condition);
        builder_->CreateCondBr(cond, end_bb, body_bb);

        func->insert(func->end(), end_bb);
        builder_->SetInsertPoint(end_bb);
    }
    else if (auto cas = dynamic_cast<const CaseStmtAST *>(&stmt))
    {
        llvm::Function *func = builder_->GetInsertBlock()->getParent();
        llvm::BasicBlock *merge_bb = llvm::BasicBlock::Create(*context_, "caseend", func);
        llvm::BasicBlock *else_bb = llvm::BasicBlock::Create(*context_, "caseelse");

        llvm::Value *selector = codegen_expr(*cas->expr);
        if (selector->getType()->isIntegerTy(8))
        {
            selector = builder_->CreateZExt(selector, builder_->getInt32Ty());
        }

        llvm::SwitchInst *sw =
            builder_->CreateSwitch(selector, else_bb, static_cast<unsigned>(cas->arms.size()));

        for (const auto &arm : cas->arms)
        {
            llvm::BasicBlock *arm_bb = llvm::BasicBlock::Create(*context_, "casearm");
            for (const auto &label : arm.labels)
            {
                int value = 0;
                if (auto pVal = std::get_if<int>(&label))
                {
                    value = *pVal;
                }
                else if (auto pStr = std::get_if<std::string>(&label))
                {
                    value = static_cast<int>(static_cast<unsigned char>((*pStr)[0]));
                }
                sw->addCase(builder_->getInt32(value), arm_bb);
            }
            func->insert(func->end(), arm_bb);
            builder_->SetInsertPoint(arm_bb);
            if (arm.stmt)
            {
                codegen_stmt(*arm.stmt);
            }
            builder_->CreateBr(merge_bb);
        }

        func->insert(func->end(), else_bb);
        builder_->SetInsertPoint(else_bb);
        if (cas->else_branch)
        {
            codegen_stmt(*cas->else_branch);
        }
        builder_->CreateBr(merge_bb);

        builder_->SetInsertPoint(merge_bb);
    }
    else if (auto wr = dynamic_cast<const WritelnStmtAST *>(&stmt))
    {
        codegen_writeln(wr->args);
    }
    else if (auto call = dynamic_cast<const CallStmtAST *>(&stmt))
    {
        const SubDeclAST *sub = subprograms_.at(call->callee);
        std::vector<llvm::Value *> args;
        codegen_call_args(sub->params, call->args, args);
        llvm::Function *callee = builder_->GetInsertBlock()->getModule()->getFunction(call->callee);
        builder_->CreateCall(callee, args);
    }
}

void CodeGenerator::print_ir(const llvm::Module &mod) const
{
    mod.print(llvm::outs(), nullptr);
}

void CodeGenerator::emit_object_file(llvm::Module &mod, const std::string &filename)
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::Triple target_triple(llvm::sys::getDefaultTargetTriple());
    mod.setTargetTriple(target_triple);

    std::string error;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if (!target)
    {
        throw std::runtime_error("LLVM Target Lookup Error: " + error);
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions opt;
    auto reloc_model = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);

    std::unique_ptr<llvm::TargetMachine> target_machine(
        target->createTargetMachine(target_triple, cpu, features, opt, reloc_model));
    if (!target_machine)
    {
        throw std::runtime_error("Could not create LLVM TargetMachine");
    }

    mod.setDataLayout(target_machine->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::FA_Write);
    if (ec)
    {
        throw std::runtime_error("Could not open output file " + filename + ": " + ec.message());
    }

    llvm::legacy::PassManager pass;
    auto file_type = llvm::CodeGenFileType::ObjectFile;
    if (target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type))
    {
        throw std::runtime_error("TargetMachine cannot emit an object file of this type");
    }

    pass.run(mod);
    dest.flush();
}

} // namespace Pascal
