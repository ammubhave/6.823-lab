#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include "pin.H"

//#define DEBUG(format, ...) fprintf(stderr, "%d " format "\n", __LINE__, ##__VA_ARGS__)
#define DEBUG(format, ...) 

#define truncate(val, bits) ((val)&((1<<(bits))-1))
const UINT64 MAXIMUM_STORAGE_SIZE = 33792;

static UINT64 takenCorrect = 0;
static UINT64 takenIncorrect = 0;
static UINT64 notTakenCorrect = 0;
static UINT64 notTakenIncorrect = 0;

// N < 64
template <size_t N, UINT64 init = (1<<N)/2-1>
class SaturatingCounter
{
    UINT64 val;
    public:
        SaturatingCounter() {
            reset();
        }

        void increment() {
            if (val < (1<<N)-1)
                val++;
        }

        void decrement() {
            if (val > 0)
                val--;
        }

        void reset() {
            val = init;
        }

        UINT64 getVal() {
            return val;
        }

        BOOL isTaken() {
            if (val > (1<<N)/2-1)
                return true;
            return false;
        }

        // N bit register
        static UINT64 getSize() {
            return N;
        }
};

template <size_t L, size_t N, size_t H, UINT64 init = (1<<N)/2-1>
class SaturatingCounterWithSharedHystersis
{
    UINT64 val[1<<L];
    BOOL hyst[(1<<L)/H];
    public:
        SaturatingCounterWithSharedHystersis() {
            for (UINT64 i = 0; i < (1<<L); i++) {
                reset(i);
            }
        }

        void increment(UINT64 i) {
            if (hyst[i/H] == 0)
                hyst[i/H] = 1;
            else if (val < (1<<(N-1))-1) {
                val++;
                hyst[i/H] = 0;
            }
        }

        void decrement(UINT64 i) {
            if (hyst[i/H] == 1)
                hyst[i/H] = 0;
            else if (val > 0) {
                val--;
                hyst[i/H] = 1;
            }
        }

        void reset(UINT64 i) {
            val[i] = init >> 1;
            hyst[i/H] = init & 1;
        }

        BOOL isTaken() {
            if (val > (1<<(N-1))/2-1)
                return true;
            return false;
        }

        // N bit register
        static UINT64 getSize() {
            return L*N;
        }
};

// N < 64
template<size_t N>
class ShiftRegister
{
    UINT64 val;
    public:
        ShiftRegister() {
            val = 0;
        }

        bool shiftIn(bool b) {
            bool ret = !!(val&(1<<(N-1)));
            val <<= 1;
            val |= b;
            val &= (1<<N)-1;
            return ret;
        }

        UINT64 getVal() {
            return val;
        }

        // N bit register
        static UINT64 getSize() {
            return N;
        }
};

UINT64 f_xor(UINT64 a, UINT64 b) {
    return a ^ b;
}

template<size_t s_a, size_t s_b>
UINT64 f_concat(UINT64 a, UINT64 b) {
    return ((a&((1<<s_a)-1))<<s_a)|(b&((1<<s_b)-1));
}

UINT64 f_a(UINT64 a, UINT64 b) {
    return a;
}

UINT64 f_b(UINT64 a, UINT64 b) {
    return b;
}

template<size_t L, UINT64 s_a, UINT64 s_b>
UINT64 f_folded_xor(UINT64 a, UINT64 b) {
    UINT64 v = 0;
    for (UINT64 i = 0; i < 64/L && (i+1)*L <= s_a; i++) {
        v ^= truncate(a, L);
        a >>= L;
    }
    for (UINT64 i = 0; i < 64/L && (i+1)*L <= s_b; i++) {
        v ^= truncate(b, L);
        b >>= L;
    }
    return v;
}

class BranchPredictor
{
    public:
        BranchPredictor() { }

        virtual BOOL makePrediction(ADDRINT address) { return FALSE; };

        virtual void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {};

        static UINT64 getSize() {
            return 0;
        }

};

