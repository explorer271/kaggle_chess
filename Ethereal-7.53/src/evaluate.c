#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "castle.h"
#include "movegen.h"
#include "magics.h"
#include "masks.h"
#include "types.h"
#include "bitboards.h"
#include "bitutils.h"
#include "transposition.h"
#include "evaluate.h"
#include "piece.h"

int evaluateBoard(Board * board, PawnTable * ptable){
    
    int mid = 0, end = 0;
    int curPhase, midEval, endEval, eval;
    
    int pawnCount   = 0;
    int knightCount = 0;
    int bishopCount = 0;
    int rookCount   = 0;
    
    // EXTRACT BASIC BITBOARDS FROM BOARD
    uint64_t white    = board->colourBitBoards[ColourWhite];
    uint64_t black    = board->colourBitBoards[ColourBlack];
    uint64_t pawns    = board->pieceBitBoards[0];
    uint64_t knights  = board->pieceBitBoards[1];
    uint64_t bishops  = board->pieceBitBoards[2];
    uint64_t rooks    = board->pieceBitBoards[3];
    uint64_t queens   = board->pieceBitBoards[4];
    
    // CHECK FOR RECOGNIZED DRAWS
    if (pawns == 0 && rooks == 0 && queens == 0){
        
        // K v K
        if (board->numPieces == 2)
            return 0;
        
        // K+B v K and K+N v K
        if (countSetBits(white & (knights | bishops)) <= 1 && 
            countSetBits(black) == 1)
            return 0;
        else if (countSetBits(black & (knights | bishops)) <= 1 && 
            countSetBits(white) == 1)
            return 0;
         
        // K+B+N v K 
        /* Pawel Koziol pointed out that this should not be here! Will add
            a PSQT table to find the mates 
        if (countSetBits(black) == 1 &&
            countSetBits(white & bishops) == 1 &&
            countSetBits(white & knights) == 1)
            return 0;
        else if (countSetBits(white) == 1 &&
            countSetBits(black & bishops) == 1 &&
            countSetBits(black & knights) == 1)
            return 0;
        */
        
        // K+N+N v K
        if (countSetBits(black) == 1 &&
            countSetBits(white & knights) == 2 &&
            countSetBits(white & bishops) == 0)
            return 0;
        else if (countSetBits(white) == 1 &&
            countSetBits(black & knights) == 2 &&
            countSetBits(black & bishops) == 0)
            return 0;
    }
    
    
    // ATTEMPT TO USE PREVIOUS PAWN EVALUATION
    PawnEntry * pentry = getPawnEntry(ptable, board->phash);
    
    // PAWN ENTRY FOUND
    if (pentry != NULL){
        mid = pentry->mg;
        end = pentry->eg;
    } 
    
    // NO ENTRY FOUND, EVALUATE AND STORE
    else {
        evaluatePawns(&mid, &end, board, &pawnCount);
        storePawnEntry(ptable, board->phash, mid, end);
    }
    
    evaluateKnights(&mid, &end, board, &knightCount);
    evaluateBishops(&mid, &end, board, &bishopCount);
    evaluateRooks(&mid, &end, board, &rookCount);
    evaluateKings(&mid, &end, board);
    
    midEval = board->opening + mid;
    endEval = board->endgame + end;
    
    midEval += (board->turn == ColourWhite) ? 5 : -5;
    endEval += (board->turn == ColourWhite) ? 7 : -7;
    
    curPhase = 24 - (1 * (knightCount + bishopCount))
                  - (2 * rookCount)
                  - (4 * countSetBits(queens));
    curPhase = (curPhase * 256 + 12) / 24;
    
    eval = ((midEval * (256 - curPhase)) + (endEval * curPhase)) / 256;
    
    return board->turn == ColourWhite ? eval : -eval;
}

void evaluatePawns(int* mid, int* end, Board* board, int * pawnCount){
    
    uint64_t allPawns = board->pieceBitBoards[0];
    uint64_t myPawns, enemyPawns;
    
    int mg = 0, eg = 0;
    int colour, i, sq, file, rank;
    
    for (colour = ColourBlack; colour >= ColourWhite; colour--){
        
        mg = -mg;
        eg = -eg;
        
        myPawns = allPawns & board->colourBitBoards[colour];
        uint64_t allMyPawns = myPawns;
        enemyPawns = allPawns ^ myPawns;
        
        for (i = 0; myPawns != 0; i++){
            
            (*pawnCount)++;
            
            sq = getLSB(myPawns);
            myPawns ^= (1ull << sq);
            
            file = sq % 8;
            rank = (colour == ColourBlack) ? 7 - (sq / 8) : sq / 8;
            
            if (!(PassedPawnMasks[colour][sq] & enemyPawns)){
                mg += PawnPassedMid[rank];
                eg += PawnPassedEnd[rank];
            }
            
            if (!(IsolatedPawnMasks[sq] & myPawns)){
                mg -= PAWN_ISOLATED_MID;
                eg -= PAWN_ISOLATED_END;
            }
            
            else if (FILES[file] & myPawns){
                mg -= PAWN_STACKED_MID;
                eg -= PAWN_STACKED_END;
            }
            
            if (PawnConnectedMasks[colour][sq] & allMyPawns){
                mg += PawnConnected[colour][sq];
                eg += PawnConnected[colour][sq];
            }
        }
    }
    
    *mid += mg;
    *mid += eg;
}

