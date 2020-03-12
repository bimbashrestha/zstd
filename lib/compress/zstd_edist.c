#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "zstd_edist.h"
#include "mem.h"

#define ZSTD_EDIST_DIAG_MAX (S32)(1 << 30)

typedef struct {
    U32 dictIdx;
    U32 srcIdx;
    U32 matchLength;
} ZSTD_eDist_match;

typedef struct {
    const BYTE* dict;
    const BYTE* src;
    size_t dictSize;
    size_t srcSize;
    S32* forwardDiag;
    S32* backwardDiag;
    ZSTD_eDist_match* matches;
    U32 nbMatches;
} ZSTD_eDist_state;

typedef struct {
    S32 dictMid;
    S32 srcMid;
} ZSTD_eDist_partition;

static void ZSTD_eDist_diag(ZSTD_eDist_state* state,
                    ZSTD_eDist_partition* partition,
                    S32 dictLow, S32 dictHigh, S32 srcLow, 
                    S32 srcHigh)
{
    S32* const forwardDiag = state->forwardDiag;
    S32* const backwardDiag = state->backwardDiag;
    const BYTE* const dict = state->dict;
    const BYTE* const src = state->src;

    S32 const diagMin = dictLow - srcHigh;
    S32 const diagMax = dictHigh - srcLow;
    S32 const forwardMid = dictLow - srcLow;
    S32 const backwardMid = dictHigh - srcHigh;

    S32 forwardMin = forwardMid;
    S32 forwardMax = forwardMid;
    S32 backwardMin = backwardMid;
    S32 backwardMax = backwardMid;
    int odd = (forwardMid - backwardMid) & 1;

    forwardDiag[forwardMid] = dictLow;
    backwardDiag[backwardMid] = dictHigh;

    while (1) {
        S32 diag;
        
        if (forwardMin > diagMin) {
            forwardMin--;
            forwardDiag[forwardMin - 1] = -1;
        } else {
            forwardMin++;
        }

        if (forwardMax < diagMax) {
            forwardMax++;
            forwardDiag[forwardMax + 1] = -1;
        } else {
            forwardMax--;
        }

        for (diag = forwardMax; diag >= forwardMin; diag -= 2) {
            S32 dictIdx;
            S32 srcIdx;
            S32 low = forwardDiag[diag - 1];
            S32 high = forwardDiag[diag + 1];
            S32 dictIdx0 = low < high ? high : low + 1;

            for (dictIdx = dictIdx0, srcIdx = dictIdx0 - diag;
                dictIdx < dictHigh && srcIdx < srcHigh && dict[dictIdx] == src[srcIdx];
                dictIdx++, srcIdx++) continue;

            forwardDiag[diag] = dictIdx;

            if (odd && backwardMin <= diag && diag <= backwardMax && backwardDiag[diag] <= dictIdx) {
                partition->dictMid = dictIdx;
                partition->srcMid = srcIdx;
                return;
            }
        }

        if (backwardMin > diagMin) {
            backwardMin--;
            backwardDiag[backwardMin - 1] = ZSTD_EDIST_DIAG_MAX;
        } else {
            backwardMin++;
        }

        if (backwardMax < diagMax) {
            backwardMax++;
            backwardDiag[backwardMax + 1] = ZSTD_EDIST_DIAG_MAX;
        } else {
            backwardMax--;
        }


        for (diag = backwardMax; diag >= backwardMin; diag -= 2) {
            S32 dictIdx;
            S32 srcIdx;
            S32 low = backwardDiag[diag - 1];
            S32 high = backwardDiag[diag + 1];
            S32 dictIdx0 = low < high ? low : high - 1;

            for (dictIdx = dictIdx0, srcIdx = dictIdx0 - diag;
                dictLow < dictIdx && srcLow < srcIdx && dict[dictIdx - 1] == src[srcIdx - 1];
                dictIdx--, srcIdx--) continue;

            backwardDiag[diag] = dictIdx;

            if (!odd && forwardMin <= diag && diag <= forwardMax && dictIdx <= forwardDiag[diag]) {
                partition->dictMid = dictIdx;
                partition->srcMid = srcIdx;
                return;
            }
        }
    }
}

static void ZSTD_eDist_insertMatch(ZSTD_eDist_state* state, 
                    S32 const dictIdx, S32 const srcIdx)
{
    state->matches[state->nbMatches].dictIdx = dictIdx;
    state->matches[state->nbMatches].srcIdx = srcIdx;
    state->matches[state->nbMatches].matchLength = 1;
    state->nbMatches++;
}

static int ZSTD_eDist_matchComp(const void* p, const void* q)
{
    S32 const l = ((ZSTD_eDist_match*)p)->srcIdx;
    S32 const r = ((ZSTD_eDist_match*)q)->srcIdx;
    return (l - r);
}