template<size_t L>
class BHTPredictor: public BranchPredictor
{
    SaturatingCounter<2> counter[1<<L];
    public:
        BHTPredictor() { assert (getSize() <= MAXIMUM_STORAGE_SIZE); }

        BOOL makePrediction(ADDRINT address) {
            return counter[truncate(address, L)].isTaken();
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(address, L);
            if (takenActually)
                counter[idx].increment();
            else
                counter[idx].decrement();
        }

        static UINT64 getSize() {
            return (1<<L)*SaturatingCounter<2>::getSize();
        }
};

template<size_t L, size_t H>
class BHTPredictorWithSharedHysteresis: public BranchPredictor
{
    BOOL counter[1<<L];
    BOOL hystersis[(1<<L)/H];
    public:
        BHTPredictorWithSharedHysteresis() { assert (getSize() <= MAXIMUM_STORAGE_SIZE); }

        BOOL makePrediction(ADDRINT address) {
            return counter[truncate(address, L)];
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(address, L);
            if (takenActually) {
                if (!hystersis[idx/H])
                    hystersis[idx/H] = true;
                else if (!counter[idx]) {
                    counter[idx] = true; hystersis[idx/H] = false;
                }
            } else {
                if (hystersis[idx/H])
                    hystersis[idx/H] = false;
                else if (counter[idx]) {
                    counter[idx] = false; hystersis[idx/H] = true;
                }
            }
        }

        static UINT64 getSize() {
            return (1<<L)+((1<<L)/H);
        }
};

template<size_t L, size_t H, UINT64 (*hash)(UINT64 address, UINT64 history), UINT64 bits = 2>
class GlobalHistoryPredictor: public BranchPredictor
{
    // http://www.eng.utah.edu/~cs6810/pres/10-6810-08.pdf
    SaturatingCounter<bits> counter[1<<L];
    ShiftRegister<H> globalHistory;
    public:
        GlobalHistoryPredictor() { assert (getSize() <= MAXIMUM_STORAGE_SIZE); }
        BOOL makePrediction(ADDRINT address) {
            return counter[truncate(hash(address, globalHistory.getVal()), L)].isTaken();
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT32 idx = truncate(hash(address, globalHistory.getVal()), L);
            if (takenActually)
                counter[idx].increment();
            else
                counter[idx].decrement();
            globalHistory.shiftIn(takenActually);
        }

        UINT64 getHistory() {
            return globalHistory.getVal();
        }

        static UINT64 getSize() {
            return (1<<L)*SaturatingCounter<bits>::getSize() + ShiftRegister<H>::getSize();
        }
};

template<size_t L, size_t H, size_t HL, UINT64 (*hash)(UINT64 address, UINT64 history), UINT64 bits = 2>
class LocalHistoryPredictor: public BranchPredictor
{
    SaturatingCounter<bits> counter[1<<L];
    ShiftRegister<H> hists[1<<HL];

    public:
        LocalHistoryPredictor() { assert (getSize() <= MAXIMUM_STORAGE_SIZE); }

        BOOL makePrediction(ADDRINT address) {
            UINT64 hists_idx = truncate(address, HL);
            return counter[truncate(hash(address, hists[hists_idx].getVal()), L)].isTaken();
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 hists_idx = truncate(address, HL);
            UINT64 idx = truncate(hash(address, hists[hists_idx].getVal()), L);
            if (takenActually)
                counter[idx].increment();
            else
                counter[idx].decrement();
            hists[hists_idx].shiftIn(takenActually);
        }

        static UINT64 getSize() {
            return (1<<L)*SaturatingCounter<bits>::getSize() + (1<<HL)*ShiftRegister<H>::getSize();
        }
};

template<size_t L, UINT64 bits = 2>
class TournamentPredictor: public BranchPredictor {
    SaturatingCounter<bits> counter[1<<L];
    BranchPredictor* BPs[2];

    public:
        TournamentPredictor(BranchPredictor* BP0, BranchPredictor* BP1) {
            BPs[0] = BP0;
            BPs[1] = BP1;
            assert (getSize() <= MAXIMUM_STORAGE_SIZE);
        }