void evaluateKnights(int* mid, int*end, Board* board, int * knightCount){
    
    uint64_t allPawns = board->pieceBitBoards[0];
    uint64_t allKnights = board->pieceBitBoards[1];
    
    uint64_t myPawns, enemyPawns, myKnights, defenders;
    
    int mg = 0, eg = 0;
    int colour, i, sq, outpostValue;
    
    for (colour = ColourBlack; colour >= ColourWhite; colour--){
        
        mg = -mg;
        eg = -eg;
        
        myKnights = allKnights & board->colourBitBoards[colour];
        myPawns = allPawns & board->colourBitBoards[colour];
        enemyPawns = allPawns ^ myPawns;
        
        for (i = 0; myKnights != 0; i++){
            
            (*knightCount)++;
            
            sq = getLSB(myKnights);
            myKnights ^= (1ull << sq);
            
            outpostValue = KnightOutpost[colour][sq];
            
            if (outpostValue != 0){
            
                if (!(OutpostSquareMasks[colour][sq] & enemyPawns)){
                    
                    defenders = PawnAttackMasks[colour][sq] & myPawns;
                    
                    if (defenders){
                        
                        if (PawnAdvanceMasks[colour][sq] & enemyPawns)
                            outpostValue *= 2;
                    }
                    
                    mg += outpostValue / 2;
                    eg += outpostValue / 2;
                }
            }
        }
    }
    
    *mid += mg;
    *end += eg;
}

void evaluateBishops(int* mid, int* end, Board* board, int * bishopCount){
    
    uint64_t allPawns = board->pieceBitBoards[0];
    uint64_t allBishops = board->pieceBitBoards[2];
    
    uint64_t myPawns, enemyPawns, myBishops, defenders;
    
    int mg = 0, eg = 0;
    int colour, i, sq, outpostValue;
    
    for (colour = ColourBlack; colour >= ColourWhite; colour--){
        
        mg = -mg;
        eg = -eg;
        
        myBishops = allBishops & board->colourBitBoards[colour];
        myPawns = allPawns & board->colourBitBoards[colour];
        enemyPawns = allPawns ^ myPawns;
        
        if (myBishops != 0
            && (myPawns & (FILE_A|FILE_B|FILE_C))
            && (myPawns & (FILE_F|FILE_G|FILE_H))){
                
            mg += BISHOP_HAS_WINGS_MID;
            eg += BISHOP_HAS_WINGS_END;
        }
        
        for (i = 0; myBishops != 0; i++){
            
            (*bishopCount)++;
            
            sq = getLSB(myBishops);
            myBishops ^= (1ull << sq);
            
            if (i == 1){
                mg += BISHOP_PAIR_MID;
                eg += BISHOP_PAIR_END;
            }
            
            outpostValue = BishopOutpost[colour][sq];
            
            if (outpostValue != 0){
            
                if (!(OutpostSquareMasks[colour][sq] & enemyPawns)){
                    
                    defenders = PawnAttackMasks[colour][sq] & myPawns;
                    
                    if (defenders){
                        
                        if (PawnAdvanceMasks[colour][sq] & enemyPawns)
                            outpostValue *= 2;
                        
                        if (defenders & (defenders-1))
                            outpostValue *= 0;
                    }
                    
                    mg += outpostValue / 2;
                    eg += outpostValue / 2;
                }
            }
        }
    }
    
    *mid += mg;
    *end += eg;
}

void evaluateRooks(int* mid, int* end, Board* board, int * rookCount){
    
    uint64_t allPawns = board->pieceBitBoards[0];
    uint64_t allRooks = board->pieceBitBoards[3];
    
    uint64_t myPawns, enemyPawns, myRooks;
    
    int mg = 0, eg = 0;
    int colour, i, sq;
    
    for (colour = ColourBlack; colour >= ColourWhite; colour--){
        
        mg = -mg;
        eg = -eg;
        
        myRooks = allRooks & board->colourBitBoards[colour];
        
        for (i = 0; myRooks != 0; i++){
            
            (*rookCount)++;
            
            sq = getLSB(myRooks);
            myRooks ^= (1ull << sq);
            
            myPawns = allPawns & board->colourBitBoards[colour];
            enemyPawns = allPawns ^ myPawns;
            
            if (!(myPawns & FILES[sq % 8])){
                
                if (!(enemyPawns & FILES[sq % 8])){
                    mg += ROOK_OPEN_FILE_MID;
                    eg += ROOK_OPEN_FILE_END;
                }
                
                else {
                    mg += ROOK_SEMI_FILE_MID;
                    eg += ROOK_SEMI_FILE_END;
                }
            }
            
            if (FILES[sq % 8] & myRooks){
                mg += ROOK_STACKED_MID;
                eg += ROOK_STACKED_END;
            }
        }
    }
    
    *mid += mg;
    *end += eg;
}

void evaluateKings(int* mid, int* end, Board* board){
    
    int mg = 0, eg = 0;
    int colour, options;
    
    for (colour = ColourBlack; colour >= ColourWhite; colour--){
        mg = -mg;
        eg = -eg;
        
        if (board->hasCastled[colour]){
            mg += KING_HAS_CASTLED;
            eg += KING_HAS_CASTLED;
        }
        
        else {
            
            options = 0;
            if (board->castleRights & (1 << (2*colour)))
                options++;
            if (board->castleRights & (2 << (2*colour)))
                options++;
            
            if (options == 2){
                mg += 1.2 * KING_CAN_CASTLE;
                eg += 1.2 * KING_CAN_CASTLE;
            }
            
            else if (options == 1){
                mg += KING_CAN_CASTLE;
                eg += KING_CAN_CASTLE;
            }
        }
    }
    
    *mid += mg;
    *end += eg;
}