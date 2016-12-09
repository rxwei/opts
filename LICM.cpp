#include "llvm/IR/Function.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Scalar.h"
#include <llvm/IR/InstIterator.h>
#include <llvm/Analysis/ValueTracking.h>
#include <iostream>
#include <unordered_set>
#include <numeric>

using namespace llvm;

namespace {
    class LICM : public LoopPass {
    private:
        Loop *curLoop;

    public:
        static char ID; // Pass identification, replacement for typeid
        LICM() : LoopPass(ID) {}
        virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override {
            curLoop = L;
            return doLICM();
        }

        /// This transformation requires natural loop information & requires that
        /// loop preheaders be inserted into the CFG...
        //
        // !!!PLEASE READ getLoopAnalysisUsage(AU) defined in lib/Transforms/Utils/LoopUtils.cpp
        void getAnalysisUsage(AnalysisUsage &AU) const override {
            AU.setPreservesCFG();
            getLoopAnalysisUsage(AU);
        }

    private:
        bool doLICM();

        // Determine if instruction is safe to hoist
        bool safeToHoist(Instruction &Inst) const {
            auto Cond1 = isSafeToSpeculativelyExecute(&Inst);
            SmallVector<BasicBlock *, 16> ExitBlocks;
            curLoop->getExitBlocks(ExitBlocks);
            auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
            auto InstBlock = Inst.getParent();
            auto Cond2 = std::accumulate(ExitBlocks.begin(), ExitBlocks.end(), true,
                                         [&](bool Acc, BasicBlock *BB) {
                                             return Acc && DT.dominates(InstBlock, BB);
                                         });
            return Cond1 || Cond2;
        }

        // Determine if loop is invariant
        bool isLoopInvariant(Instruction &Inst) const {
            // Condition 1: One of the invariant instruction classes
            auto Cond1 = Inst.isBinaryOp() || Inst.isShift() || Inst.isCast() ||
                    isa<SelectInst>(Inst) || isa<GetElementPtrInst>(Inst);
            // Condition 2: Every operand is either a constant or an instruction computed outside the loop
            auto Cond2 = std::accumulate(Inst.operands().begin(), Inst.operands().end(), true,
                                         [&](bool Acc, Use &Op) {
                                             auto IsConst = isa<Constant>(Op);
                                             auto OpInst = dyn_cast<Instruction>(Op);
                                             auto IsComputedOutside = OpInst && !curLoop->contains(OpInst);
                                             return Acc && (IsConst || IsComputedOutside);
                                         });
            return Cond1 && Cond2;
        }
    };
}

char LICM::ID = 0;
RegisterPass<LICM> X("xwei12-licm", "Loop Invariant Code Motion (MP4)",
                     false /* Only looks at CFG? */,
                     false /* Analysis Pass? */);

// Implement LICM algorithm here
bool LICM::doLICM()
{
    auto *PH = curLoop->getLoopPreheader();
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto RootNode = DT[curLoop->getHeader()];
    auto &Children = RootNode->getChildren();
    auto Changed = false;
    for (auto *Child : Children) {
        auto *BB = Child->getBlock();
        // Check if BB is outside the current loop or in sub loop
        if (!curLoop->contains(BB) || LI.getLoopFor(BB) != curLoop)
            continue;
        // Hoist instructions that are loop-invariant and safe to hoist
        for (auto BBI = BB->begin(); BBI != BB->end(); BBI++) {
            auto &Inst = *BBI;
            if (isLoopInvariant(Inst) && safeToHoist(Inst)) {
                Changed = true;
                BBI++;
                Inst.moveBefore(PH->getTerminator()); // Hoist
            }
        }
    }
    return Changed;
}
