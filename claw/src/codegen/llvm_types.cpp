#include "codegen/llvm_internal.h"

namespace claw::codegen {

void LlvmEmitter::collectDecls() {
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            std::visit(Overloaded{
                [&](const LirFunction& fn) {
                    functionsByName[fn.name] = &fn;
                },
                [&](const LirShape& shape) {
                    shapesByName[shape.name] = &shape;
                    const std::string canonicalName = canonicalBackendTypeBase(shape.name);
                    if (canonicalName != shape.name) {
                        shapesByName[canonicalName] = &shape;
                    }
                    std::unordered_map<std::string, size_t> fields;
                    for (size_t i = 0; i < shape.fields.size(); ++i) {
                        fields[shape.fields[i].name] = i;
                    }
                    shapeFieldIndices[shape.name] = fields;
                    if (canonicalName != shape.name) {
                        shapeFieldIndices[canonicalName] = fields;
                    }
                },
                [&](const LirChoice& choice) {
                    choicesByName[choice.name] = &choice;
                    const std::string canonicalName = canonicalBackendTypeBase(choice.name);
                    if (canonicalName != choice.name) {
                        choicesByName[canonicalName] = &choice;
                    }
                },
                [&](const LirStatic& stat) {
                    staticsByName[stat.name] = &stat;
                }
            }, decl);
        }
    }
}

const LirFunction* LlvmEmitter::entryFunction() const {
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<LirFunction>(&decl)) {
                if (fn->linkage.symbol == program.entrySymbol) {
                    return fn;
                }
            }
        }
    }
    return nullptr;
}

std::string LlvmEmitter::llvmType(const std::string& typeText) const {
    const std::string stripped = stripViewPrefix(typeText);
    const std::string base = canonicalBackendTypeBase(stripGenericArgs(stripped));

    if (stripped == "Unit" || base == "Unit") return "void";
    if (stripped == "Bool" || base == "Bool") return "i1";
    if (stripped == "integer literal" || base == "integer literal") return "i32";
    if (stripped == "float literal" || base == "float literal") return "double";
    if (base == "Int8" || base == "UInt8") return "i8";
    if (base == "Int16" || base == "UInt16") return "i16";
    if (base == "Char" || base == "Int32" || base == "UInt32") return "i32";
    if (base == "Int64" || base == "UInt64" || base == "USize") return "i64";
    if (base == "Int128" || base == "UInt128") return "i128";
    if (base == "Float32") return "float";
    if (base == "Float64") return "double";
    if (base == "Str" || base == "Span") return "%claw.slice";
    if (base == "Vec") return "%claw.buffer";
    if (base == "Addr" || base == "RawPtr" || base == "RawMut" || base == "Fn" || base == "CStr" ||
        base == "OwnedCStr" || base == "Arena" || base == "Anchor" || base == "Map" ||
        base == "Set" || base == "Queue") {
        return "ptr";
    }

    const auto shapeIt = shapesByName.find(base);
    if (shapeIt != shapesByName.end()) {
        const LirShape& shape = *shapeIt->second;
        if (shape.typeParams.empty() && shape.layout.has_value() && !shape.layout->isTemplate) {
            return quoteType(shape.linkage.symbol);
        }
        if (!shape.typeParams.empty()) {
            const ParsedTypeName parsed = parseTypeName(stripped);
            if (parsed.args.size() != shape.typeParams.size()) {
                throw std::runtime_error(
                    "LLVM lowering expected " + std::to_string(shape.typeParams.size()) +
                    " type argument(s) for shape '" + base + "', got " + std::to_string(parsed.args.size()) + ".");
            }
            std::unordered_map<std::string, std::string> bindings;
            for (size_t i = 0; i < shape.typeParams.size(); ++i) {
                bindings[shape.typeParams[i]] = parsed.args[i];
            }
            std::ostringstream concreteType;
            concreteType << "{ ";
            for (size_t i = 0; i < shape.fields.size(); ++i) {
                if (i > 0) {
                    concreteType << ", ";
                }
                concreteType << llvmType(substituteTypeText(shape.fields[i].type, bindings));
            }
            concreteType << " }";
            return concreteType.str();
        }
    }
    if (const auto choice = resolveChoiceType(typeText); choice.has_value()) {
        return choice->llvmTypeText;
    }

    throw std::runtime_error("LLVM lowering does not yet support type '" + typeText + "'.");
}

