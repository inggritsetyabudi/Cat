#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "diagnostics/diagnostics.h"
#include "lexer/lexer.h"

namespace claw::frontend {

struct AstNode {
    SourceSpan span;
    virtual ~AstNode() = default;
};

struct TypeNode : public AstNode {
    std::string name;
    bool isTuple = false;
    std::string viewKind;
    std::string scopeName;
    std::string viewScope;
    std::vector<std::unique_ptr<TypeNode>> params;
};

struct ImportItem {
    std::string name;
    std::string alias;
};

struct Expr : public AstNode {};

struct BoolExpr : public Expr {
    bool value = false;
};

struct IntExpr : public Expr {
    std::string value;
};

struct FloatExpr : public Expr {
    std::string value;
};

struct StringExpr : public Expr {
    std::string value;
};

struct CharExpr : public Expr {
    uint32_t value = 0;
};

struct TupleExpr : public Expr {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct IdentExpr : public Expr {
    std::string name;
    std::string backendBindingKey;
};

struct BinaryExpr : public Expr {
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct UnaryExpr : public Expr {
    std::string op;
    std::unique_ptr<Expr> operand;
};

struct CallExpr : public Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MemberExpr : public Expr {
    std::unique_ptr<Expr> object;
    std::string member;
};

struct IndexExpr : public Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
};

struct BorrowExpr : public Expr {
    bool isMutable = false;
    std::string scopeName;
    std::unique_ptr<Expr> target;
};

struct ShapeInitField {
    SourceSpan span;
    std::string name;
    std::unique_ptr<Expr> value;
};

struct ShapeInitExpr : public Expr {
    std::string name;
    std::string scopeName;
    std::vector<ShapeInitField> fields;
};

struct Stmt : public AstNode {};

struct BlockStmt : public Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct ExprStmt : public Stmt {
    std::unique_ptr<Expr> expr;
};

struct ReturnStmt : public Stmt {
    std::unique_ptr<Expr> value;
};

struct AssignStmt : public Stmt {
    std::string op = "=";
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct BindingStmt : public Stmt {
    bool isMutable = false;
    std::string name;
    std::vector<std::string> tuplePatternNames;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> value;
    std::string backendBindingKey;
    std::vector<std::string> tuplePatternBindingKeys;
};

struct TryStmt : public Stmt {
    bool isMutable = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> expr;
    bool autoPropagate = false;
    std::string failName;
    std::unique_ptr<BlockStmt> failBlock;
    std::string backendBindingKey;
    std::string failBindingKey;
};

struct IfStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBlock;
    std::unique_ptr<BlockStmt> elseBlock;
};

struct IfExpr : public Expr {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBlock;
    std::unique_ptr<BlockStmt> elseBlock;
};

struct LoopStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;
};

struct ScanStmt : public Stmt {
    std::string itemName;
    std::string indexName;
    std::string itemBindingKey;
    std::string indexBindingKey;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<BlockStmt> body;
};

struct ScopeStmt : public Stmt {
    std::string name;
    std::unique_ptr<BlockStmt> body;
};

struct StopStmt : public Stmt {};
struct SkipStmt : public Stmt {};

enum class PickPatternKind {
    Wildcard,
    Binding,
    Tuple,
};

struct PickPattern {
    SourceSpan span;
    PickPatternKind kind = PickPatternKind::Wildcard;
    std::string name;
    std::vector<std::unique_ptr<PickPattern>> elements;
    std::string backendBindingKey;
};

struct PickBranch {
    SourceSpan span;
    std::string tag;
    std::vector<std::string> bindings;
    std::unique_ptr<BlockStmt> body;
    std::unique_ptr<PickPattern> tuplePattern;
    std::vector<std::string> bindingKeys;
};

struct PickStmt : public Stmt {
    std::unique_ptr<Expr> value;
    std::vector<PickBranch> branches;
};

struct PickExpr : public Expr {
    std::unique_ptr<Expr> value;
    std::vector<PickBranch> branches;
};


struct RawStmt : public Stmt {
    std::unique_ptr<BlockStmt> body;
};

struct Decl : public AstNode {
    bool isShared = false;
};

struct FnParam {
    SourceSpan span;
    std::string name;
    std::unique_ptr<TypeNode> type;
    bool isSelf = false;
};

struct FnDecl : public Decl {
    std::string name;
    std::vector<FnParam> params;
    std::unique_ptr<TypeNode> returnType;
    std::unique_ptr<BlockStmt> body;
};

struct ShapeField {
    SourceSpan span;
    bool isShared = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
};

struct ShapeDecl : public Decl {
    std::string name;
    bool isViewShape = false;
    std::string scopeParamName;
    std::vector<std::string> typeParams;
    std::vector<ShapeField> fields;
};

struct ChoiceVariant {
    SourceSpan span;
    std::string tag;
    std::vector<FnParam> payloads;
};

struct ChoiceDecl : public Decl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<ChoiceVariant> variants;
};

struct ImplementsDecl : public Decl {
    std::string typeName;
    std::vector<std::string> typeParams;
    std::optional<std::string> contractName;
    std::vector<std::unique_ptr<FnDecl>> methods;
    SourceSpan span;
};

struct ContractDecl : public Decl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<std::unique_ptr<FnDecl>> methods;
    SourceSpan span;
};

struct StaticDecl : public Decl {
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> init;
    SourceSpan span;
};

struct ImportDecl {
    SourceSpan span;
    std::string modulePath;
    std::string alias;
    std::vector<ImportItem> items;
    bool isSuper = false;
};

struct RealmDecl : public AstNode {
    std::string name;
    std::vector<ImportDecl> imports;
    std::vector<std::unique_ptr<Decl>> declarations;
};

} // namespace claw::frontend
