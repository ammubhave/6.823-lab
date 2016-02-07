#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include "pin.H"

// The array storing the spacing frequency between two dependant instructions
UINT64 *dependencySpacing;

INT32 maxSize;

// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.csv", "specify the output file name");

// This knob will set the maximum spacing between two dependant instructions in the program
KNOB<string> KnobMaxSpacing(KNOB_MODE_WRITEONCE, "pintool", "s", "100", "specify the maximum spacing between two dependant instructions in the program");


// Convenience data structure
typedef uint32_t reg_t;
struct Registers {
  std::vector<reg_t> read;
  std::vector<reg_t> write;
};


// Global variables
uint64_t instructionCounter = 0;
uint64_t lastInstructionCount[1024];

// This function is called before every instruction is executed. 
// You have to edit this function to determine the dependency distance
// and populate the dependencySpacing data structure.
VOID updateDependencyDistanceInfo(void * vpParms) {
  // Update the instruction counter
  ++instructionCounter;

  // regs contains the registers read and written by this instruction.
  // regs->read contains the registers read.
  // regs->write contains the registers written.
  Registers * regs = (Registers*) vpParms;

  std::vector<reg_t>::iterator it;
  for (it = regs->read.begin(); it != regs->read.end(); ++it) {
    reg_t reg = *it;

    // Compute the dependency distance
    uint64_t distance = instructionCounter - lastInstructionCount[reg];

    // Populate the dependencySpacing array
    if (distance <= maxSize) {
        // YOUR CODE HERE....
    }
  }

  for (it = regs->write.begin(); it != regs->write.end(); ++it) {
    reg_t reg = *it;
    // Update the lastInstructionCount for this register
    lastInstructionCount[reg] = /**/;
  }
}

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
  // regs stores the registers read, written by this instruction
  Registers* regs = new Registers();

  // Find all the registers read
  for (uint32_t ir = 0; ir < INS_MaxNumRRegs(ins); ir++) {
    REG rr = INS_RegR(ins, ir);
    rr = REG_FullRegName(rr);
    if (!REG_valid(rr)) {
      continue;
    }
    if (std::find(regs->read.begin(), regs->read.end(), rr) == regs->read.end()) {
      regs->read.push_back(rr);
    }
  }

  // Find all the register written
  for (uint32_t iw = 0; iw < INS_MaxNumWRegs(ins); iw++) {
    REG wr = INS_RegW(ins, iw);
    wr = REG_FullRegName(wr);
    if (!REG_valid(wr)) {
      continue;
    }
    if (std::find(regs->write.begin(), regs->write.end(), wr) == regs->write.end()) {
      regs->write.push_back(wr);
    }
  }

  // Insert a call to the analysis function -- updateDependencyDistanceInfo -- before every instruction.
  // Pass the regs structure to the analysis function.
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)updateDependencyDistanceInfo, IARG_PTR, (void*)regs, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    FILE* outfile;
    assert(outfile = fopen(KnobOutputFile.Value().c_str(),"w"));
    for(INT32 i = 0; i < maxSize; i++)
        fprintf(outfile, "%lu,", dependencySpacing[i]);
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    maxSize = atoi(KnobMaxSpacing.Value().c_str());

    // Initializing depdendancy Spacing
    dependencySpacing = new UINT64[maxSize];

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);
    
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}