        BOOL makePrediction(ADDRINT address) {
            if (counter[truncate(address, L)].isTaken())
                return BPs[1]->makePrediction(address);
            else
                return BPs[0]->makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(address, L);
            BOOL pred_0 = BPs[0]->makePrediction(address);
            BOOL pred_1 = BPs[1]->makePrediction(address);
            if (takenActually != pred_0 && takenActually == pred_1)
                counter[idx].increment();
            else if (takenActually == pred_0 && takenActually != pred_1)
                counter[idx].decrement();
            BPs[0]->makeUpdate(takenActually, takenPredicted, address);
            BPs[1]->makeUpdate(takenActually, takenPredicted, address);
        }

        UINT64 getSize() {
            return BPs[0]->getSize() + BPs[1]->getSize() + (1<<L)*SaturatingCounter<bits>::getSize();
        }
};

template<size_t L>
class Alpha21264Predictor: public BranchPredictor {
    SaturatingCounter<2> counter[1<<L];
    GlobalHistoryPredictor<L, L, &f_xor> GP;
    LocalHistoryPredictor<10, 10, 10, &f_b, 3> LP;

    public:
        Alpha21264Predictor() {
            assert (getSize() <= MAXIMUM_STORAGE_SIZE);
        }

        BOOL makePrediction(ADDRINT address) {
            if (counter[truncate(GP.getHistory(), L)].isTaken())
                return GP.makePrediction(address);
            else
                return LP.makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(GP.getHistory(), L);
            BOOL pred_0 = GP.makePrediction(address);
            BOOL pred_1 = LP.makePrediction(address);
            if (takenActually != pred_0 && takenActually == pred_1)
                counter[idx].decrement();
            else if (takenActually == pred_0 && takenActually != pred_1)
                counter[idx].increment();
            GP.makeUpdate(takenActually, takenPredicted, address);
            LP.makeUpdate(takenActually, takenPredicted, address);
        }

        UINT64 getSize() {
            return GP.getSize() + LP.getSize() + (1<<L)*SaturatingCounter<2>::getSize();
        }
};

class TagePredictorComponentBase {
public:
    TagePredictorComponentBase() { }

    virtual BOOL predict(ADDRINT address, UINT64 hist, BOOL *taken) { assert (false); return false; };

    virtual void update(BOOL takenActually, BOOL takenPredicted, ADDRINT address, UINT64 hist, BOOL altpred) { };

    virtual BOOL allocate(ADDRINT address, UINT64 hist, BOOL takenActually) { assert (false); return false; };

    virtual void decrement_u(ADDRINT address, UINT64 hist) { };

    virtual void reset_u() { };

    virtual UINT64 getSize() { assert (false); return 0; };
};

template<size_t LL, size_t T, UINT64 (*hash)(UINT64 address, UINT64 history), UINT64 (*hash_tag)(UINT64 address, UINT64 history)>
class TagePredictorComponent : public TagePredictorComponentBase {
    SaturatingCounter<3> ctr[1<<LL];
    SaturatingCounter<2, 0> u[1<<LL];
    UINT64 tag[1<<LL];

    public:
        TagePredictorComponent() {
            assert (getSize() <= MAXIMUM_STORAGE_SIZE);
        }

        BOOL predict(ADDRINT address, UINT64 hist, BOOL* taken) {
            DEBUG("DEBUG");
            UINT64 idx = truncate(hash(address, hist), LL);
            *taken = ctr[idx].isTaken();
            DEBUG("DEBUG");
            return (tag[idx] == truncate(address, T));
        }

        void update(BOOL takenActually, BOOL takenPredicted, ADDRINT address, UINT64 hist, BOOL altpred) {
            DEBUG("DEBUG");
            int idx = truncate(hash(address, hist), LL);
            if (altpred != takenPredicted) {
                if (takenActually == takenPredicted)
                    u[idx].increment();
                else
                    u[idx].decrement();
            }

            DEBUG("DEBUG");
            if (takenActually)
                ctr[idx].increment();
            else
                ctr[idx].decrement();
            DEBUG("DEBUG");
        }

