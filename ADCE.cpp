#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/PostDominators.h"

#include <iostream>
#include <set>
#include <llvm/IR/InstIterator.h>

using namespace llvm;

namespace {
    //===-------------------------------------------------------------------===//
    // ADCE Class
    //
    // This class does all of the work of Aggressive Dead Code Elimination.
    //
    class ADCE : public FunctionPass {
    private:
        Function *Func;                      // Function we are working on
        std::vector<Instruction*> WorkList;  // Instructions that just became live
        std::set<Instruction*>    LiveSet;   // Set of live instructions

        //===-----------------------------------------------------------------===//
        // The public interface for this class
        //
    public:
        static char ID; // Pass identification
        ADCE() : FunctionPass(ID) {}

        // Execute the Aggressive Dead Code Elimination algorithm on one function
        //
        virtual bool runOnFunction(Function &F) {
            Func = &F;
            bool Changed = doADCE();
            assert(WorkList.empty());
            LiveSet.clear();
            return Changed;
        }

        // getAnalysisUsage
        //
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.setPreservesCFG();
        }

    private:
        // doADCE() - Run the Aggressive Dead Code Elimination algorithm, returning
        // true if the function was modified.
        //
        bool doADCE();

        // Determine if instruction is trivially live
        bool isTriviallyLive(Instruction *Inst) const {
            return Inst->mayWriteToMemory() || Inst->mayHaveSideEffects() ||
                    isa<TerminatorInst>(Inst);
        }

        // Mark live
        void markLive(Instruction *Inst) {
            if (LiveSet.count(Inst)) return;
            LiveSet.insert(Inst);
            WorkList.push_back(Inst);
        }
    };
}  // End of anonymous namespace

char ADCE::ID = 0;
static RegisterPass<ADCE> X("xwei12-adce", "Aggressive Dead Code Elimination (MP4)",
                            false /* Only looks at CFG? */,
                            false /* Analysis Pass? */);

// Implement ADCE algorithm here
bool ADCE::doADCE()
{
    // Initial pass to mark trivially live and trivially dead instructions
    // Perform this pass in depth-first order on the CFG so that we never
    // visit blocks that are unreachable: those are trivially dead.
    std::set<BasicBlock *> RBBs;
    for (auto *BB : depth_first_ext(Func, RBBs)) {
        for (auto &Inst : *BB) {
            if (isTriviallyLive(&Inst))
                markLive(&Inst);
            else if (Inst.use_empty())
                Inst.removeFromParent();
        }
    }

    // Find new live instructions
    while (!WorkList.empty()) {
        auto *Inst = WorkList.back();
        WorkList.pop_back();
        if (RBBs.count(Inst->getParent()))
            for (auto &Op : Inst->operands())
                if (auto OpInst = dyn_cast<Instruction>(Op))
                    markLive(OpInst);
    }

    // Delete all instructions not in LiveSet
    WorkList.clear();
    for (auto &BB : *Func) {
        if (RBBs.count(&BB)) {
            for (auto &Inst : BB) {
                if (!LiveSet.count(&Inst)) {
                    // Record instructions to erase
                    WorkList.push_back(&Inst);
                    Inst.dropAllReferences();
                }
            }
        }
    }
    for (auto *Inst : WorkList)
        if (!LiveSet.count(Inst))
            Inst->eraseFromParent();

    bool Changed = !WorkList.empty();
    WorkList.clear();
    return Changed;
}