std::string LlvmEmitter::emitTypeDecls() const {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* shape = std::get_if<LirShape>(&decl)) {
                if (!shape->layout.has_value() || shape->layout->isTemplate || !shape->typeParams.empty()) {
                    continue;
                }
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << quoteType(shape->linkage.symbol) << " = type { ";
                for (size_t i = 0; i < shape->fields.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << llvmType(shape->fields[i].type);
                }
                out << " }\n";
            } else if (const auto* choice = std::get_if<LirChoice>(&decl)) {
                if (!choice->layout.has_value() || choice->layout->isTemplate || !choice->typeParams.empty()) {
                    continue;
                }
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << quoteType(choice->linkage.symbol) << " = type { i32";
                const size_t paddingBytes = choice->layout->payloadOffsetBytes > choice->layout->tagSizeBytes
                    ? choice->layout->payloadOffsetBytes - choice->layout->tagSizeBytes
                    : 0;
                if (paddingBytes > 0) {
                    out << ", [" << paddingBytes << " x i8]";
                }
                if (choice->layout->payloadSizeBytes > 0) {
                    out << ", [" << choice->layout->payloadSizeBytes << " x i8]";
                }
                out << " }\n";
            }
        }
    }
    return out.str();
}

std::string LlvmEmitter::emitStaticGlobals() {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* stat = std::get_if<LirStatic>(&decl)) {
                if (!first) {
                    out << "\n";
                }
                first = false;
                
                std::string llvmVal;
                if (stat->type == "Int32") {
                    llvmVal = stat->value;
                } else if (stat->type == "Bool") {
                    llvmVal = stat->value == "true" ? "1" : "0";
                } else if (stat->type == "Str") {
                    const auto& info = internString(stat->value);
                    llvmVal = "{ ptr " + info.globalName + ", i64 " + std::to_string(info.length) + " }";
                } else {
                    llvmVal = "0"; // Fallback for unknown literal types
                }
                
                out << quoteGlobal(stat->link.symbol) << " = " << llvmFunctionLinkage(stat->link) 
                    << "constant " << llvmType(stat->type) << " " << llvmVal;
            }
        }
    }
    return out.str();
}

const StringConstantInfo& LlvmEmitter::internString(std::string_view literal) {
    const std::string key(literal);
    const auto it = stringPool.find(key);
    if (it != stringPool.end()) {
        return it->second;
    }

    StringConstantInfo info;
    info.globalName = "@.str." + std::to_string(stringPool.size());
    const std::string decoded = decodeStringLiteral(literal);
    info.length = decoded.size();
    info.arrayType = "[" + std::to_string(info.length + 1) + " x i8]";
    stringPool.emplace(key, info);
    return stringPool.find(key)->second;
}

std::string LlvmEmitter::emitStringGlobals() {
    std::ostringstream out;
    bool first = true;
    const auto collectLiteral = [&](const LirValue& value) {
        if (isStringLiteral(value.text)) {
            internString(value.text);
        }
    };
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<LirFunction>(&decl)) {
                for (const auto& block : fn->blocks) {
                    for (const auto& inst : block.insts) {
                        std::visit(Overloaded{
                            [&](const LirPhiInst& value) {
                                for (const auto& in : value.inputs) {
                                    collectLiteral(in.value);
                                }
                            },
                            [&](const LirStoreInst& value) {
                                collectLiteral(value.value);
                            },
                            [&](const LirStoreFieldInst& value) {
                                collectLiteral(value.object);
                                collectLiteral(value.value);
                            },
                            [&](const LirReturnInst& value) {
                                collectLiteral(value.value);
                            },
                            [&](const LirDiscardInst& value) {
                                collectLiteral(value.value);
                            },
                            [&](const LirBoundsCheckInst& value) {
                                for (const auto& arg : value.args) {
                                    collectLiteral(arg);
                                }
                            },
                            [&](const LirCallInst& value) {
                                for (const auto& arg : value.args) {
                                    collectLiteral(arg);
                                }
                            },
                            [&](const LirIterNextInst& value) {
                                collectLiteral(value.iterable);
                            },
                            [&](const LirBranchInst& value) {
                                collectLiteral(value.condition);
                            },
                            [&](const LirSwitchInst& value) {
                                collectLiteral(value.value);
                            },
                            [&](const LirLiftInst& value) {
                                collectLiteral(value.value);
                            },
                            [&](const LirChoiceMakeInst& value) {
                                for (const auto& payload : value.payloads) {
                                    collectLiteral(payload);
                                }
                            },
                            [&](const LirFieldInst& value) {
                                collectLiteral(value.object);
                            },
                            [&](const LirBinaryInst& value) {
                                collectLiteral(value.left);
                                collectLiteral(value.right);
                            },
                            [&](const auto&) {}
                        }, inst);
                    }
                }
            }
        }
    }

    for (const auto& [literal, info] : stringPool) {
        const std::string decoded = decodeStringLiteral(literal);
        if (!first) {
            out << "\n";
        }
        first = false;
        out << info.globalName << " = private unnamed_addr constant " << info.arrayType
            << " c\"" << escapeLlvmBytes(decoded) << "\\00\"\n";
    }
    return out.str();
}

