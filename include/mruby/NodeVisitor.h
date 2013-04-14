#pragma once
struct ScopeNode;
struct PostExeNode;
struct SelfNode;
struct WordsNode;
struct SymbolsNode;
struct NilNode;
struct TrueNode;
struct FalseNode;
struct RetryNode;
struct RedoNode;
struct EnsureNode;
struct NegateNode;
struct StrNode;
struct XstrNode;
struct RegxNode;
struct DregxNode;
struct BeginNode;
struct LambdaNode;
struct BlockNode;
struct ModuleNode;
struct ClassNode;
struct SclassNode;
struct CaseNode;
struct RescueNode;
struct ForNode;
struct SdefNode;
struct DefNode;
struct OpAsgnNode;
struct IfNode;
struct HeredocNode;
struct DsymNode;
struct DstrNode;
struct AsgnNode;
struct SymNode;
struct Colon2Node;
struct AndNode;
struct OrNode;
struct Dot3Node;
struct Dot2Node;
struct YieldNode;
struct UntilNode;
struct WhileNode;
struct AliasNode;
struct SplatNode;
struct MAsgnNode;
struct UndefNode;
struct HashNode;
struct SuperNode;
struct Colon3Node;
struct ReturnNode;
struct NextNode;
struct ArrayNode;
struct NthRefNode;
struct BackRefNode;
struct ZsuperNode;
struct IntLiteralNode;
struct FloatLiteralNode;
struct ConstNode;
struct IVarNode;
struct CVarNode;
struct GVarNode;
struct LVarNode;
struct CallNode;
struct FCallNode;
struct BlockArgNode;
struct BreakNode;
// no codegen use
struct LiteralDelimNode;
struct ArgNode;
struct DxstrNode;
struct NodeVisitor {
    virtual int  visit(ScopeNode *n)=0;
    virtual void visit(PostExeNode *n)=0;
    virtual void visit(SelfNode *sn)=0;
    virtual void visit(WordsNode *n)=0;
    virtual void visit(SymbolsNode *n)=0;
    virtual void visit(NilNode *n)=0;
    virtual void visit(TrueNode *n)=0;
    virtual void visit(FalseNode *n)=0;
    virtual void visit(RetryNode *nn)=0;
    virtual void visit(RedoNode *nn)=0;
    virtual void visit(EnsureNode *nn)=0;
    virtual void visit(NegateNode *nn)=0;
    virtual void visit(StrNode *sn)=0;
    virtual void visit(XstrNode *xn)=0;
    virtual void visit(RegxNode *xn)=0;
    virtual void visit(DregxNode *dn)=0;
    virtual void visit(BeginNode *n)=0;
    virtual void visit(LambdaNode *node)=0;
    virtual void visit(BlockNode *node)=0;
    virtual void visit(ModuleNode *nd)=0;
    virtual void visit(ClassNode * nd)=0;
    virtual void visit(SclassNode * nd)=0;
    virtual void visit(CaseNode * n)=0;
    virtual void visit(RescueNode * node)=0;
    virtual void visit(ForNode * n)=0;
    virtual void visit(SdefNode * n)=0;
    virtual void visit(DefNode * n)=0;
    virtual void visit(OpAsgnNode * node)=0;
    virtual void visit(IfNode * node)=0;
    virtual void visit(HeredocNode *node)=0;
    virtual void visit(DsymNode *node)=0;
    virtual void visit(DstrNode *node)=0;
    virtual void visit(AsgnNode *node)=0;
    virtual void visit(SymNode *node)=0;
    virtual void visit(Colon2Node *node)=0;
    virtual void visit(AndNode *node)=0;
    virtual void visit(OrNode *node)=0;
    virtual void visit(Dot3Node *node)=0;
    virtual void visit(Dot2Node *node)=0;
    virtual void visit(YieldNode *node)=0;
    virtual void visit(UntilNode *n)=0;
    virtual void visit(WhileNode *n)=0;
    virtual void visit(AliasNode *n)=0;
    virtual void visit(SplatNode *node)=0;
    virtual void visit(MAsgnNode *node)=0;
    virtual void visit(UndefNode * n)=0;
    virtual void visit(HashNode * node)=0;
    virtual void visit(SuperNode * node)=0;
    virtual void visit(Colon3Node * node)=0;
    virtual void visit(CallNode * node)=0;
    virtual void visit(FCallNode * node)=0;
    virtual void visit(ReturnNode * node)=0;
    virtual void visit(NextNode * node)=0;
    virtual void visit(ArrayNode * node)=0;
    virtual void visit(NthRefNode * n)=0;
    virtual void visit(BackRefNode * n)=0;
    virtual void visit(ZsuperNode * n)=0;
    virtual void visit(IntLiteralNode * n)=0;
    virtual void visit(FloatLiteralNode * n)=0;
    virtual void visit(ConstNode * n)=0;
    virtual void visit(IVarNode * n)=0;
    virtual void visit(CVarNode * n)=0;
    virtual void visit(GVarNode * n)=0;
    virtual void visit(LVarNode * n)=0;
    virtual void visit(BlockArgNode * n)=0;
    virtual void visit(BreakNode * n)=0;
    virtual void visit(LiteralDelimNode * n)=0;
    virtual void visit(ArgNode * n)=0;
    virtual void visit(DxstrNode * n)=0;

};
struct NodeVisitor_Null : public NodeVisitor {
    virtual int  visit(ScopeNode *n) {return 0;}
    virtual void visit(PostExeNode *n) {}
    virtual void visit(SelfNode *sn) {}
    virtual void visit(WordsNode *n) {}
    virtual void visit(SymbolsNode *n) {}
    virtual void visit(NilNode *n) {}
    virtual void visit(TrueNode *n) {}
    virtual void visit(FalseNode *n) {}
    virtual void visit(RetryNode *nn) {}
    virtual void visit(RedoNode *nn) {}
    virtual void visit(EnsureNode *nn) {}
    virtual void visit(NegateNode *nn) {}
    virtual void visit(StrNode *sn) {}
    virtual void visit(XstrNode *xn) {}
    virtual void visit(RegxNode *xn) {}
    virtual void visit(DregxNode *dn) {}
    virtual void visit(BeginNode *n) {}
    virtual void visit(LambdaNode *node) {}
    virtual void visit(BlockNode *node) {}
    virtual void visit(ModuleNode *nd) {}
    virtual void visit(ClassNode * nd) {}
    virtual void visit(SclassNode * nd) {}
    virtual void visit(CaseNode * n) {}
    virtual void visit(RescueNode * node) {}
    virtual void visit(ForNode * n) {}
    virtual void visit(SdefNode * n) {}
    virtual void visit(DefNode * n) {}
    virtual void visit(OpAsgnNode * node) {}
    virtual void visit(IfNode * node) {}
    virtual void visit(HeredocNode *node) {}
    virtual void visit(DsymNode *node) {}
    virtual void visit(DstrNode *node) {}
    virtual void visit(AsgnNode *node) {}
    virtual void visit(SymNode *node) {}
    virtual void visit(Colon2Node *node) {}
    virtual void visit(AndNode *node) {}
    virtual void visit(OrNode *node) {}
    virtual void visit(Dot3Node *node) {}
    virtual void visit(Dot2Node *node) {}
    virtual void visit(YieldNode *node) {}
    virtual void visit(UntilNode *n) {}
    virtual void visit(WhileNode *n) {}
    virtual void visit(AliasNode *n) {}
    virtual void visit(SplatNode *node) {}
    virtual void visit(MAsgnNode *node) {}
    virtual void visit(UndefNode * n) {}
    virtual void visit(HashNode * node) {}
    virtual void visit(SuperNode * node) {}
    virtual void visit(Colon3Node * node) {}
    virtual void visit(CallNode * node) {}
    virtual void visit(FCallNode * node) {}
    virtual void visit(ReturnNode * node) {}
    virtual void visit(NextNode * node) {}
    virtual void visit(ArrayNode * node) {}
    virtual void visit(NthRefNode * n) {}
    virtual void visit(BackRefNode * n) {}
    virtual void visit(ZsuperNode * n) {}
    virtual void visit(IntLiteralNode * n) {}
    virtual void visit(FloatLiteralNode * n) {}
    virtual void visit(ConstNode * n) {}
    virtual void visit(IVarNode * n) {}
    virtual void visit(CVarNode * n) {}
    virtual void visit(GVarNode * n) {}
    virtual void visit(LVarNode * n) {}
    virtual void visit(BlockArgNode * n) {}
    virtual void visit(BreakNode * n) {}
    virtual void visit(LiteralDelimNode * n) {}
    virtual void visit(ArgNode * n) {}
    virtual void visit(DxstrNode * n) {}
};