static int ZSTD_eDist_compare(ZSTD_eDist_state* state,
                    S32 dictLow, S32 dictHigh, S32 srcLow,
                    S32 srcHigh)
{
    const BYTE* const dict = state->dict;
    const BYTE* const src = state->src;

    while (dictLow < dictHigh && srcLow < srcHigh && dict[dictLow] == src[srcLow]) {
        ZSTD_eDist_insertMatch(state, dictLow, srcLow);
        dictLow++;
        srcLow++;
    }

    while (dictLow < dictHigh && srcLow < srcHigh && dict[dictHigh - 1] == src[srcHigh - 1]) {
        ZSTD_eDist_insertMatch(state, dictHigh - 1, srcHigh - 1);
        dictHigh--;
        srcHigh--;
    }
    
    if (dictLow == dictHigh) {
        while (srcLow < srcHigh)
            srcLow++;
    } else if (srcLow == srcHigh) {
        while (dictLow < dictHigh) 
            dictLow++;
    } else {
        ZSTD_eDist_partition partition;
        partition.dictMid = 0;
        partition.srcMid = 0;
        ZSTD_eDist_diag(state, &partition, dictLow, dictHigh, 
            srcLow, srcHigh);
        if (ZSTD_eDist_compare(state, dictLow, partition.dictMid, 
          srcLow, partition.srcMid))
            return 1;
        if (ZSTD_eDist_compare(state, partition.dictMid, dictHigh,
          partition.srcMid, srcHigh))
            return 1;
    }

    return 0;
}

static void ZSTD_eDist_combineMatches(ZSTD_eDist_state* state)
{
    ZSTD_eDist_match* combinedMatches = 
        ZSTD_malloc(state->nbMatches * sizeof(ZSTD_eDist_match), 
        ZSTD_defaultCMem);

    U32 nbCombinedMatches = 1;
    size_t i;
    qsort(state->matches, state->nbMatches, sizeof(ZSTD_eDist_match), ZSTD_eDist_matchComp);
    memcpy(combinedMatches, state->matches, sizeof(ZSTD_eDist_match));
    for (i = 1; i < state->nbMatches; i++) {
        ZSTD_eDist_match const match = state->matches[i];
        ZSTD_eDist_match const combinedMatch = 
            combinedMatches[nbCombinedMatches - 1];

        if (combinedMatch.srcIdx + combinedMatch.matchLength == match.srcIdx) {
            combinedMatches[nbCombinedMatches - 1].matchLength++;
        } else {
            if (combinedMatches[nbCombinedMatches - 1].matchLength < MINMATCH) {
                nbCombinedMatches--;
            }
            memcpy(combinedMatches + nbCombinedMatches, 
                state->matches + i, sizeof(ZSTD_eDist_match));
            nbCombinedMatches++;
        }
    }

    memcpy(state->matches, combinedMatches, nbCombinedMatches * sizeof(ZSTD_eDist_match));
    state->nbMatches = nbCombinedMatches;

    ZSTD_free(combinedMatches, ZSTD_defaultCMem);
}

static size_t ZSTD_eDist_convertMatchesToSequences(ZSTD_Sequence* sequences, 
    ZSTD_eDist_state* state)
{
    const ZSTD_eDist_match* matches = state->matches;
    size_t const nbMatches = state->nbMatches;
    size_t const dictSize = state->dictSize;
    size_t nbSequences = 0;
    size_t i;
    for (i = 0; i < nbMatches; i++) {
        ZSTD_eDist_match const match = matches[i];
        U32 const litLength = !i ? match.srcIdx : 
            match.srcIdx - (matches[i - 1].srcIdx + matches[i - 1].matchLength);
        U32 const offset = (match.srcIdx + dictSize) - match.dictIdx;
        U32 const matchLength = match.matchLength;
        sequences[nbSequences].offset = offset;
        sequences[nbSequences].litLength = litLength;
        sequences[nbSequences].matchLength = matchLength;
        nbSequences++;
    }
    return nbSequences;
}

size_t ZSTD_eDist_genSequences(ZSTD_Sequence* sequences, 
                        const void* dict, size_t dictSize,
                        const void* src, size_t srcSize)
{
    size_t const nbDiags = dictSize + srcSize + 3;
    S32* buffer = ZSTD_malloc(nbDiags * 2 * sizeof(S32), ZSTD_defaultCMem);
    ZSTD_eDist_state state;
    size_t nbSequences = 0;

    state.dict = (const BYTE*)dict;
    state.src = (const BYTE*)src;
    state.dictSize = dictSize;
    state.srcSize = srcSize;
    state.forwardDiag = buffer;
    state.backwardDiag = buffer + nbDiags;
    state.forwardDiag += srcSize + 1;
    state.backwardDiag += srcSize + 1;
    state.matches = ZSTD_malloc(srcSize * sizeof(ZSTD_eDist_match), ZSTD_defaultCMem);
    state.nbMatches = 0;

    ZSTD_eDist_compare(&state, 0, dictSize, 0, srcSize);
    ZSTD_eDist_combineMatches(&state);
    nbSequences = ZSTD_eDist_convertMatchesToSequences(sequences, &state);

    ZSTD_free(buffer, ZSTD_defaultCMem);
    ZSTD_free(state.matches, ZSTD_defaultCMem);

    return nbSequences;
}