        BOOL allocate(ADDRINT address, UINT64 hist, BOOL takenActually) {
            DEBUG("DEBUG");
            int idx = truncate(hash(address, hist), LL);
            if (u[idx].getVal() == 0) {
                ctr[idx].reset();
                if (takenActually)
                    ctr[idx].increment();
                tag[idx] = hash_tag(address, hist);
                return true;
            }
            DEBUG("DEBUG");
            return false;
        }

        void decrement_u(ADDRINT address, UINT64 hist) {
            u[truncate(hash(address, hist), LL)].decrement();
        }

        void reset_u() {
            for (UINT64 i = 0; i < (1<<LL); i++) {
                u[i].reset();
            }
        }

        UINT64 getSize() {
            return (1<<LL)*SaturatingCounter<3>::getSize() + (1<<LL)*SaturatingCounter<2>::getSize() + (1<<LL)*T;
        }
};

template<size_t LL, size_t H>
class TageBasePredictor : public TagePredictorComponentBase {
    BHTPredictorWithSharedHysteresis<LL, H> T0;

    public:
        TageBasePredictor() {
            assert (getSize() <= MAXIMUM_STORAGE_SIZE);
        }

        BOOL predict(ADDRINT address, UINT64 hist, BOOL* taken) {
            DEBUG("DEBUG");
            *taken = T0.makePrediction(address);
            DEBUG("DEBUG");
            return true;
        }

        void update(BOOL takenActually, BOOL takenPredicted, ADDRINT address, UINT64 hist, BOOL altpred) {
            DEBUG("DEBUG");
            T0.makeUpdate(takenActually, takenPredicted, address);
            DEBUG("DEBUG");
        }

        BOOL allocate(ADDRINT address, UINT64 hist, BOOL takenActually) { assert (false); }

        void decrement_u(ADDRINT address, UINT64 hist) { 
            assert (false);
        }

        void reset_u() { }

        UINT64 getSize() {
            return BHTPredictor<LL>::getSize();
        }
};

template<size_t N, size_t G>
class TagePredictor : public BranchPredictor {
    TagePredictorComponentBase* Ts[N];
    ShiftRegister<G> globalHistory;
    UINT64 noOfBranches;

    public:
        TagePredictor(TagePredictorComponentBase* T0, ...) {
            va_list stages;
            va_start(stages, T0);
            Ts[0] = T0;
            for (UINT64 i = 1; i < N; i++) {
                TagePredictorComponentBase* j = va_arg(stages, TagePredictorComponentBase*);
                Ts[i] = j;
            }
            va_end(stages);
            noOfBranches = 0;
            assert (getSize() <= MAXIMUM_STORAGE_SIZE);
        }

        void getPredictionsAndProviders(ADDRINT address, UINT64 *pred_p, bool *pred, bool* altpred) {
            DEBUG("DEBUG");
            UINT64 i;
            for (i = N - 1; i >= 0; i--) {
                if (Ts[i]->predict(address, globalHistory.getVal(), pred))
                    break;
            }
            DEBUG("DEBUG");

            if (pred_p)
                *pred_p = i;
            if (!altpred)
                return;

            DEBUG("DEBUG");
            for(i--; i >= 0 && i < N; i--) {
                DEBUG("DEBUG %lu", i);
                if (Ts[i]->predict(address, globalHistory.getVal(), altpred))
                    return;
            }
            DEBUG("DEBUG");
            *altpred = *pred;
        }

