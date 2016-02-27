#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "pin.H"

#define DEBUG 0

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
        UINT32** lruQ;

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
            lruQ = new UINT32*[1u<<logNumRows];
            for(UINT32 i = 0; i < 1u<<logNumRows; i++)
            {
                tag[i] = new UINT32[associativity];
                validBit[i] = new bool[associativity];
                lruQ[i] = new UINT32[associativity];
                for(UINT32 j = 0; j < associativity; j++) {
                    validBit[i][j] = false;
                    lruQ[i][j] = j;
                }
            }
        }
        // Destructor
        virtual ~CacheModel()
        {
            for(UINT32 i = 0; i < (1U<<logNumRows); i++)
            {
                delete[] tag[i];
                delete[] validBit[i];
                delete[] lruQ[i];
            }
            delete[] tag;
            delete[] validBit;
            delete[] lruQ;
        }

        //Call this function to update the cache state whenever data is read
        virtual void readReq(UINT32 virtualAddr) = 0;

        //Call this function to update the cache state whenever data is written
        virtual void writeReq(UINT32 virtualAddr) = 0;

        bool searchAddr(UINT32 addr, UINT32* r_j) {
            UINT32 x_tag = getTag(addr), x_idx = getIdx(addr);
            for(UINT32 j = 0; j < associativity; j++) {
                if (validBit[x_idx][j] && tag[x_idx][j] == x_tag) {
                    *r_j = j; return true;
                }
            }
            return false;
        }

        UINT32 getPageNumber(UINT32 addr) {
            return addr >> logPageSize;
        }
        UINT32 getPageOffset(UINT32 addr) {
            return addr & ((1u<<logPageSize)-1);
        }
        UINT32 getTag(UINT32 addr) {
            return addr >> (logBlockSize + logNumRows);
        }
        UINT32 getIdx(UINT32 addr) {
            return (addr >> logBlockSize) & ((1u<<logNumRows)-1);
        }
        UINT32 getOffset(UINT32 addr) {
            return addr & ((1u<<logBlockSize)-1);
        }

        // Make address from tag, idx and offset
        UINT32 makeAddr(UINT32 tag, UINT32 idx, UINT32 offset) {
            assert (idx < (1u<<logNumRows));
            assert (offset < (1u<<logBlockSize));

            return (tag << (logBlockSize + logNumRows)) | (idx << logBlockSize) | offset;
        }
        // Make address from ppn and offset
        UINT32 makeAddr(UINT32 pageNumber, UINT32 pageOffset) {
            assert (pageOffset < (1u<<logPageSize));

            return (pageNumber << logPageSize) | pageOffset;
        }

        // Pushes x_j to the very back of lruQ[idx] 
        UINT32 lruTouch(UINT32 idx, UINT32 x_j) {
            assert (idx < (1u<<logNumRows));
            assert (x_j < associativity);

            UINT32 j = 0;
            while (lruQ[idx][j] != x_j) {
                if (DEBUG)  printf("%d %d %d\n", j, x_j, lruQ[idx][j] );
                j++;
            }
            while (j < associativity - 1) {
                lruQ[idx][j] = lruQ[idx][j + 1];
                j++;
            }
            lruQ[idx][associativity - 1] = x_j;
        }
        UINT32 lruHead(UINT32 idx) {
            return lruQ[idx][0];
        }

        //Do not modify this function
        void dumpResults(FILE* outFile)
        {
            fprintf(outFile, "%lu,%lu,%lu,%lu\n", readReqs, writeReqs, readHits, writeHits);
        }
};

CacheModel* cachePP;
CacheModel* cacheVP;
CacheModel* cacheVV;
UINT64 numMisalignedLoads = 0;
UINT64 numMisalignedStores = 0;
UINT32 lowestPhysicalAddr = -1;
UINT32 highestPhysicalAddr = 0;
UINT32 lowestVirtualAddr = -1;
UINT32 highestVirtualAddr= 0;

class LruPhysIndexPhysTagCacheModel: public CacheModel
{
    public:
        LruPhysIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        bool access(UINT32 virtualAddr) {
            UINT32 physicalAddr = makeAddr(getPhysicalPageNumber(getPageNumber(virtualAddr)), getPageOffset(virtualAddr));


            if (physicalAddr > highestPhysicalAddr) highestPhysicalAddr = virtualAddr;
            if (physicalAddr < lowestPhysicalAddr) lowestPhysicalAddr = virtualAddr;


            UINT32 idx = getIdx(physicalAddr), j;
            if (DEBUG) printf("Searching %x in cache\n", physicalAddr);
            bool isHit = searchAddr(physicalAddr, &j);
            if (isHit) {
                if (DEBUG) printf("Got hit, updating metadata\n");
                lruTouch(idx, j);
                if (DEBUG) printf("Access (HIT) successful\n");
                return true;
            }
            if (DEBUG) printf("Got miss, updating metadata\n");
            UINT32 lru_j = lruHead(idx);
            tag[idx][lru_j] = getTag(physicalAddr);
            validBit[idx][lru_j] = true;
            if (DEBUG) printf("Updating lruQ\n");
            lruTouch(idx, lru_j);
            if (DEBUG) printf("Access (MISS) successful\n");
            return false;
        }

