#include "parser/parser.h"

#include <stdexcept>
#include <utility>

namespace claw::frontend {

Parser::Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

namespace {

void collectPickPatternBindings(const PickPattern* pattern, std::vector<std::string>& bindings) {
    if (!pattern) {
        return;
    }
    if (pattern->kind == PickPatternKind::Binding) {
        bindings.push_back(pattern->name);
        return;
    }
    for (const auto& element : pattern->elements) {
        collectPickPatternBindings(element.get(), bindings);
    }
}

} // namespace

const Token& Parser::peek() const {
    return tokens[pos];
}

const Token& Parser::previous() const {
    return pos > 0 ? tokens[pos - 1] : tokens[0];
}

Token Parser::advance() {
    if (!isAtEnd()) {
        ++pos;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const {
    if (isAtEnd()) {
        return kind == TokenKind::Eof;
    }
    return peek().kind == kind;
}

bool Parser::checkAhead(size_t offset, TokenKind kind) const {
    return pos + offset < tokens.size() && tokens[pos + offset].kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consumeToken(TokenKind kind, const std::string& message) {
    if (!check(kind)) {
        failAtCurrent(message);
    }

    const Token& token = peek();
    advance();
    return token;
}

const Token& Parser::consumeNameToken(const std::string& message) {
    if (!isNameToken(peek().kind)) {
        failAtCurrent(message);
    }
    const Token& token = peek();
    advance();
    return token;
}

void Parser::consume(TokenKind kind, const std::string& message) {
    consumeToken(kind, message);
}

bool Parser::isAtEnd() const {
    return peek().kind == TokenKind::Eof;
}

bool Parser::isNameToken(TokenKind kind) const {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::KwFail:
    case TokenKind::KwView:

    case TokenKind::KwRef:
    case TokenKind::KwMut:

    case TokenKind::KwAs:
    case TokenKind::KwOver:
    case TokenKind::KwSuper:
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
    case TokenKind::KwSelf:
        return true;
    default:
        return false;
    }
}

bool Parser::matchNameOrKeyword() {
    if (isNameToken(peek().kind)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::isTopLevelRecoveryPoint(TokenKind kind) const {
    return kind == TokenKind::KwImport || kind == TokenKind::KwFn || kind == TokenKind::KwShape ||
           kind == TokenKind::KwChoice || kind == TokenKind::KwShare || kind == TokenKind::Eof;
}

bool Parser::isStatementRecoveryPoint(TokenKind kind) const {
    return kind == TokenKind::KwVal || kind == TokenKind::KwVar ||
           kind == TokenKind::KwReturn || kind == TokenKind::KwIf ||
           kind == TokenKind::KwLoop || kind == TokenKind::KwScan || kind == TokenKind::KwScope ||
           kind == TokenKind::KwPick ||
           kind == TokenKind::KwStop || kind == TokenKind::KwSkip ||
           kind == TokenKind::KwRaw || kind == TokenKind::KwScope ||
           kind == TokenKind::RBrace || isTopLevelRecoveryPoint(kind);
}

void Parser::recordDiagnostic(const DiagnosticError& error) {
    diagnostics.insert(diagnostics.end(), error.diagnostics().begin(), error.diagnostics().end());
}

void Parser::synchronizeTopLevel() {
    while (!isAtEnd()) {
        if (isTopLevelRecoveryPoint(peek().kind)) {
            return;
        }
        advance();
    }
}

void Parser::synchronizeStatement() {
    while (!isAtEnd()) {
        if (isStatementRecoveryPoint(peek().kind)) {
            return;
        }
        advance();
    }
}

SourceSpan Parser::spanFromToken(const Token& token) const {
    return SourceSpan{token.line, token.column, token.length == 0 ? 1 : token.length};
}

[[noreturn]] void Parser::failAtCurrent(const std::string& message) {
    failAt(peek(), message);
}

[[noreturn]] void Parser::failAt(const Token& token, const std::string& message) {
    throw DiagnosticError(
        "Parsing failed.",
        std::vector<Diagnostic>{Diagnostic{"parse", message, spanFromToken(token)}});
}

ImportItem Parser::parseImportItem() {
    ImportItem item;
    consumeNameToken("Expected import item name");
    item.name = previous().text;
    if (match(TokenKind::KwAs)) {
        consumeNameToken("Expected import alias after 'as'");
        item.alias = previous().text;
    }
    return item;
}

ImportDecl Parser::parseImportDeclaration() {
    ImportDecl imp;
    imp.span = spanFromToken(previous());

    if (match(TokenKind::KwSuper)) {
        imp.isSuper = true;
        imp.modulePath = "super";
        consume(TokenKind::Dot, "Expected '.' after 'super'");
        consume(TokenKind::LBrace, "Expected '{' for import items");
        if (!check(TokenKind::RBrace)) {
            do {
                imp.items.push_back(parseImportItem());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBrace, "Expected '}' after import items");
        return imp;
    }

    consumeNameToken("Expected module name");
    imp.modulePath = previous().text;
    while (match(TokenKind::Dot)) {
        if (check(TokenKind::LBrace)) {
            consume(TokenKind::LBrace, "Expected '{' for import items");
            if (!check(TokenKind::RBrace)) {
                do {
                    imp.items.push_back(parseImportItem());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RBrace, "Expected '}' after import items");
            return imp;
        }

        consumeNameToken("Expected module path segment");
        imp.modulePath += "." + previous().text;
    }

    if (match(TokenKind::KwAs)) {
        consumeNameToken("Expected module alias after 'as'");
        imp.alias = previous().text;
    }

    return imp;
}

void Parser::parseTypeParameterList(std::vector<std::string>* out) {
    if (!out) {
        return;
    }

    if (match(TokenKind::LBracket)) {
        if (!check(TokenKind::RBracket)) {
            do {
                consumeNameToken("Expected type parameter");
                out->push_back(previous().text);
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RBracket, "Expected ']' after type parameters");
    }
}

std::vector<std::unique_ptr<TypeNode>> Parser::parseBracketTypeArguments() {
    std::vector<std::unique_ptr<TypeNode>> params;
    consume(TokenKind::LBracket, "Expected '[' to start type arguments");
    if (!check(TokenKind::RBracket)) {
        do {
            params.push_back(parseType());
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RBracket, "Expected ']' after type arguments");
    return params;
}

std::unique_ptr<RealmDecl> Parser::parseFile() {
    auto realm = std::make_unique<RealmDecl>();
    realm->span = spanFromToken(peek());



    while (!isAtEnd()) {
        try {
            if (match(TokenKind::KwImport)) {
                realm->imports.push_back(parseImportDeclaration());
                continue;
            }

            auto decl = parseDeclaration();
            if (decl) {
                realm->declarations.push_back(std::move(decl));
            }
        } catch (const DiagnosticError& error) {
            recordDiagnostic(error);
            synchronizeTopLevel();
        }
    }

    if (!diagnostics.empty()) {
        throw DiagnosticError(
            "Parsing failed with " + std::to_string(diagnostics.size()) + " errors.",
            diagnostics);
    }

    return realm;
}

std::unique_ptr<Decl> Parser::parseDeclaration() {
    bool isShared = match(TokenKind::KwShare);
    const bool isViewShape = (check(TokenKind::KwView) || (check(TokenKind::Identifier) && peek().text == "view")) &&
                             checkAhead(1, TokenKind::KwShape);
    if (isViewShape) {
        advance();
    }

    if (match(TokenKind::KwFn)) {
        if (isViewShape) {
            failAt(previous(), "'view' may only be used with 'shape'.");
        }
        auto fn = parseFnDeclaration();
        fn->isShared = isShared;
        return fn;
    }
    if (match(TokenKind::KwShape)) {
        auto shape = parseShapeDeclaration(isViewShape);
        shape->isShared = isShared;
        return shape;
    }
    if (match(TokenKind::KwChoice)) {
        if (isViewShape) {
            failAt(previous(), "'view' may only be used with 'shape'.");
        }
        auto choice = parseChoiceDeclaration();
        choice->isShared = isShared;
        return choice;
    }
    if (isViewShape) {
        failAt(previous(), "Expected 'shape' after 'view'.");
    }
    if (match(TokenKind::KwImplements)) {
        return parseImplementsDeclaration();
    }
    if (match(TokenKind::KwContract)) {
        return parseContractDeclaration();
    }
    if (match(TokenKind::KwStatic)) {
        if (isShared) {
            failAt(previous(), "`static` cannot be combined with `share`.");
        }
        auto stat = std::make_unique<StaticDecl>();
        stat->span = spanFromToken(previous());
        consumeNameToken("Expected static name");
        stat->name = previous().text;
        consume(TokenKind::Colon, "Expected ':' after static name");
        stat->type = parseType();
        consume(TokenKind::Equal, "Expected '=' for static initializer");
        stat->init = parseExpression();
        return stat;
    }
    failAtCurrent("Unexpected token at top level.");
}

std::unique_ptr<FnDecl> Parser::parseFnDeclaration() {
    auto fn = std::make_unique<FnDecl>();
    fn->span = spanFromToken(previous());

    consumeNameToken("Expected function name");
    fn->name = previous().text;

    consume(TokenKind::LParen, "Expected '(' for parameters");
    if (!check(TokenKind::RParen)) {
        do {
            FnParam param;
            if (match(TokenKind::KwSelf)) {
                param.span = spanFromToken(previous());
                param.name = "self";
                param.isSelf = true;
                if (match(TokenKind::Colon)) {
                    if (match(TokenKind::KwRef)) {
                        auto selfType = std::make_unique<TypeNode>();
                        selfType->span = spanFromToken(previous());
                        selfType->viewKind = match(TokenKind::KwMut) ? "edit" : "look";
                        selfType->name = "Self";
                        param.type = std::move(selfType);
                    } else {
                        param.type = parseType();
                    }
                } else {
                    auto selfType = std::make_unique<TypeNode>();
                    selfType->span = param.span;
                    selfType->name = "Self";
                    param.type = std::move(selfType);
                }
            } else {
                consumeNameToken("Expected parameter name");
                param.span = spanFromToken(previous());
                param.name = previous().text;
                consume(TokenKind::Colon, "Expected ':' after param name");
                param.type = parseType();
            }
            fn->params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "Expected ')' after parameters");

    if (match(TokenKind::Arrow)) {
        fn->returnType = parseType();
    }

    if (check(TokenKind::LBrace)) {
        fn->body = parseBlock();
    }
    return fn;
}

std::unique_ptr<ShapeDecl> Parser::parseShapeDeclaration(bool isViewShape) {
    auto shape = std::make_unique<ShapeDecl>();
    shape->span = spanFromToken(previous());
    shape->isViewShape = isViewShape;

    consumeNameToken("Expected shape name");
    shape->name = previous().text;
    if (isViewShape) {
        consume(TokenKind::LBracket, "Expected '[' after view shape name.");
        consumeNameToken("Expected named scope parameter for view shape.");
        shape->scopeParamName = previous().text;
        consume(TokenKind::RBracket, "Expected ']' after view shape scope parameter.");
    } else {
        parseTypeParameterList(&shape->typeParams);
    }

    consume(TokenKind::LBrace, "Expected '{' to start shape body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        ShapeField field;
        field.isShared = match(TokenKind::KwShare);
        consumeNameToken("Expected field name");
        field.span = spanFromToken(previous());
        field.name = previous().text;
        consume(TokenKind::Colon, "Expected ':' after field name");
        field.type = parseType();
        shape->fields.push_back(std::move(field));
        match(TokenKind::Comma);
    }
    consume(TokenKind::RBrace, "Expected '}' to end shape body");
    return shape;
}

std::unique_ptr<ImplementsDecl> Parser::parseImplementsDeclaration() {
    auto impl = std::make_unique<ImplementsDecl>();
    impl->span = spanFromToken(previous());

    consumeNameToken("Expected shape name after 'implements'");
    impl->typeName = previous().text;
    parseTypeParameterList(&impl->typeParams);

    if (match(TokenKind::KwWith)) {
        consumeNameToken("Expected contract name after 'with'");
        impl->contractName = previous().text;
    }

    consume(TokenKind::LBrace, "Expected '{' to start implements body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        if (!match(TokenKind::KwFn)) {
            failAtCurrent("Expected 'fn' inside 'implements' block");
        }
        auto fn = parseFnDeclaration();
        impl->methods.push_back(std::move(fn));
    }
    consume(TokenKind::RBrace, "Expected '}' to end implements body");
    return impl;
}

std::unique_ptr<ContractDecl> Parser::parseContractDeclaration() {
    auto contract = std::make_unique<ContractDecl>();
    contract->span = spanFromToken(previous());

    consumeNameToken("Expected contract name");
    contract->name = previous().text;
    parseTypeParameterList(&contract->typeParams);

    consume(TokenKind::LBrace, "Expected '{' to start contract body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        if (!match(TokenKind::KwFn)) {
            failAtCurrent("Expected 'fn' inside 'contract' block");
        }
        auto fn = parseFnDeclaration();
        // In contracts, methods usually have no body. 
        // parseFnDeclaration() parses an optional block if '{' is present, otherwise expects no block.
        // If there's no block, fn->body will be null, which is correct for contract interfaces.
        contract->methods.push_back(std::move(fn));
    }
    consume(TokenKind::RBrace, "Expected '}' to end contract body");
    return contract;
}

std::unique_ptr<ChoiceDecl> Parser::parseChoiceDeclaration() {
    auto choice = std::make_unique<ChoiceDecl>();
    choice->span = spanFromToken(previous());

    consumeNameToken("Expected choice name");
    choice->name = previous().text;
    parseTypeParameterList(&choice->typeParams);

    consume(TokenKind::LBrace, "Expected '{' to start choice body");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        ChoiceVariant variant;
        if (!matchNameOrKeyword()) {
            failAtCurrent("Expected variant tag");
        }
        variant.span = spanFromToken(previous());
        variant.tag = previous().text;

        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do {
                    FnParam payload;
                    consumeNameToken("Expected payload name");
                    payload.span = spanFromToken(previous());
                    payload.name = previous().text;
                    consume(TokenKind::Colon, "Expected ':' after payload name");
                    payload.type = parseType();
                    variant.payloads.push_back(std::move(payload));
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after payloads");
        }

        choice->variants.push_back(std::move(variant));
    }
    consume(TokenKind::RBrace, "Expected '}' to end choice body");
    return choice;
}

std::unique_ptr<TypeNode> Parser::parseType() {
    auto type = std::make_unique<TypeNode>();
    const Token startToken = peek();
    type->span = spanFromToken(startToken);

    if (match(TokenKind::KwRef)) {
        if (match(TokenKind::KwMut)) {
            type->viewKind = "edit";
        } else {
            type->viewKind = "look";
        }
        if (match(TokenKind::LBracket)) {
            if (type->viewKind == "edit") {
                failAt(previous(), "`ref mut[s]` is not supported. Scope-bound borrows currently use `ref[s]`.");
            }
            consumeNameToken("Expected scope name inside ref[]");
            type->viewScope = previous().text;
            consume(TokenKind::RBracket, "Expected ']' after scope name");
        }
    }

    if (match(TokenKind::LParen)) {
        if (match(TokenKind::RParen)) {
            type->name = "Unit";
            return type;
        }

        auto first = parseType();
        if (!match(TokenKind::Comma)) {
            consume(TokenKind::RParen, "Expected ')' after grouped type");
            if (!type->viewKind.empty()) {
                first->viewKind = type->viewKind;
                first->viewScope = type->viewScope;
            }
            return first;
        }

        type->name = "Tuple";
        type->isTuple = true;
        type->params.push_back(std::move(first));
        while (!check(TokenKind::RParen) && !isAtEnd()) {
            type->params.push_back(parseType());
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        consume(TokenKind::RParen, "Expected ')' after tuple type");
        return type;
    }

    consumeNameToken("Expected type name");
    type->name = previous().text;

    if (check(TokenKind::LBracket)) {
        type->params = parseBracketTypeArguments();
    }

    return type;
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    const Token& openBrace = consumeToken(TokenKind::LBrace, "Expected '{' to start block");
    auto block = std::make_unique<BlockStmt>();
    block->span = spanFromToken(openBrace);

    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        try {
            block->statements.push_back(parseStatement());
        } catch (const DiagnosticError& error) {
            recordDiagnostic(error);
            synchronizeStatement();
        }
    }

    consume(TokenKind::RBrace, "Expected '}' to end block");
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (match(TokenKind::KwVal)) return parseBinding(false);
    if (match(TokenKind::KwVar)) return parseBinding(true);
    if (match(TokenKind::KwReturn)) return parseReturn();
    if (check(TokenKind::KwIf) || check(TokenKind::KwPick)) {
        auto expr = parseExpression();
        auto stmt = std::make_unique<ExprStmt>();
        stmt->span = expr->span;
        stmt->expr = std::move(expr);
        return stmt;
    }
    if (match(TokenKind::KwLoop)) return parseLoop();
    if (match(TokenKind::KwScan)) return parseScan();
    if (match(TokenKind::KwScope)) return parseScope();
    if (match(TokenKind::KwStop)) {
        auto stmt = std::make_unique<StopStmt>();
        stmt->span = spanFromToken(previous());
        return stmt;
    }
    if (match(TokenKind::KwSkip)) {
        auto stmt = std::make_unique<SkipStmt>();
        stmt->span = spanFromToken(previous());
        return stmt;
    }
    if (match(TokenKind::KwRaw)) {
        auto raw = std::make_unique<RawStmt>();
        raw->span = spanFromToken(previous());
        raw->body = parseBlock();
        return raw;
    }

    auto expr = parseExpression();
    if (match(TokenKind::Equal) || match(TokenKind::PlusEqual) ||
        match(TokenKind::MinusEqual) || match(TokenKind::StarEqual) ||
        match(TokenKind::SlashEqual) || match(TokenKind::PercentEqual)) {
        auto assign = std::make_unique<AssignStmt>();
        assign->span = expr->span;
        assign->op = previous().text;
        assign->target = std::move(expr);
        assign->value = parseExpression();
        return assign;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->span = expr->span;
    stmt->expr = std::move(expr);
    return stmt;
}

std::unique_ptr<ReturnStmt> Parser::parseReturn() {
    auto returnStmt = std::make_unique<ReturnStmt>();
    returnStmt->span = spanFromToken(previous());
    if (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        returnStmt->value = parseExpression();
    }
    return returnStmt;
}

std::unique_ptr<Stmt> Parser::parseBinding(bool isMutable) {
    const SourceSpan bindingSpan = spanFromToken(previous());
    if (check(TokenKind::LParen)) {
        if (isMutable) {
            failAtCurrent("Tuple destructuring is currently supported only for `val` bindings.");
        }

        consume(TokenKind::LParen, "Expected `(` to begin tuple destructuring pattern.");
        std::vector<std::string> patternNames;
        bool sawComma = false;
        if (check(TokenKind::RParen)) {
            failAtCurrent("Tuple destructuring pattern must contain at least one binding.");
        }
        while (true) {
            if (check(TokenKind::LParen)) {
                failAtCurrent("Nested tuple patterns in bindings are not yet supported.");
            }
            consumeNameToken("Expected a variable name or `_` in tuple destructuring pattern.");
            patternNames.push_back(previous().text);
            if (!match(TokenKind::Comma)) {
                break;
            }
            sawComma = true;
            if (check(TokenKind::RParen)) {
                break;
            }
        }
        consume(TokenKind::RParen, "Expected `)` after tuple destructuring pattern.");
        if (patternNames.size() == 1 && !sawComma) {
            failAtCurrent("Tuple destructuring pattern must use a comma; write `(name,)` for a one-element tuple.");
        }
        if (check(TokenKind::Colon)) {
            failAtCurrent("Tuple destructuring type annotations are not supported; annotate the source tuple instead.");
        }
        if (!match(TokenKind::Equal)) {
            failAtCurrent("Tuple destructuring requires an initializer.");
        }
        if (check(TokenKind::KwTry)) {
            failAtCurrent("Tuple destructuring with `try` is not supported; bind the `try` result first.");
        }

        auto binding = std::make_unique<BindingStmt>();
        binding->span = bindingSpan;
        binding->tuplePatternNames = std::move(patternNames);
        binding->value = parseExpression();
        return binding;
    }
    consumeNameToken("Expected variable name");
    const std::string bindingName = previous().text;

    std::unique_ptr<TypeNode> bindingType;
    if (match(TokenKind::Colon)) {
        bindingType = parseType();
    }

    if (match(TokenKind::Equal)) {
        if (match(TokenKind::KwTry)) {
            auto stmt = std::make_unique<TryStmt>();
            stmt->span = bindingSpan;
            stmt->isMutable = isMutable;
            stmt->name = bindingName;
            stmt->type = std::move(bindingType);
            stmt->expr = parseExpression();

            if (match(TokenKind::KwElse)) {
                consumeNameToken("Expected failure binding name after 'else'");
                stmt->failName = previous().text;
                stmt->failBlock = parseBlock();
            } else {
                stmt->autoPropagate = true;
            }
            return stmt;
        }

        auto binding = std::make_unique<BindingStmt>();
        binding->span = bindingSpan;
        binding->isMutable = isMutable;
        binding->name = bindingName;
        binding->type = std::move(bindingType);
        binding->value = parseExpression();
        return binding;
    }

    auto binding = std::make_unique<BindingStmt>();
    binding->span = bindingSpan;
    binding->isMutable = isMutable;
    binding->name = bindingName;
    binding->type = std::move(bindingType);
    return binding;
}

std::unique_ptr<IfStmt> Parser::parseIf() {
    auto stmt = std::make_unique<IfStmt>();
    stmt->span = spanFromToken(previous());
    stmt->condition = parseExpression();
    stmt->thenBlock = parseBlock();

    if (match(TokenKind::KwElse)) {
        stmt->elseBlock = parseBlock();
    }

    return stmt;
}

std::unique_ptr<LoopStmt> Parser::parseLoop() {
    auto stmt = std::make_unique<LoopStmt>();
    stmt->span = spanFromToken(previous());

    if (!check(TokenKind::LBrace)) {
        stmt->condition = parseExpression();
    }
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<ScanStmt> Parser::parseScan() {
    auto stmt = std::make_unique<ScanStmt>();
    stmt->span = spanFromToken(previous());
    consumeNameToken("Expected iteration variable name");
    stmt->itemName = previous().text;
    if (match(TokenKind::Comma)) {
        consumeNameToken("Expected scan index variable name after ','");
        stmt->indexName = previous().text;
    }
    consume(TokenKind::KwOver, "Expected 'over' after scan variable");
    stmt->iterable = parseExpression();
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<ScopeStmt> Parser::parseScope() {
    auto stmt = std::make_unique<ScopeStmt>();
    stmt->span = spanFromToken(previous());
    consumeNameToken("Expected scope name after 'scope'");
    stmt->name = previous().text;
    stmt->body = parseBlock();
    return stmt;
}

std::unique_ptr<PickStmt> Parser::parsePick() {
    auto stmt = std::make_unique<PickStmt>();
    stmt->span = spanFromToken(previous());
    stmt->value = parseExpression();

    consume(TokenKind::LBrace, "Expected '{' to start pick block");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        PickBranch branch;
        if (check(TokenKind::LParen)) {
            branch.tuplePattern = parsePickPattern();
            branch.span = branch.tuplePattern->span;
            collectPickPatternBindings(branch.tuplePattern.get(), branch.bindings);
        } else {
            if (!matchNameOrKeyword()) {
                failAtCurrent("Expected variant tag, '_' or tuple pattern in pick branch");
            }
            branch.span = spanFromToken(previous());
            branch.tag = previous().text;

            if (match(TokenKind::LParen)) {
                if (!check(TokenKind::RParen)) {
                    do {
                        consumeNameToken("Expected binding name");
                        branch.bindings.push_back(previous().text);
                    } while (match(TokenKind::Comma));
                }
                consume(TokenKind::RParen, "Expected ')' after pattern bindings");
            }
        }

        branch.body = parseBlock();
        stmt->branches.push_back(std::move(branch));
    }
    consume(TokenKind::RBrace, "Expected '}' to end pick block");
    return stmt;
}

std::unique_ptr<PickPattern> Parser::parsePickPattern() {
    if (match(TokenKind::LParen)) {
        const Token open = previous();
        auto pattern = std::make_unique<PickPattern>();
        pattern->span = spanFromToken(open);
        pattern->kind = PickPatternKind::Tuple;
        if (check(TokenKind::RParen)) {
            failAtCurrent("A tuple pick pattern must contain at least one element.");
        }

        bool sawComma = false;
        do {
            pattern->elements.push_back(parsePickPattern());
            if (!match(TokenKind::Comma)) {
                break;
            }
            sawComma = true;
        } while (!check(TokenKind::RParen) && !isAtEnd());

        consume(TokenKind::RParen, "Expected ')' after tuple pick pattern");
        if (pattern->elements.size() == 1 && !sawComma) {
            failAt(open, "A one-element tuple pick pattern must use a trailing comma, for example `(value,)`.");
        }
        return pattern;
    }

    if (!matchNameOrKeyword()) {
        failAtCurrent("Expected a binding, '_' or nested tuple pattern.");
    }
    const Token token = previous();
    auto pattern = std::make_unique<PickPattern>();
    pattern->span = spanFromToken(token);
    pattern->kind = token.text == "_" ? PickPatternKind::Wildcard : PickPatternKind::Binding;
    pattern->name = token.text;
    return pattern;
}

std::unique_ptr<IfExpr> Parser::parseIfExpression() {
    auto expr = std::make_unique<IfExpr>();
    expr->span = spanFromToken(previous());
    expr->condition = parseExpression();
    expr->thenBlock = parseBlock();

    if (match(TokenKind::KwElse)) {
        if (match(TokenKind::KwIf)) {
            auto nested = parseIfExpression();
            auto block = std::make_unique<BlockStmt>();
            block->span = nested->span;
            auto tail = std::make_unique<ExprStmt>();
            tail->span = nested->span;
            tail->expr = std::move(nested);
            block->statements.push_back(std::move(tail));
            expr->elseBlock = std::move(block);
        } else {
            expr->elseBlock = parseBlock();
        }
    }

    return expr;
}

std::unique_ptr<PickExpr> Parser::parsePickExpression() {
    auto expr = std::make_unique<PickExpr>();
    expr->span = spanFromToken(previous());
    expr->value = parseExpression();

    consume(TokenKind::LBrace, "Expected '{' to start pick block");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        PickBranch branch;
        if (check(TokenKind::LParen)) {
            branch.tuplePattern = parsePickPattern();
            branch.span = branch.tuplePattern->span;
            collectPickPatternBindings(branch.tuplePattern.get(), branch.bindings);
        } else {
            if (!matchNameOrKeyword()) {
                failAtCurrent("Expected variant tag, '_' or tuple pattern in pick branch");
            }
            branch.span = spanFromToken(previous());
            branch.tag = previous().text;

            if (match(TokenKind::LParen)) {
                if (!check(TokenKind::RParen)) {
                    do {
                        consumeNameToken("Expected binding name");
                        branch.bindings.push_back(previous().text);
                    } while (match(TokenKind::Comma));
                }
                consume(TokenKind::RParen, "Expected ')' after pattern bindings");
            }
        }

        branch.body = parseBlock();
        expr->branches.push_back(std::move(branch));
    }
    consume(TokenKind::RBrace, "Expected '}' to end pick block");
    return expr;
}


std::unique_ptr<Expr> Parser::parseExpression(int minBindingPower) {
    auto left = parseUnary();
    bool sawComparison = false;
    bool sawRange = false;

    const auto bindingPower = [](TokenKind kind) -> int {
        switch (kind) {
        case TokenKind::KwOr: return 10;
        case TokenKind::KwAnd: return 20;
        case TokenKind::EqEq:
        case TokenKind::NotEq:
        case TokenKind::Less:
        case TokenKind::LessEq:
        case TokenKind::Greater:
        case TokenKind::GreaterEq:
        case TokenKind::KwIn: return 30;
        case TokenKind::Plus:
        case TokenKind::Minus: return 40;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent: return 50;
        case TokenKind::DotDot:
        case TokenKind::DotDotEqual: return 5;
        default: return -1;
    }
    };
    const auto isComparison = [](TokenKind kind) {
        return kind == TokenKind::EqEq || kind == TokenKind::NotEq ||
            kind == TokenKind::Less || kind == TokenKind::LessEq ||
            kind == TokenKind::Greater || kind == TokenKind::GreaterEq ||
            kind == TokenKind::KwIn;
    };
    const auto isRange = [](TokenKind kind) {
        return kind == TokenKind::DotDot || kind == TokenKind::DotDotEqual;
    };

    while (true) {
        const TokenKind kind = peek().kind;
        const int leftBindingPower = bindingPower(kind);
        if (leftBindingPower < minBindingPower) {
            break;
        }
        if (isComparison(kind) && sawComparison) {
            failAtCurrent("Comparison operators are non-associative; combine comparisons with `and`.");
        }
        if (isRange(kind) && sawRange) {
            failAtCurrent("Range operators `..` and `..=` are non-associative; add parentheses.");
        }

        const Token opToken = advance();
        auto expr = std::make_unique<BinaryExpr>();
        expr->span = left->span;
        expr->op = opToken.text;
        expr->left = std::move(left);
        if (kind == TokenKind::KwIn) {
            expr->right = parseExpression(leftBindingPower + 1);
            if (check(TokenKind::DotDot) || check(TokenKind::DotDotEqual)) {
                if (sawRange) {
                    failAtCurrent("Range operators `..` and `..=` are non-associative; add parentheses.");
                }
                const Token rangeToken = advance();
                auto rangeExpr = std::make_unique<BinaryExpr>();
                rangeExpr->span = expr->right->span;
                rangeExpr->op = rangeToken.text;
                rangeExpr->left = std::move(expr->right);
                rangeExpr->right = parseExpression(leftBindingPower + 1);
                expr->right = std::move(rangeExpr);
                sawRange = true;
            }
        } else {
            expr->right = parseExpression(leftBindingPower + 1);
        }
        left = std::move(expr);
        sawComparison = sawComparison || isComparison(kind);
        sawRange = sawRange || isRange(kind);
    }

    return left;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (match(TokenKind::KwRef)) {
        auto borrow = std::make_unique<BorrowExpr>();
        borrow->span = spanFromToken(previous());
        if (match(TokenKind::KwMut)) {
            borrow->isMutable = true;
        }
        if (match(TokenKind::LBracket)) {
            if (borrow->isMutable) {
                failAt(previous(), "`ref mut[s]` is not supported. Scope-bound borrows currently use `ref[s]`.");
            }
            consumeNameToken("Expected scope name inside ref[...]");
            borrow->scopeName = previous().text;
            consume(TokenKind::RBracket, "Expected ']' after scope name");
        }
        borrow->target = parseUnary();
        return borrow;
    }

    if (match(TokenKind::Minus) || match(TokenKind::KwNot)) {
        auto unary = std::make_unique<UnaryExpr>();
        unary->span = spanFromToken(previous());
        unary->op = previous().text;
        unary->operand = parseUnary();
        return unary;
    }

    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();
    while (true) {
        if (auto* ident = dynamic_cast<IdentExpr*>(expr.get())) {
            const bool startsShapeLiteralBody =
                checkAhead(0, TokenKind::LBrace) &&
                (checkAhead(1, TokenKind::RBrace) ||
                 (pos + 2 < tokens.size() && isNameToken(tokens[pos + 1].kind) && checkAhead(2, TokenKind::Colon)));

            if (check(TokenKind::LBracket) && checkAhead(1, TokenKind::Identifier) &&
                checkAhead(2, TokenKind::RBracket) && checkAhead(3, TokenKind::LBrace) &&
                (checkAhead(4, TokenKind::RBrace) ||
                 (pos + 5 < tokens.size() && isNameToken(tokens[pos + 4].kind) && checkAhead(5, TokenKind::Colon)))) {
                consume(TokenKind::LBracket, "Expected '[' after shape name.");
                consumeNameToken("Expected named scope after '[' in shape literal.");
                const std::string scopeName = previous().text;
                consume(TokenKind::RBracket, "Expected ']' after named scope in shape literal.");
                expr = parseShapeInitExpr(ident->name, scopeName, ident->span);
                continue;
            }

            if (startsShapeLiteralBody) {
                expr = parseShapeInitExpr(ident->name, {}, ident->span);
                continue;
            }
        }

        if (match(TokenKind::LParen)) {
            auto call = std::make_unique<CallExpr>();
            call->span = expr->span;
            call->callee = std::move(expr);
            if (!check(TokenKind::RParen)) {
                do {
                    call->args.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after arguments");
            expr = std::move(call);
            continue;
        }

        if (match(TokenKind::Dot)) {
            auto member = std::make_unique<MemberExpr>();
            member->span = expr->span;
            member->object = std::move(expr);
            if (match(TokenKind::IntLiteral)) {
                member->member = previous().text;
            } else {
                consumeNameToken("Expected member name or numeric tuple projection after '.'");
                member->member = previous().text;
            }
            expr = std::move(member);
            continue;
        }

        if (match(TokenKind::LBracket)) {
            auto index = std::make_unique<IndexExpr>();
            index->span = expr->span;
            index->object = std::move(expr);
            index->index = parseExpression();
            consume(TokenKind::RBracket, "Expected ']' after index expression");
            expr = std::move(index);
            continue;
        }

        break;
    }
    return expr;
}

std::unique_ptr<ShapeInitExpr> Parser::parseShapeInitExpr(std::string name, std::string scopeName, SourceSpan span) {
    auto expr = std::make_unique<ShapeInitExpr>();
    expr->span = span;
    expr->name = std::move(name);
    expr->scopeName = std::move(scopeName);

    consume(TokenKind::LBrace, "Expected '{' to start shape literal.");
    while (!check(TokenKind::RBrace) && !isAtEnd()) {
        ShapeInitField field;
        consumeNameToken("Expected field name in shape literal.");
        field.span = spanFromToken(previous());
        field.name = previous().text;
        consume(TokenKind::Colon, "Expected ':' after shape literal field name.");
        field.value = parseExpression();
        expr->fields.push_back(std::move(field));
        match(TokenKind::Comma);
    }
    consume(TokenKind::RBrace, "Expected '}' to end shape literal.");
    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (check(TokenKind::Error)) {
        failAtCurrent("Unexpected token in source.");
    }

    if (match(TokenKind::KwIf)) {
        return parseIfExpression();
    }
    if (match(TokenKind::KwPick)) {
        return parsePickExpression();
    }

    if (match(TokenKind::KwTrue)) {
        auto expr = std::make_unique<BoolExpr>();
        expr->span = spanFromToken(previous());
        expr->value = true;
        return expr;
    }
    if (match(TokenKind::KwFalse)) {
        auto expr = std::make_unique<BoolExpr>();
        expr->span = spanFromToken(previous());
        expr->value = false;
        return expr;
    }
    if (match(TokenKind::IntLiteral)) {
        auto expr = std::make_unique<IntExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::FloatLiteral)) {
        auto expr = std::make_unique<FloatExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::Identifier) || match(TokenKind::KwSelf) || match(TokenKind::KwView)) {
        auto expr = std::make_unique<IdentExpr>();
        expr->span = spanFromToken(previous());
        expr->name = previous().text;
        return expr;
    }
    if (match(TokenKind::StringLiteral)) {
        auto expr = std::make_unique<StringExpr>();
        expr->span = spanFromToken(previous());
        expr->value = previous().text;
        return expr;
    }
    if (match(TokenKind::LParen)) {
        const Token openToken = previous();
        if (match(TokenKind::RParen)) {
            auto tuple = std::make_unique<TupleExpr>();
            tuple->span = spanFromToken(openToken);
            return tuple;
        }

        auto first = parseExpression();
        if (!match(TokenKind::Comma)) {
            consume(TokenKind::RParen, "Expected ')' after grouped expression");
            return first;
        }

        auto tuple = std::make_unique<TupleExpr>();
        tuple->span = spanFromToken(openToken);
        tuple->elements.push_back(std::move(first));
        while (!check(TokenKind::RParen) && !isAtEnd()) {
            tuple->elements.push_back(parseExpression());
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        consume(TokenKind::RParen, "Expected ')' after tuple literal");
        return tuple;
    }

    failAtCurrent("Expected expression.");
}

} // namespace claw::frontend