        BOOL makePrediction(ADDRINT address) {
            DEBUG("DEBUG");
            BOOL pred;
            getPredictionsAndProviders(address, NULL, &pred, NULL);
            DEBUG("DEBUG");
            return pred;
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            noOfBranches++;
            if (noOfBranches >= 256e3) {
                noOfBranches = 0;
                for (UINT64 i = 0; i < N; i++) {
                    Ts[i]->reset_u();
                }
            }

            DEBUG("DEBUG");
            UINT64 pred_p;
            BOOL pred, altpred;
            getPredictionsAndProviders(address, &pred_p, &pred, &altpred);
            assert (pred == takenPredicted);
            DEBUG("DEBUG");
            Ts[pred_p]->update(takenActually, takenPredicted, address, globalHistory.getVal(), altpred);
            DEBUG("DEBUG");
            if (takenActually != takenPredicted) {
                UINT64 k_offset = 0;
                UINT64 tmp = rand() % (1 << (N-pred_p-1));
                while (tmp >>= 1) k_offset++;
                for (UINT64 k = 0; k < N - (pred_p + 1); k++) {
                    if (Ts[pred_p + 1 + ((k+k_offset)%(N - (pred_p + 1)))]->allocate(address, globalHistory.getVal(), takenActually))
                        return;
                    DEBUG("DEBUG");
                }

                // Allocation failed, decrement useful counters
                for (UINT64 j = pred_p + 1; j < N; j++) {
                    Ts[j]->decrement_u(address, globalHistory.getVal());
                }
            }
            DEBUG("DEBUG");
        }

        UINT64 getSize() {
            UINT64 size = ShiftRegister<G>::getSize();
            for (UINT64 i = 0; i < N; i++) {
                size += Ts[i]->getSize();
            }
            return size;
        }
};

template<size_t N, size_t L>
class NaiveBPAT : public BranchPredictor {
    ShiftRegister<2*N> history[1<<L];
    SaturatingCounter<2> counter[1<<L];
    BranchPredictor* altPredictor;

    public:
        NaiveBPAT(BranchPredictor* alt) {
            altPredictor = alt;
            assert(getSize() < MAXIMUM_STORAGE_SIZE);
        }

        BOOL predict(ADDRINT address, BOOL* pred) {
            UINT64 idx = truncate(address, L);
            UINT64 haystack = history[idx].getVal();
            UINT64 needle = truncate(haystack, N);

            for (UINT64 i = 0; i < N; i++) {
                *pred = haystack&1;
                haystack >>= 1;
                if (truncate(haystack, N) == needle)
                    return true;
            }
            return false;
        }

        BOOL makePrediction(ADDRINT address) {
            BOOL pred;
            if (predict(address, &pred) && !counter[truncate(address, L)].isTaken())
                return pred;
            return altPredictor->makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(address, L);
            BOOL pred, altpred;
            altpred = altPredictor->makePrediction(address);
            altPredictor->makeUpdate(takenActually, altpred, address);
            if (predict(address, &pred)) {
                if (pred == takenActually && altpred != takenActually)
                    counter[idx].decrement();
                else if (pred != takenActually && altpred == takenActually)
                    counter[idx].increment();
            } else if (altpred == takenActually) {
                counter[idx].increment();
            }

            history[idx].shiftIn(takenActually);
        }

        UINT64 getSize() {
            return altPredictor->getSize() + (1<<L)*ShiftRegister<2*N>::getSize();
        }
};

template<size_t N, size_t L, size_t N2, size_t G2>
class nBPATGShare : public BranchPredictor {
    ShiftRegister<2*N> history[1<<L];
    SaturatingCounter<2> counter[1<<L];
    GlobalHistoryPredictor<N2, G2, &f_xor> altPredictor;

    public:
        nBPATGShare() {
            assert(getSize() < MAXIMUM_STORAGE_SIZE);
        }

        BOOL predict(ADDRINT address, BOOL* pred) {
            UINT64 idx = truncate(address, L);
            UINT64 haystack = history[idx].getVal();
            UINT64 needle = truncate(haystack, N);

            for (UINT64 i = 0; i < N; i++) {
                *pred = haystack&1;
                haystack >>= 1;
                if (truncate(haystack, N) == needle)
                    return true;
            }
            return false;
        }

        BOOL makePrediction(ADDRINT address) {
            BOOL pred;
            if (predict(address, &pred) && !counter[truncate(address, L)].isTaken())
                return pred;
            return altPredictor.makePrediction(address);
        }

