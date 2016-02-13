#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "pin.H"

UINT32 logPageSize;
UINT32 logPhysicalMemSize;

//Function to obtain physical page number given a virtual page number
UINT32 getPhysicalPageNumber(UINT32 virtualPageNumber)
{
    INT32 key = (INT32) virtualPageNumber;
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return (UINT32) (key&(((UINT32)(~0))>>(32-(logPhysicalMemSize-logPageSize))));
}

class CacheModel
{
    protected:
        UINT32   logNumRows;
        UINT32   logBlockSize;
        UINT32   associativity;
        UINT64   readReqs;
        UINT64   writeReqs;
        UINT64   readHits;
        UINT64   writeHits;
        UINT32** tag;
        bool**   validBit;

    public:
        //Constructor for a cache
        CacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
        {
            logNumRows = logNumRowsParam;
            logBlockSize = logBlockSizeParam;
            associativity = associativityParam;
            readReqs = 0;
            writeReqs = 0;
            readHits = 0;
            writeHits = 0;
            tag = new UINT32*[1u<<logNumRows];
            validBit = new bool*[1u<<logNumRows];
            for(UINT32 i = 0; i < 1u<<logNumRows; i++)
            {
                tag[i] = new UINT32[associativity];
                validBit[i] = new bool[associativity];
                for(UINT32 j = 0; j < associativity; j++)
                    validBit[i][j] = false;
            }       
        }
        // Destructor
        virtual ~CacheModel()
        {
            for(UINT32 i = 0; i < (1U<<logNumRows); i++)
            {
                delete[] tag[i];
                delete[] validBit[i];
            }
            delete[] tag;
            delete[] validBit;
        }

        //Call this function to update the cache state whenever data is read
        virtual void readReq(UINT32 virtualAddr) = 0;

        //Call this function to update the cache state whenever data is written
        virtual void writeReq(UINT32 virtualAddr) = 0;

        //Do not modify this function
        void dumpResults(FILE* outFile)
        {
            fprintf(outFile, "%lu,%lu,%lu,%lu\n", readReqs, writeReqs, readHits, writeHits);
        }
};

CacheModel* cachePP;
CacheModel* cacheVP;
CacheModel* cacheVV;

class LruPhysIndexPhysTagCacheModel: public CacheModel
{
    public:
        LruPhysIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
        }

        void writeReq(UINT32 virtualAddr)
        {
        }
};

class LruVirIndexPhysTagCacheModel: public CacheModel
{
    public:
        LruVirIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
        }

        void writeReq(UINT32 virtualAddr)
        {
        }
};

class LruVirIndexVirTagCacheModel: public CacheModel
{
    public:
        LruVirIndexVirTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        void readReq(UINT32 virtualAddr)
        {
        }

        void writeReq(UINT32 virtualAddr)
        {
        }
};

//Cache analysis routine
void cacheLoad(UINT32 virtualAddr)
{
    //Here the virtual address is aligned to a word boundary
    virtualAddr = (virtualAddr >> 2) << 2;
    cachePP->readReq(virtualAddr);
    cacheVP->readReq(virtualAddr);
    cacheVV->readReq(virtualAddr);
}

//Cache analysis routine
void cacheStore(UINT32 virtualAddr)
{
    //Here the virtual address is aligned to a word boundary
    virtualAddr = (virtualAddr >> 2) << 2;
    cachePP->writeReq(virtualAddr);
    cacheVP->writeReq(virtualAddr);
    cacheVV->writeReq(virtualAddr);
}

// This knob will set the outfile name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
        "o", "results.out", "specify optional output file name");

// This knob will set the param logPhysicalMemSize
KNOB<UINT32> KnobLogPhysicalMemSize(KNOB_MODE_WRITEONCE, "pintool",
        "m", "28", "specify the log of physical memory size in bytes");

// This knob will set the param logPageSize
KNOB<UINT32> KnobLogPageSize(KNOB_MODE_WRITEONCE, "pintool",
        "p", "12", "specify the log of page size in bytes");

// This knob will set the cache param logNumRows
KNOB<UINT32> KnobLogNumRows(KNOB_MODE_WRITEONCE, "pintool",
        "r", "10", "specify the log of number of rows in the cache");

// This knob will set the cache param logBlockSize
KNOB<UINT32> KnobLogBlockSize(KNOB_MODE_WRITEONCE, "pintool",
        "b", "5", "specify the log of block size of the cache in bytes");

// This knob will set the cache param associativity
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
        "a", "2", "specify the associativity of the cache");

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID *v)
{
    if(INS_IsMemoryRead(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheLoad, IARG_MEMORYREAD_EA, IARG_END);
    if(INS_IsMemoryWrite(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cacheStore, IARG_MEMORYWRITE_EA, IARG_END);
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{
    FILE* outfile;
    assert(outfile = fopen(KnobOutputFile.Value().c_str(),"w"));
    fprintf(outfile, "physical index physical tag: ");
    cachePP->dumpResults(outfile);
    fprintf(outfile, "virtual index physical tag: ");
    cacheVP->dumpResults(outfile);
    fprintf(outfile, "virtual index virtual tag: ");
    cacheVV->dumpResults(outfile);
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    logPageSize = KnobLogPageSize.Value();
    logPhysicalMemSize = KnobLogPhysicalMemSize.Value();

    cachePP = new LruPhysIndexPhysTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value()); 
    cacheVP = new LruVirIndexPhysTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value());
    cacheVV = new LruVirIndexVirTagCacheModel(KnobLogNumRows.Value(), KnobLogBlockSize.Value(), KnobAssociativity.Value());

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

