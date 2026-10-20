#include "adapter.h"
#include "pair_admission.h"
#include "pair_layout.h"
#include <llvm/IR/LLVMContext.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace claw::frontend;
using claw::experimental::llvm_cpp::ObjectOptions;
using claw::experimental::llvm_cpp::emitObject;
using claw::experimental::llvm_cpp::validatePairAdmission;
namespace {
using Path = std::filesystem::path;
using Mutation = std::function<void(LirProgram&)>;
void need(bool ok, const std::string& s) { if (!ok) throw std::runtime_error("pair admission: " + s); }
LirValue v(std::string text, std::string type, std::string key = {}, bool unit = false) {
    LirValue x{std::move(text), std::move(type)}; x.bindingKey = std::move(key); x.isUnit = unit; return x;
}
LirValue k(std::string text, std::string type, std::string key) { return v(std::move(text), std::move(type), std::move(key)); }
LirFunction& fn(LirProgram& p, const std::string& n) {
    for (auto& d : p.realms.front().decls) if (auto* f = std::get_if<LirFunction>(&d); f && f->name == n) return *f;
    throw std::runtime_error("missing function " + n);
}
LirObjectInst obj(const std::string& n = "%t0", const std::string& key = "slot:t0") {
    return LirObjectInst{false, n, "Tuple[Int32, Int32]", "stack", key};
}
LirFieldInst field(const std::string& r, const std::string& o, const std::string& key, const std::string& i = "0") {
    return LirFieldInst{r, k(o, "Tuple[Int32, Int32]", key), i, "Int32"};
}
LirProgram valid() {
    LirFunction make; make.name="make"; make.returnType="Tuple[Int32, Int32]"; make.returnPassKind=AbiPassKind::Direct;
    make.linkage.symbol="pair::make"; make.params={{"x","Int32",AbiPassKind::Scalar,"param:x"},{"y","Int32",AbiPassKind::Scalar,"param:y"}};
    make.blocks.push_back({"entry",{}, {}, {obj(),
        LirStoreFieldInst{k("%t0","Tuple[Int32, Int32]","slot:t0"),"0",k("x","Int32","param:x")},
        LirStoreFieldInst{k("%t0","Tuple[Int32, Int32]","slot:t0"),"1",k("y","Int32","param:y")},
        LirReturnInst{k("%t0","Tuple[Int32, Int32]","slot:t0")}}});
    LirFunction take; take.name="take"; take.returnType="Int32"; take.returnPassKind=AbiPassKind::Scalar; take.linkage.symbol="pair::take";
    take.params.push_back({"p","Tuple[Int32, Int32]",AbiPassKind::Direct,"param:p"});
    take.blocks.push_back({"entry",{}, {}, {field("%a","p","param:p","0"), field("%b","p","param:p","1"), LirReturnInst{k("%a","Int32","%a")}}});
    LirFunction main; main.name="main"; main.returnType="Int32"; main.returnPassKind=AbiPassKind::Scalar; main.linkage.symbol="pair::main";
    main.blocks.push_back({"entry",{}, {}, {obj("p","slot:p"), LirCallInst{"%made","make",{v("40","Int32"),v("2","Int32")},"Tuple[Int32, Int32]",{},LirCallKind::Direct}, LirStoreInst{"p",k("%made","Tuple[Int32, Int32]","%made"),"slot:p"}, field("%a","p","slot:p","0"), LirCallInst{"%taken","take",{k("p","Tuple[Int32, Int32]","slot:p")},"Int32",{},LirCallKind::Direct}, LirReturnInst{k("%taken","Int32","%taken")}}});
    for (LirFunction* f : {&make,&take,&main}) { f->linkage.abi="claw"; f->linkage.linkage=LinkageKind::Internal; f->linkage.ffiStable=false; }
    return LirProgram{"pair", "pair::main", {{"pair", {make,take,main}}}};
}
ObjectOptions out(const Path& d, bool pair, bool arithmetic=false) { ObjectOptions o; o.objectPath=(d/"out.o").string(); o.irPath=(d/"out.ll").string(); o.enablePairI32=pair; o.enableArithmeticI32=arithmetic; return o; }
void rm(const ObjectOptions& o) { std::error_code e; std::filesystem::remove(o.objectPath,e); std::filesystem::remove(o.irPath,e); }
void sentinels(const ObjectOptions& o) { std::ofstream a(o.objectPath,std::ios::binary), b(o.irPath,std::ios::binary); a<<"PAIR OBJECT SENTINEL\n"; b<<"PAIR IR SENTINEL\n"; }
std::string readBytes(const std::string& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
void noTemps(const Path& root, const std::string& name) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        need(entry.path().filename().string().find(".tmp") == std::string::npos,
             "temporary artifact leaked by " + name + ": " + entry.path().string());
}
void reject(const Path& root,const std::string& name,const LirProgram& p,const ObjectOptions& o,std::ofstream& log) {
    std::filesystem::create_directories(Path(o.objectPath).parent_path()); rm(o); std::string e; bool ok=emitObject(p,o,e);
    need(!ok && e.find("unsupported")!=std::string::npos, "accepted " + name + ": " + e);
    need(!std::filesystem::exists(o.objectPath)&&!std::filesystem::exists(o.irPath), "published " + name);
    noTemps(root, name);
    sentinels(o); std::string aa=readBytes(o.objectPath), bb=readBytes(o.irPath);
    ok=emitObject(p,o,e); need(!ok && readBytes(o.objectPath)==aa && readBytes(o.irPath)==bb,"clobbered sentinel "+name);
    noTemps(root, name);
    log<<name<<"\t"<<e<<"\n";
}
}
int main(int argc,char** argv) {
 try { need(argc==2,"expected artifact directory"); Path root=argv[1]; std::filesystem::create_directories(root); std::ofstream log(root/"admission.log");
   llvm::LLVMContext context;
   auto* i32=llvm::Type::getInt32Ty(context);
   auto* pair=llvm::StructType::get(context,{i32,i32},false);
   const std::string good="e-p:64:64-p270:32:32-p271:32:32-p272:64:64-i32:32-a:0:64";
   claw::experimental::llvm_cpp::detail::validatePairLayout(llvm::DataLayout(good),pair);
   for(const std::string badLayout:{
       "E-p:64:64-p270:32:32-p271:32:32-p272:64:64-i32:32-a:0:64",
       "e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i32:32-a:0:64",
       "e-p:64:64-p270:64:64-p271:32:32-p272:64:64-i32:32-a:0:64",
       "e-p:64:64-p270:32:32-p271:64:64-p272:64:64-i32:32-a:0:64",
       "e-p:64:64-p270:32:32-p271:32:32-p272:32:32-i32:32-a:0:64",
       "e-p:64:64-p270:32:32-p271:32:32-p272:64:64-i32:32-a:0:32",
       "e-p:64:64-p270:32:32-p271:32:32-p272:64:64-i32:64-a:0:64",
       "e-p:64:64-p270:32:32-p271:32:32-p272:64:64-i32:32-a:64:64"}) {
       auto layoutOut=out(root/"bad-layout",true);std::filesystem::create_directories(Path(layoutOut.objectPath).parent_path());
       for(bool sentinel:{false,true}) {
           rm(layoutOut);if(sentinel)sentinels(layoutOut);
           bool rejected=false;try{claw::experimental::llvm_cpp::detail::validatePairLayout(llvm::DataLayout(badLayout),pair);}catch(const std::runtime_error&){rejected=true;}
           need(rejected,"bad layout accepted");
           need(sentinel?(readBytes(layoutOut.objectPath)=="PAIR OBJECT SENTINEL\n"&&readBytes(layoutOut.irPath)=="PAIR IR SENTINEL\n"):(!std::filesystem::exists(layoutOut.objectPath)&&!std::filesystem::exists(layoutOut.irPath)),"layout guard altered outputs");
           noTemps(root,"layout guard");
       }
   }
   auto p=valid(); validatePairAdmission(p,false); auto o=out(root/"positive",true); std::filesystem::create_directories(Path(o.objectPath).parent_path()); rm(o); std::string e; need(emitObject(p,o,e),"positive: "+e); noTemps(root,"positive");
   std::ifstream ir(o.irPath); std::string text((std::istreambuf_iterator<char>(ir)),{}); for (auto m:{"{ i32, i32 }","pair::make","pair::take","i32 0, i32 0","i32 0, i32 1"}) need(text.find(m)!=std::string::npos,std::string("missing IR marker ")+m);
   auto noarith=valid(); validatePairAdmission(noarith,false);
   auto add=valid(); auto& t=fn(add,"take"); t.blocks[0].insts.pop_back(); t.blocks[0].insts.push_back(LirBinaryInst{"%sum","add",k("%a","Int32","%a"),k("%b","Int32","%b"),"Int32"}); t.blocks[0].insts.push_back(LirReturnInst{k("%sum","Int32","%sum")}); validatePairAdmission(add,true);
   auto arithmeticPositive=out(root/"positive-arithmetic",true,true); std::filesystem::create_directories(Path(arithmeticPositive.objectPath).parent_path()); need(emitObject(add,arithmeticPositive,e),"positive arithmetic: "+e); noTemps(root,"positive-arithmetic");
   std::vector<std::pair<std::string,Mutation>> bad;
   bad.push_back({"wrong-arity",[](auto&p){fn(p,"make").params.pop_back();}});
   bad.push_back({"wrong-type",[](auto&p){fn(p,"make").params[0].type="Bool";}});
   bad.push_back({"wrong-index",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).field="2";}});
   bad.push_back({"wrong-field-type",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.type="Bool";}});
   bad.push_back({"wrong-return-abi",[](auto&p){fn(p,"make").returnPassKind=AbiPassKind::Scalar;}});
   bad.push_back({"wrong-param-abi",[](auto&p){fn(p,"take").params[0].passKind=AbiPassKind::Scalar;}});
   bad.push_back({"symbol",[](auto&p){fn(p,"make").linkage.symbol="other::make";}});
   bad.push_back({"call-arity",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).args.pop_back();}});
   bad.push_back({"call-return",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).type="Int32";}});
   bad.push_back({"unknown-target",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).callee="missing";}});
   bad.push_back({"store-target-absent",[](auto&p){auto& s=std::get<LirStoreInst>(fn(p,"main").blocks[0].insts[2]); s.target="p"; s.targetBindingKey="missing-slot";}});
   bad.push_back({"duplicate-slot",[](auto&p){fn(p,"make").blocks[0].insts.insert(fn(p,"make").blocks[0].insts.begin()+2,obj("%x","slot:t0"));}});
   bad.push_back({"uninit-read",[](auto&p){fn(p,"take").blocks[0].insts[0]=field("%x","missing","missing");}});
   bad.push_back({"partial-transfer",[](auto&p){fn(p,"make").blocks[0].insts.erase(fn(p,"make").blocks[0].insts.begin()+2);}});
   bad.push_back({"duplicate-write",[](auto&p){auto&i=fn(p,"make").blocks[0].insts; i.insert(i.begin()+2,i[1]);}});
   bad.push_back({"parameter-store",[](auto&p){fn(p,"take").blocks[0].insts.insert(fn(p,"take").blocks[0].insts.begin(),LirStoreInst{"p",v("1","Int32"),"param:p"});}});
   bad.push_back({"rvalue-store",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).object=v("tmp","Tuple[Int32, Int32]");}});
   bad.push_back({"unknown-storage",[](auto&p){std::get<LirObjectInst>(fn(p,"make").blocks[0].insts[0]).storage="heap";}});
   bad.push_back({"branch",[](auto&p){fn(p,"main").blocks[0].insts.back()=LirBranchInst{v("true","Bool"),"x","y"};}});
   bad.push_back({"phi",[](auto&p){fn(p,"main").blocks[0].insts.insert(fn(p,"main").blocks[0].insts.begin(),LirPhiInst{"%p","Int32",{}});}});
   bad.push_back({"drop-provenance",[](auto&p){fn(p,"make").blocks[0].insts.insert(fn(p,"make").blocks[0].insts.begin()+1,LirDropInst{"x","Int32","param:x",{}});}});
   bad.push_back({"unknown-instruction",[](auto&p){fn(p,"make").blocks[0].insts[0]=LirDefectInst{"mystery",""};}});
   bad.push_back({"malformed-entry",[](auto&p){p.entrySymbol="wrong::entry";}});
   bad.push_back({"pair-layout-shape",[](auto&p){std::get<LirObjectInst>(fn(p,"make").blocks[0].insts[0]).type="Tuple[Int32]";}});
   bad.push_back({"malformed-linkage",[](auto&p){fn(p,"make").linkage.abi="foreign";}});
   bad.push_back({"provenance-nonempty",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.provenance=Provenance::forRoot("param:x",1);}});
   bad.push_back({"provenance-invalid-id",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.provenance.syntheticStorageId=0;}});
   bad.push_back({"provenance-invalid-source",[](auto&p){std::get<LirStoreFieldInst>(fn(p,"make").blocks[0].insts[1]).value.provenance.source=SourceProjection{};}});
   bad.push_back({"call-kind",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).kind=LirCallKind::External;}});
   bad.push_back({"call-safety",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).safety=LirSafetyTag::ProvenSafe;}});
   bad.push_back({"call-external-metadata",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).externalInfo=LirExternalCallInfo{};}});
   bad.push_back({"call-return-param",[](auto&p){std::get<LirCallInst>(fn(p,"main").blocks[0].insts[1]).result="slot:p";}});
   for(auto& [n,m]:bad){auto q=valid();m(q);reject(root,n,q,out(root/n,true),log);}
   auto addRejected=add; reject(root,"pair-alone-rejects-add",addRejected,out(root/"pair-alone-rejects-add",true),log);
   auto disabled=valid(); reject(root,"default-pair-rejected",disabled,out(root/"default-pair-rejected",false),log);
   auto arithmetic=valid(); reject(root,"arithmetic-alone-pair-rejected",arithmetic,out(root/"arithmetic-alone-pair-rejected",false,true),log);
   auto singleton=valid(); auto singletonOptions=out(root/"singleton-pair-rejected",false); singletonOptions.enableSingletonI32=true; reject(root,"singleton-pair-rejected",singleton,singletonOptions,log);
   std::cout<<"PASS pair admission: direct constructor/projections, arithmetic gate, malformed matrix, sentinels\n"; return 0;
 } catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;} }