void LlvmEmitter::addRuntimeDecl(const std::string& decl) {
    if (runtimeDeclKeys.insert(decl).second) {
        runtimeDecls.push_back(decl);
    }
}

void LlvmEmitter::addExternalDecl(const std::string& decl) {
    if (externalDeclKeys.insert(decl).second) {
        externalDecls.push_back(decl);
    }
}

bool LlvmEmitter::usesRuntimePointerAbi(const std::string& argType) const {
    const std::string llvmArgType = llvmType(argType);
    return llvmArgType == "%claw.slice" || llvmArgType == "%claw.buffer" || llvmArgType == "i128";
}

std::string LlvmEmitter::llvmRuntimeParamType(const std::string& argType) const {
    return usesRuntimePointerAbi(argType) ? std::string("ptr") : llvmType(argType);
}

std::string LlvmEmitter::runtimeSymbol(std::string_view callee, const std::string& argType) {
    const std::string llvmArgType = llvmType(argType);
    std::string suffix;
    if (llvmArgType == "%claw.slice") {
        suffix = "slice";
    } else if (llvmArgType == "%claw.buffer") {
        suffix = "buffer";
    } else if (llvmArgType == "ptr") {
        suffix = "ptr";
    } else {
        suffix = llvmArgType;
    }
    std::replace(suffix.begin(), suffix.end(), '%', '_');
    std::replace(suffix.begin(), suffix.end(), '"', '_');
    std::replace(suffix.begin(), suffix.end(), '.', '_');
    const std::string symbol = quoteGlobal(std::string("claw.runtime.") + std::string(callee) + "." + suffix);
    addRuntimeDecl("declare void " + symbol + "(" + llvmRuntimeParamType(argType) + ")");
    return symbol;
}

std::string LlvmEmitter::externalSymbol(const LirCallInst& call) {
    if (!call.externalInfo.has_value()) {
        throw std::runtime_error("LLVM lowering requires explicit external metadata for external calls.");
    }
    const auto& info = *call.externalInfo;
    if (info.rawOnly || info.opaqueResult) {
        throw std::runtime_error("LLVM lowering does not yet support raw-only or opaque external calls: '" + call.callee + "'.");
    }

    std::vector<std::string> paramTypes;
    paramTypes.reserve(call.args.size());
    for (const auto& arg : call.args) {
        paramTypes.push_back(llvmType(arg.type));
    }

    std::ostringstream decl;
    decl << "declare " << llvmType(call.type) << " " << quoteGlobal(info.linkageName) << "(";
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        if (i > 0) {
            decl << ", ";
        }
        decl << paramTypes[i];
    }
    decl << ")";
    addExternalDecl(decl.str());
    return quoteGlobal(info.linkageName);
}

std::string LlvmEmitter::emitDeclarations() {
    std::ostringstream out;
    bool first = true;
    for (const auto& decl : runtimeDecls) {
        if (!first) {
            out << "\n";
        }
        first = false;
        out << decl;
    }
    for (const auto& decl : externalDecls) {
        if (!first) {
            out << "\n";
        }
        first = false;
        out << decl;
    }
    if (needsTrap) {
        if (!first) {
            out << "\n";
        }
        out << "declare void @llvm.trap()";
    }
    return out.str();
}

std::optional<AbiLayout> LlvmEmitter::abiLayoutForType(std::string_view typeText) const {
    const std::string stripped = trim(stripViewPrefix(typeText));
    const ParsedTypeName parsed = parseTypeName(stripped);
    const std::string& base = parsed.base;

    if (base == "Unit") return AbiLayout{0, 1};
    if (base == "Bool" || base == "Int8" || base == "UInt8") return AbiLayout{1, 1};
    if (base == "Int16" || base == "UInt16") return AbiLayout{2, 2};
    if (base == "Char" || base == "Int32" || base == "UInt32" || base == "Float32") return AbiLayout{4, 4};
    if (base == "Int64" || base == "UInt64" || base == "USize" || base == "Float64") {
        return AbiLayout{8, 8};
    }
    if (base == "Int128" || base == "UInt128") return AbiLayout{16, 16};
    if (base == "Str" || base == "Span") return AbiLayout{16, 8};
    if (base == "Vec") return AbiLayout{24, 8};
    if (base == "Addr" || base == "RawPtr" || base == "RawMut" || base == "Fn" || base == "CStr" ||
        base == "OwnedCStr" || base == "Arena" || base == "Anchor" || base == "Map" ||
        base == "Set" || base == "Queue") {
        return AbiLayout{8, 8};
    }

    if (shapesByName.contains(base)) {
        if (const TypeLayoutInfo* layout = resolveShapeLayout(typeText)) {
            return AbiLayout{layout->sizeBytes, layout->alignBytes};
        }
    }

    if (const auto choice = resolveChoiceType(typeText); choice.has_value()) {
        return AbiLayout{choice->sizeBytes, choice->alignBytes};
    }

    return std::nullopt;
}

