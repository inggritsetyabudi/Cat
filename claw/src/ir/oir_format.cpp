#include "ir/oir.h"

#include <sstream>
#include <string>
#include <variant>

namespace claw::frontend {
namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

std::string indentText(int indent) {
    return std::string(static_cast<size_t>(indent) * 2, ' ');
}
std::string externalCallSuffix(const std::optional<OirExternalCallInfo>& info) {
    if (!info.has_value()) {
        return {};
    }

    std::ostringstream out;
    out << " [extern abi=" << (info->abi.empty() ? std::string("unknown") : info->abi)
        << " dep=" << (info->dependencyRoot.empty() ? std::string("<external>") : info->dependencyRoot)
        << " symbol=" << (info->linkageName.empty() ? std::string("<callee>") : info->linkageName)
        << (info->rawOnly ? " raw" : " safe");
    if (info->opaqueResult) {
        out << " opaque-result";
    }
    out << ']';
    return out.str();
}
std::string formatSymbolLinkInfo(const SymbolLinkInfo& info) {
    std::ostringstream out;
    out << " [abi=" << (info.abi.empty() ? std::string("unknown") : info.abi)
        << " link=" << describeLinkageKind(info.linkage)
        << " symbol=" << (info.symbol.empty() ? std::string("<unknown>") : info.symbol)
        << " ffi=" << (info.ffiStable ? "true" : "false") << ']';
    return out.str();
}

std::string formatLayoutInfo(const TypeLayoutInfo& layout) {
    std::ostringstream out;
    out << "layout repr=" << layout.repr
        << " kind=" << describeTypeLayoutKind(layout.kind)
        << " size=" << layout.sizeBytes
        << " align=" << layout.alignBytes
        << " pass=" << describeAbiPassKind(layout.passKind)
        << " ffi=" << (layout.ffiStable ? "true" : "false");
    if (layout.kind == TypeLayoutKind::Tagged && !layout.isTemplate) {
        out << " tag=" << layout.tagSizeBytes
            << " payload_offset=" << layout.payloadOffsetBytes
            << " payload_size=" << layout.payloadSizeBytes;
    }
    if (layout.isTemplate) {
        out << " unresolved=true";
    }
    return out.str();
}

std::string formatParam(const OirParam& param) {
    return param.name + ": " + param.type + " [" + describeAbiPassKind(param.passKind) + "]";
}

std::string formatTypeParams(const std::vector<std::string>& params) {
    if (params.empty()) {
        return {};
    }

    std::ostringstream out;
    out << " of ";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    return out.str();
}

const LayoutFieldInfo* findLayoutField(const TypeLayoutInfo& layout, std::string_view fieldName) {
    for (const auto& field : layout.fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

const LayoutVariantInfo* findLayoutVariant(const TypeLayoutInfo& layout, std::string_view variantName) {
    for (const auto& variant : layout.variants) {
        if (variant.name == variantName) {
            return &variant;
        }
    }
    return nullptr;
}
std::string formatValue(const OirValue& value) {
    return value.text;
}

std::string formatInst(const OirInst& inst, int indent) {
    const std::string pad = indentText(indent);
    return std::visit(Overloaded{
        [&](const OirHoldInst& value) {
            std::ostringstream out;
            out << pad << (value.isMutable ? "var " : "val ") << value.name << ": " << value.type;
            if (value.init.has_value()) {
                out << " = " << formatValue(*value.init);
            }
            out << "\n";
            return out.str();
        },
        [&](const OirStoreInst& value) {
            std::ostringstream out;
            out << pad << "store " << value.target << " <- " << formatValue(value.value)
                << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirStoreFieldInst& value) {
            std::ostringstream out;
            out << pad << "store_field " << formatValue(value.object) << "." << value.field << " <- "
                << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirReturnInst& value) {
            std::ostringstream out;
            out << pad << "return " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirDiscardInst& value) {
            std::ostringstream out;
            out << pad << "discard " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirDropInst& value) {
            std::ostringstream out;
            out << pad << "drop " << value.name << " : " << value.type << "\n";
            return out.str();
        },
        [&](const OirBranchInst& value) {
            std::ostringstream out;
            out << pad << "branch " << formatValue(value.condition) << " -> " << value.trueLabel << ", "
                << value.falseLabel << "\n";
            return out.str();
        },
        [&](const OirGotoInst& value) {
            return pad + "goto " + value.targetLabel + "\n";
        },
        [&](const OirScanInst& value) {
            std::ostringstream out;
            out << pad << "scan " << value.itemName << ": " << value.itemType << " over "
                << formatValue(value.iterable) << " -> " << value.bodyLabel << ", " << value.exitLabel << "\n";
            return out.str();
        },
        [&](const OirPickInst& value) {
            std::ostringstream out;
            out << pad << "pick " << formatValue(value.value) << " : " << value.value.type << "\n";
            for (const auto& item : value.cases) {
                out << pad << "case " << item.tag;
                if (!item.bindings.empty()) {
                    out << "(";
                    for (size_t i = 0; i < item.bindings.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.bindings[i];
                    }
                    out << ")";
                }
                out << " -> " << item.targetLabel << "\n";
            }
            return out.str();
        },
        [&](const OirLiftInst& value) {
            std::ostringstream out;
            out << pad << "try " << formatValue(value.value) << " -> " << value.okName;
            if (value.autoPropagate) {
                out << " propagate\n";
            } else {
                out << ", else " << value.failName << " -> " << value.failLabel << "\n";
            }
            return out.str();
        },
        [&](const OirChoiceMakeInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.variantName << "(";
            for (size_t i = 0; i < value.payloads.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.payloads[i]);
            }
            out << ") : " << value.type << "\n";
            return out.str();
        },
        [&](const OirStopInst& value) {
            if (value.targetLabel.empty()) {
                return pad + "stop\n";
            }
            return pad + "stop -> " + value.targetLabel + "\n";
        },
        [&](const OirSkipInst& value) {
            if (value.targetLabel.empty()) {
                return pad + "skip\n";
            }
            return pad + "skip -> " + value.targetLabel + "\n";
        },
        [&](const OirCallInst& value) {
            std::ostringstream out;
            if (value.result.has_value()) {
                out << pad << *value.result << " = ";
            } else {
                out << pad;
            }
            out << "call " << value.callee << "(";
            for (size_t i = 0; i < value.args.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.args[i]);
            }
            out << ") : " << value.type << externalCallSuffix(value.externalInfo) << "\n";
            return out.str();
        },
        [&](const OirFieldInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = field " << formatValue(value.object) << "." << value.field
                << " : " << value.type << "\n";
            return out.str();
        },
        [&](const OirBinaryInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.op << " " << formatValue(value.left) << ", "
                << formatValue(value.right) << " : " << value.type << "\n";
            return out.str();
        }
    }, inst);
}

std::string formatDecl(const OirDecl& decl) {
    return std::visit(Overloaded{
        [&](const OirFunction& fn) {
            std::ostringstream out;
            out << "oir.fn " << fn.name << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatParam(fn.params[i]);
            }
            out << ") -> " << fn.returnType << " [" << describeAbiPassKind(fn.returnPassKind) << "]"
                << formatSymbolLinkInfo(fn.linkage) << "\n";
            for (const auto& block : fn.blocks) {
                out << "  block " << block.label;
                if (block.rawRegion.has_value()) {
                    out << " [raw: " << *block.rawRegion << "]";
                }
                out << ":\n";
                for (const auto& inst : block.insts) {
                    out << formatInst(inst, 2);
                }
            }
            return out.str();
        },
        [&](const OirShape& shape) {
            std::ostringstream out;
            out << "oir." << (shape.isViewShape ? "view_shape " : "shape ")
                << shape.name;
            if (shape.isViewShape && !shape.scopeParamName.empty()) {
                out << "[" << shape.scopeParamName << "]";
            }
            out << formatTypeParams(shape.typeParams)
                << formatSymbolLinkInfo(shape.linkage) << "\n";
            if (shape.layout.has_value()) {
                out << "  " << formatLayoutInfo(*shape.layout) << "\n";
            }
            for (const auto& field : shape.fields) {
                out << "  field " << field.name << ": " << field.type;
                if (shape.layout.has_value() && !shape.layout->isTemplate) {
                    if (const auto* layoutField = findLayoutField(*shape.layout, field.name)) {
                        out << " @offset=" << layoutField->offsetBytes
                            << " size=" << layoutField->sizeBytes
                            << " align=" << layoutField->alignBytes;
                    }
                }
                out << "\n";
            }
            return out.str();
        },
        [&](const OirChoice& choice) {
            std::ostringstream out;
            out << "oir.choice " << choice.name << formatTypeParams(choice.typeParams)
                << formatSymbolLinkInfo(choice.linkage) << "\n";
            if (choice.layout.has_value()) {
                out << "  " << formatLayoutInfo(*choice.layout) << "\n";
            }
            for (const auto& item : choice.cases) {
                out << "  case " << item.name;
                if (!item.payloadTypes.empty()) {
                    out << "(";
                    for (size_t i = 0; i < item.payloadTypes.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.payloadTypes[i];
                    }
                    out << ")";
                }
                if (choice.layout.has_value() && !choice.layout->isTemplate) {
                    if (const auto* variant = findLayoutVariant(*choice.layout, item.name)) {
                        out << " @payload_offset=" << variant->payloadOffsetBytes
                            << " size=" << variant->payloadSizeBytes
                            << " align=" << variant->payloadAlignBytes;
                    }
                }
                out << "\n";
            }
            return out.str();
        },
        [&](const OirStatic& stat) {
            std::ostringstream out;
            out << "oir.static " << stat.name << ": " << stat.type << " = " << stat.value << "\n";
            return out.str();
        }
    }, decl);
}

} // namespace

std::string formatOirRealm(const OirRealm& realm) {
    std::ostringstream out;
    out << "oir.module " << (realm.name.empty() ? std::string("<unknown>") : realm.name) << "\n";
    for (const auto& decl : realm.decls) {
        out << formatDecl(decl);
    }
    return out.str();
}

std::string formatOirProgram(const OirProgram& program) {
    std::ostringstream out;
    out << "oir.program entry " << (program.entryRealm.empty() ? std::string("<unknown>") : program.entryRealm)
        << " symbol " << (program.entrySymbol.empty() ? std::string("<unknown>::main") : program.entrySymbol) << "\n";
    for (size_t i = 0; i < program.realms.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << formatOirRealm(program.realms[i]);
    }
    return out.str();
}

std::string emitOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return formatOirProgram(buildOirProgram(entryRealm, units));
}

} // namespace claw::frontend
