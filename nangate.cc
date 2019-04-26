#include "kernel/yosys.h"
#include "kernel/celltypes.h"
#include "erase_b2f_pm.h"
#include "dff_nan_pm.h"
#include "share_nan_pm.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct NandToNaNWorker
{
    int nand_count = 0, not_count = 0, b2f_count = 0, f2b_count = 0;
    RTLIL::Design *design;
    RTLIL::Module *module;

    NandToNaNWorker(RTLIL::Design *design, RTLIL::Module *module) :
        design(design), module(module)
    {
        for (auto cell : module->selected_cells()) {
            if (cell->type == "$_NAND_") {
                b2f_count += 2;
                nand_count += 1;
                f2b_count += 1;
                RTLIL::Cell *b2fA = module->addCell(NEW_ID, "\\bit_to_fp3");
                RTLIL::Cell *b2fB = module->addCell(NEW_ID, "\\bit_to_fp3");
                RTLIL::Cell *nan  = module->addCell(NEW_ID, "\\nan");
                RTLIL::Cell *f2b  = module->addCell(NEW_ID, "\\fp3_to_bit");
                b2fA->setPort("\\A", cell->getPort("\\A"));
                b2fA->setPort("\\Y", module->addWire(NEW_ID, 3));
                b2fB->setPort("\\A", cell->getPort("\\B"));
                b2fB->setPort("\\Y", module->addWire(NEW_ID, 3));
                f2b->setPort("\\A", module->addWire(NEW_ID, 3));
                f2b->setPort("\\Y", cell->getPort("\\Y"));
                nan->setPort("\\A", b2fA->getPort("\\Y"));
                nan->setPort("\\B", b2fB->getPort("\\Y"));
                nan->setPort("\\Y", f2b->getPort("\\A"));
                module->swap_names(cell, nan);
                module->remove(cell);
            } else if (cell->type == "$_NOT_") {
                b2f_count += 1;
                not_count += 1;
                f2b_count += 1;
                RTLIL::Cell *b2f = module->addCell(NEW_ID, "\\bit_to_fp3");
                RTLIL::Cell *nan = module->addCell(NEW_ID, "\\nan");
                RTLIL::Cell *f2b = module->addCell(NEW_ID, "\\fp3_to_bit");
                b2f->setPort("\\A", cell->getPort("\\A"));
                b2f->setPort("\\Y", module->addWire(NEW_ID, 3));
                f2b->setPort("\\A", module->addWire(NEW_ID, 3));
                f2b->setPort("\\Y", cell->getPort("\\Y"));
                nan->setPort("\\A", b2f->getPort("\\Y"));
                nan->setPort("\\B", b2f->getPort("\\Y"));
                nan->setPort("\\Y", f2b->getPort("\\A"));
                module->swap_names(cell, nan);
                module->remove(cell);
            }
        }
    }
};

struct NandToNaNPass : public Pass {
    NandToNaNPass() : Pass("nand_to_nan") {}
    void execute(vector<string> args, Design *design) override {
        log_header(design, "Executing NAND_TO_NaN pass (implementing tom7 logic)\n");
        log_push();
        Pass::call(design, "read_verilog -lib -sv gateware.sv");
        log_pop();

        (void) args;
        for (auto module : design->selected_modules()) {
            log("Replacing NAND with NaN in module %s...\n", log_id(module));
            NandToNaNWorker worker(design, module);
            log("Replaced %d NAND gates and %d NOT gates.\n",
                worker.nand_count, worker.not_count);
            log("Inserted:\n        nan: %5d\n bit_to_fp3: %5d\n fp3_to_bit: %5d\n",
                worker.nand_count + worker.not_count,
                worker.b2f_count, worker.f2b_count);
        }
    }
} NandToNaNPass;

