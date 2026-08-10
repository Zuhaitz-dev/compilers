#pragma once

#include "pascal/AST/AST.hpp"
#include "pascal/AST/ExprNodes.hpp"
#include "pascal/AST/StmtNodes.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/TargetParser/Triple.h>

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace Pascal
{

class CodeGenerator
{
public:
    CodeGenerator();

    // Emits LLVM IR for a complete program.
    std::unique_ptr<llvm::Module> emit_program(const ProgramAST &program);

    // Emits a generated module to a native .o file.
    void emit_object_file(llvm::Module &mod, const std::string &filename);

    void print_ir(const llvm::Module &mod) const;

private:
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;

    // name -> storage address (alloca, global, or var-param pointer).
    std::unordered_map<std::string, llvm::Value *> values_;
    // name -> declared type of the storage behind values_[name].
    std::unordered_map<std::string, TypeRef> var_types_;
    // name -> constant value.
    std::unordered_map<std::string, TokenValue> consts_;
    // name -> subprogram declaration.
    std::unordered_map<std::string, const SubDeclAST *> subprograms_;
    // name -> LLVM function (created before bodies are filled in).
    std::unordered_map<std::string, llvm::Function *> llvm_functions_;

    llvm::Value *const_literal(const TokenValue &value);
    llvm::Type *llvm_type_of(const TypeRef &type);
    llvm::Value *coerce_to_type(llvm::Value *v, TokenType to);
    llvm::FunctionCallee get_or_declare_printf(llvm::Module *mod);

    // Returns the address of an lvalue (variable or array element).
    llvm::Value *codegen_address(const ExprAST &expr);
    llvm::Value *codegen_expr(const ExprAST &expr);
    void codegen_stmt(const StmtAST &stmt);
    void codegen_writeln(const std::vector<ExprPtr> &args);
    void codegen_call_args(const std::vector<ParamDecl> &params, const std::vector<ExprPtr> &args,
                           std::vector<llvm::Value *> &out);
    void codegen_subprogram(const SubDeclAST &sub);
};

} // namespace Pascal