const TypeLayoutInfo* LlvmEmitter::resolveShapeLayout(std::string_view typeText) const {
    const ParsedTypeName parsed = parseTypeName(typeText);
    const auto shapeIt = shapesByName.find(parsed.base);
    if (shapeIt == shapesByName.end()) {
        return nullptr;
    }

    const LirShape& shape = *shapeIt->second;
    if (shape.typeParams.empty()) {
        return shape.layout.has_value() && !shape.layout->isTemplate ? &*shape.layout : nullptr;
    }
    if (parsed.args.size() != shape.typeParams.size()) {
        return nullptr;
    }

    const std::string key = parsed.base + "[" + joinTypeArgs(parsed.args) + "]";
    if (const auto found = concreteShapeLayouts.find(key); found != concreteShapeLayouts.end()) {
        return &found->second;
    }
    if (!concreteShapeLayoutsInProgress.insert(key).second) {
        return nullptr;
    }

    std::unordered_map<std::string, std::string> bindings;
    for (size_t i = 0; i < shape.typeParams.size(); ++i) {
        bindings[shape.typeParams[i]] = parsed.args[i];
    }

    TypeLayoutInfo layout;
    layout.typeName = std::string(typeText);
    layout.repr = "claw-internal";
    layout.abi = "claw";
    layout.kind = TypeLayoutKind::Aggregate;
    layout.ffiStable = false;
    layout.alignBytes = 1;
    for (const auto& field : shape.fields) {
        const std::string fieldType = substituteTypeText(field.type, bindings);
        const auto fieldLayout = abiLayoutForType(fieldType);
        if (!fieldLayout.has_value()) {
            concreteShapeLayoutsInProgress.erase(key);
            return nullptr;
        }
        const size_t offset = roundUpTo(layout.sizeBytes, fieldLayout->align);
        layout.fields.push_back(LayoutFieldInfo{
            field.name,
            ResolvedType{},
            offset,
            fieldLayout->size,
            fieldLayout->align,
        });
        layout.sizeBytes = offset + fieldLayout->size;
        layout.alignBytes = std::max(layout.alignBytes, fieldLayout->align);
    }
    layout.sizeBytes = roundUpTo(layout.sizeBytes, layout.alignBytes);

    concreteShapeLayoutsInProgress.erase(key);
    const auto [stored, _] = concreteShapeLayouts.emplace(key, std::move(layout));
    return &stored->second;
}