        void makeUpdate(BOOL takenActually, BOOL takenPredicted, ADDRINT address) {
            UINT64 idx = truncate(address, L);
            BOOL pred, altpred;
            altpred = altPredictor.makePrediction(address);
            altPredictor.makeUpdate(takenActually, altpred, address);
            if (predict(address, &pred)) {
                if (pred == takenActually && altpred != takenActually)
                    counter[idx].decrement();
                else if (pred != takenActually && altpred == takenActually)
                    counter[idx].increment();
            } else if (altpred == takenActually) {
                counter[idx].increment();
            }

            history[idx].shiftIn(takenActually);
        }

        UINT64 getSize() {
            return altPredictor.getSize() + (1<<L)*ShiftRegister<2*N>::getSize() + (1<<L)*SaturatingCounter<2>::getSize();
        }
};

BranchPredictor* BP;


// This knob sets the output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "result.out", "specify the output file name");


// In examining handle branch, refer to quesiton 1 on the homework
void handleBranch(ADDRINT ip, BOOL direction)
{
    BOOL prediction = BP->makePrediction(ip);
    BP->makeUpdate(direction, prediction, ip);
    if(prediction)
    {
        if(direction)
        {
            takenCorrect++;
        }
        else
        {
            takenIncorrect++;
        }
    }
    else
    {
        if(direction)
        {
            notTakenIncorrect++;
        }
        else
        {
            notTakenCorrect++;
        }
    }
}


void instrumentBranch(INS ins, void * v)
{   
    if(INS_IsBranch(ins) && INS_HasFallThrough(ins))
    {
        INS_InsertCall(
                ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)handleBranch,
                IARG_INST_PTR,
                IARG_BOOL,
                TRUE,
                IARG_END); 

        INS_InsertCall(
                ins, IPOINT_AFTER, (AFUNPTR)handleBranch,
                IARG_INST_PTR,
                IARG_BOOL,
                FALSE,
                IARG_END);
    }
}


/* ===================================================================== */
VOID Fini(int, VOID * v)
{   
    FILE* outfile;
    assert(outfile = fopen(KnobOutputFile.Value().c_str(),"w"));   
    fprintf(outfile, "takenCorrect %lu  takenIncorrect %lu notTakenCorrect %lu notTakenIncorrect %lu\n", takenCorrect, takenIncorrect, notTakenCorrect, notTakenIncorrect);
}


// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char * argv[])
{
    // Make a new branch predictor
    //BP = new BHTPredictor<14>();
    //90% on both SPECINT and SPECFP

    //BP = new GlobalHistoryPredictor<14, 14, &f_xor>();
    //94% on SPECINT, 98% on SPECFP, avg 96%

    //BP = new LocalHistoryPredictor<14, 14, 6, &f_xor>();
    //87% on SPECINT, 99% on SPECFP, avg 92%

    // Alpha-21264 Predictor
    BP = new Alpha21264Predictor<12>();

    // Chooser is indexed by the address bits
    //BP = new TournamentPredictor<12>(
    //    new GlobalHistoryPredictor<12, 12, &f_xor>(),
    //    new LocalHistoryPredictor<10, 10, 10, &f_b, 3>()
    //);
    //94% on SPECINT, 99% on SPECFP, avg 96%

    // DEBUG("BP Initializing");
    // BP = new TagePredictor<5, 40>(
    //     new TageBasePredictor<11, 1>(),
    //     new TagePredictorComponent<9, 9, &f_folded_xor<9, 9, 5>, &f_folded_xor<9, 64, 5>>(),
    //     new TagePredictorComponent<9, 9, &f_folded_xor<9, 9, 10>, &f_folded_xor<9, 64, 10>>(),
    //     new TagePredictorComponent<9, 9, &f_folded_xor<9, 9, 20>, &f_folded_xor<9, 64, 20>>(),
    //     new TagePredictorComponent<9, 9, &f_folded_xor<9, 9, 40>, &f_folded_xor<9, 64, 40>>()
    // );
    // DEBUG("BP Initialized");

    // BP = new NaiveBPAT<8, 11>(
    //     new Alpha21264Predictor<11>();
    // );
//    BP = new nBPATGShare<12, 10, 11, 12>();

    // Initialize pin
    PIN_Init(argc, argv);

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(instrumentBranch, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