struct DffToFp3Pass : public Pass {
    DffToFp3Pass() : Pass("dff_nan") {}
    void execute(vector<string> args, Design *design) override {
        log_header(design, "Executing DFF_NaN pass (widening flipflops to hold floats)\n");
        (void) args;
        for (auto module : design->selected_modules()) {
            log("  Module %s\n", log_id(module));
            dff_nan_pm pm(module, module->selected_cells());
            pool<RTLIL::Cell*> dffs;
            pm.run([&]() { dffs.insert(pm.st.dff); });
            for (auto &dff : dffs) {
                RTLIL::Cell *f2b = module->addCell(NEW_ID, "\\fp3_to_bit");
                f2b->setPort("\\A", module->addWire(NEW_ID, 3));
                f2b->setPort("\\Y", dff->getPort("\\Q"));
                RTLIL::Cell *b2f = module->addCell(NEW_ID, "\\bit_to_fp3");
                b2f->setPort("\\A", dff->getPort("\\D"));
                b2f->setPort("\\Y", module->addWire(NEW_ID, 3));
                for (int i = 0; i < 3; i++) {
                    // @TODO: Support more DFF types
                    assert(dff->type == "$_DFF_P_");
                    RTLIL::Cell *new_ff = module->addCell(NEW_ID, "$_DFF_P_");
                    new_ff->setPort("\\C", dff->getPort("\\C"));
                    new_ff->setPort("\\D", b2f->getPort("\\Y")[i]);
                    new_ff->setPort("\\Q", f2b->getPort("\\A")[i]);
                }
                module->remove(dff);
            }
            log("Converted %d flip-flops to hold floats\n", GetSize(dffs));
        }
    }
} DffToFp3Pass;

struct EraseFpBitPass : public Pass {
    EraseFpBitPass() : Pass("simplify_nan") {}
    void execute(vector<string> args, Design *design) override {
        log_header(design, "Executing SIMPLIFY_NaN pass (erasing useless conversion chains)\n");
        (void) args;
        for (auto module : design->selected_modules()) {
            log("Simplifying NaN conversions in module %s\n", log_id(module));
            erase_b2f_pm pm(module, module->selected_cells());
            pool<RTLIL::Cell*> eraseCells;
            pm.run([&]() {
                module->connect(pm.st.base->getPort("\\A"), pm.st.target->getPort("\\Y"));
                eraseCells.insert(pm.st.target);
            });
            for (auto cell : eraseCells) {
                module->remove(cell);
            }
            log("Removed %d bit_to_fp3 nodes\n", GetSize(eraseCells));
        }
    }
} EraseFpBitPass;

struct ShareNaN : public Pass {
    ShareNaN() : Pass("share_nan") {}
    void execute(vector <string> args, Design *design) override {
        log_header(design, "Executing SHARE_NAN pass (merging conversion cells).\n");
        (void) args;
        for (auto module : design->selected_modules()) {
            log("Module %s\n", log_id(module));
            share_nan_pm pm(module, module->selected_cells());
            mfp<RTLIL::Cell*> sharedCells;
            pm.run([&]() { sharedCells.merge(pm.st.cvt_a, pm.st.cvt_b); });
            int merged = 0;
            for (auto &entry : sharedCells) {
                auto &main = sharedCells.find(entry);
                if (entry == main) continue;
                merged += 1;
                module->connect(main->getPort("\\Y"), entry->getPort("\\Y"));
                module->remove(entry);
            }
            log("Merged %d conversion cells\n", merged);
        }
    }
} ShareNaNPass;

struct TechmapNaN : public Pass {
    TechmapNaN() : Pass("techmap_nan", "techmap NaN gates") {}
    void execute(vector<string>, Design *design) override {
        Pass::call(design, "techmap -autoproc -extern -map gateware.sv");
    }
} TechmapNaNPass;

struct SynthNaN : public Pass {
    SynthNaN() : Pass("synth_nan", "synthesize to tom7 logic") {}
    void help() override {
        log("synth_nan [options]\n\n");
        log("Runs the equivalent of the following script:\n\n");
        log("    synth [-top <module>]\n");
        log("    abc -g NAND\n");
        log("    nand_to_nan\n");
        log("    share_nan\n");
        log("    dff_nan\n");
        log("    nan_simplify\n");
        log("    clean\n");
    }
    void execute(vector <string> args, Design *design) override {
        string synth_args;
        log_header(design, "Executing SYNTH_NaN pass (synthesizing to tom7 logic).\n");
        log_push();
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i] == "-top") {
                synth_args += " -top ";
                synth_args += args[i+1];
                i += 1;
                continue;
            }
        }
        Pass::call(design, "synth" + synth_args);
        Pass::call(design, "abc -g NAND");
        Pass::call(design, "nand_to_nan");
        Pass::call(design, "share_nan");
        Pass::call(design, "dff_nan");
        Pass::call(design, "simplify_nan");
        Pass::call(design, "clean");
        Pass::call(design, "techmap_nan");
        log_pop();
    }
} SynthNaNPass;

PRIVATE_NAMESPACE_END