std::optional<ConcreteChoiceInfo> LlvmEmitter::resolveChoiceType(std::string_view typeText) const {
    const ParsedTypeName parsed = parseTypeName(typeText);
    const auto it = choicesByName.find(parsed.base);
    if (it == choicesByName.end()) {
        return std::nullopt;
    }

    const auto* choice = it->second;
    if (!choice->layout.has_value()) {
        return std::nullopt;
    }

    ConcreteChoiceInfo info;
    info.typeText = trim(stripViewPrefix(typeText));
    info.tagSizeBytes = choice->layout->tagSizeBytes == 0 ? 4 : choice->layout->tagSizeBytes;

    if (choice->typeParams.empty()) {
        if (choice->layout->isTemplate) {
            return std::nullopt;
        }
        info.llvmTypeText = quoteType(choice->linkage.symbol);
        info.payloadOffsetBytes = choice->layout->payloadOffsetBytes;
        info.payloadSizeBytes = choice->layout->payloadSizeBytes;
        info.sizeBytes = choice->layout->sizeBytes;
        info.alignBytes = choice->layout->alignBytes;
        info.cases.reserve(choice->cases.size());
        for (size_t i = 0; i < choice->cases.size(); ++i) {
            ConcreteChoiceCaseInfo caseInfo;
            caseInfo.name = choice->cases[i].name;
            caseInfo.payloadTypes = choice->cases[i].payloadTypes;
            if (i < choice->layout->variants.size()) {
                caseInfo.layout = choice->layout->variants[i];
            }
            info.cases.push_back(std::move(caseInfo));
        }
        return info;
    }

    if (parsed.args.size() != choice->typeParams.size()) {
        throw std::runtime_error(
            "LLVM lowering expected " + std::to_string(choice->typeParams.size()) +
            " type argument(s) for choice '" + parsed.base + "', got " + std::to_string(parsed.args.size()) + ".");
    }

    std::unordered_map<std::string, std::string> bindings;
    for (size_t i = 0; i < choice->typeParams.size(); ++i) {
        bindings[choice->typeParams[i]] = parsed.args[i];
    }

    size_t maxPayloadSize = 0;
    size_t maxPayloadAlign = 1;
    info.cases.reserve(choice->cases.size());
    for (const auto& item : choice->cases) {
        ConcreteChoiceCaseInfo caseInfo;
        caseInfo.name = item.name;
        size_t nextOffset = 0;
        size_t variantAlign = 1;
        for (size_t payloadIndex = 0; payloadIndex < item.payloadTypes.size(); ++payloadIndex) {
            const std::string payloadType = substituteTypeText(item.payloadTypes[payloadIndex], bindings);
            const auto layout = abiLayoutForType(payloadType);
            if (!layout.has_value()) {
                throw std::runtime_error(
                    "LLVM lowering does not yet support payload type '" + payloadType +
                    "' in concrete choice '" + info.typeText + "'.");
            }
            nextOffset = roundUpTo(nextOffset, layout->align);
            caseInfo.payloadTypes.push_back(payloadType);
            caseInfo.layout.payloadFields.push_back(LayoutFieldInfo{
                "payload" + std::to_string(payloadIndex),
                ResolvedType{},
                nextOffset,
                layout->size,
                layout->align,
            });
            nextOffset += layout->size;
            variantAlign = std::max(variantAlign, layout->align);
        }
        caseInfo.layout.name = item.name;
        caseInfo.layout.payloadSizeBytes = roundUpTo(nextOffset, variantAlign);
        caseInfo.layout.payloadAlignBytes = variantAlign;
        maxPayloadSize = std::max(maxPayloadSize, caseInfo.layout.payloadSizeBytes);
        maxPayloadAlign = std::max(maxPayloadAlign, caseInfo.layout.payloadAlignBytes);
        info.cases.push_back(std::move(caseInfo));
    }

    info.alignBytes = std::max(info.tagSizeBytes, maxPayloadAlign);
    info.payloadOffsetBytes = roundUpTo(info.tagSizeBytes, maxPayloadAlign);
    info.payloadSizeBytes = maxPayloadSize;
    info.sizeBytes = roundUpTo(info.payloadOffsetBytes + info.payloadSizeBytes, info.alignBytes);
    info.llvmTypeText = choiceStorageType(info.tagSizeBytes, info.payloadOffsetBytes, info.payloadSizeBytes);
    for (auto& item : info.cases) {
        item.layout.payloadOffsetBytes = info.payloadOffsetBytes;
    }
    return info;
}

bool LlvmEmitter::usesIndirectAbi(std::string_view, AbiPassKind passKind) const {
    return passKind == AbiPassKind::Indirect;
}

bool LlvmEmitter::usesBorrowPointerAbi(std::string_view typeText, AbiPassKind passKind) const {
    if (passKind != AbiPassKind::Borrow) {
        return false;
    }
    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(typeText)));
    return shapesByName.contains(baseType) || choicesByName.contains(baseType);
}

bool LlvmEmitter::usesPointerAbi(std::string_view typeText, AbiPassKind passKind) const {
    return usesIndirectAbi(typeText, passKind) || usesBorrowPointerAbi(typeText, passKind);
}

std::string LlvmEmitter::llvmParamAbiType(const LirParam& param) const {
    return usesPointerAbi(param.type, param.passKind) ? std::string("ptr") : llvmType(param.type);
}

std::string LlvmEmitter::llvmFunctionReturnType(const LirFunction& fn) const {
    return usesIndirectAbi(fn.returnType, fn.returnPassKind) ? std::string("void") : llvmType(fn.returnType);
}

const LirFunction* LlvmEmitter::lookupDirectFunction(std::string_view callee) const {
    const auto directIt = functionsByName.find(std::string(callee));
    if (directIt != functionsByName.end()) {
        return directIt->second;
    }

    const std::string tail = tailSegment(callee);
    const auto tailIt = functionsByName.find(tail);
    if (tailIt != functionsByName.end()) {
        return tailIt->second;
    }
    return nullptr;
}

} // namespace claw::codegen