        void readReq(UINT32 virtualAddr)
        {
            readReqs++;
            if (access(virtualAddr))
                readHits++;
        }

        void writeReq(UINT32 virtualAddr)
        {
            writeReqs++;
            if (access(virtualAddr))
                writeHits++;
        }
};

class LruVirIndexPhysTagCacheModel: public CacheModel
{
    public:
        LruVirIndexPhysTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
           // printf("%d %d %d %d", logBlockSizeParam, logNumRows, logPageSize, associativity);
           // assert (logBlockSize + logNumRows < logPageSize && associativity == 1 ||
           //         logBlockSize + logNumRows == logPageSize);
        }

        bool access(UINT32 virtualAddr) {
            UINT32 physicalAddr = makeAddr(getPhysicalPageNumber(getPageNumber(virtualAddr)), getPageOffset(virtualAddr));
            UINT32 idx = getIdx(virtualAddr), j;
            UINT32 effectiveAddr = ((physicalAddr >> (logNumRows+logBlockSize)) << (logNumRows+logBlockSize)) | (virtualAddr & ((1u<<(logNumRows+logBlockSize))-1));
            bool isHit = searchAddr(effectiveAddr, &j);
            if (isHit) {
                lruTouch(idx, j);
                return true;
            }
            UINT32 lru_j = lruHead(idx);
            tag[idx][lru_j] = getTag(effectiveAddr);
            validBit[idx][lru_j] = true;
            lruTouch(idx, lru_j);
            return false;
        }

        void readReq(UINT32 virtualAddr)
        {
            if (logBlockSize + logNumRows > logPageSize) return;
            readReqs++;
            if (access(virtualAddr))
                readHits++;
        }

        void writeReq(UINT32 virtualAddr)
        {
            if (logBlockSize + logNumRows > logPageSize) return;
            writeReqs++;
            if (access(virtualAddr))
                writeHits++;
        }
};

class LruVirIndexVirTagCacheModel: public CacheModel
{
    public:
        LruVirIndexVirTagCacheModel(UINT32 logNumRowsParam, UINT32 logBlockSizeParam, UINT32 associativityParam)
            : CacheModel(logNumRowsParam, logBlockSizeParam, associativityParam)
        {
        }

        bool access(UINT32 virtualAddr) {
            UINT32 idx = getIdx(virtualAddr), j;
            bool isHit = searchAddr(virtualAddr, &j);
            if (isHit) {
                lruTouch(idx, j);
                return true;
            }
            UINT32 lru_j = lruHead(idx);
            tag[idx][lru_j] = getTag(virtualAddr);
            validBit[idx][lru_j] = true;
            lruTouch(idx, lru_j);
            return false;
        }

        void readReq(UINT32 virtualAddr)
        {
            readReqs++;
            if (access(virtualAddr))
                readHits++;
        }

        void writeReq(UINT32 virtualAddr)
        {
            writeReqs++;
            if (access(virtualAddr))
                writeHits++;
        }
};

//Cache analysis routine
void cacheLoad(UINT32 virtualAddr)
{
    //Here the virtual address is aligned to a word boundary
    if (virtualAddr & 0x3)
        numMisalignedLoads++;
    if (virtualAddr > highestVirtualAddr) highestVirtualAddr = virtualAddr;
    if (virtualAddr < lowestVirtualAddr) lowestVirtualAddr = virtualAddr;
    virtualAddr = (virtualAddr >> 2) << 2;
    cachePP->readReq(virtualAddr);
    cacheVP->readReq(virtualAddr);
    cacheVV->readReq(virtualAddr);
}

//Cache analysis routine
void cacheStore(UINT32 virtualAddr)
{
    if (virtualAddr & 0x3)
        numMisalignedStores++;
    if (virtualAddr > highestVirtualAddr) highestVirtualAddr = virtualAddr;
    if (virtualAddr < lowestVirtualAddr) lowestVirtualAddr = virtualAddr;
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
    //fprintf(outfile, "milaligned loads and stores: "); 
    //fprintf(outfile, "%lu,%lu\n", numMisalignedLoads, numMisalignedStores);
    fprintf(outfile, "highest and lowest virtual addr: "); 
    fprintf(outfile, "%x,%x\n", highestVirtualAddr, lowestVirtualAddr);
    fprintf(outfile, "highest and lowest physical addr: "); 
    fprintf(outfile, "%x,%x\n", highestPhysicalAddr, lowestPhysicalAddr);
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

