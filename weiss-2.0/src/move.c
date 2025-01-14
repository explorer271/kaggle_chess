/*
  Weiss is a UCI compliant chess engine.
  Copyright (C) 2020  Terje Kirstihagen

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>

#include "bitboard.h"
#include "board.h"
#include "move.h"
#include "movegen.h"
#include "transposition.h"


// Checks whether a move is pseudo-legal (assuming it is pseudo-legal in some position)
bool MoveIsPseudoLegal(const Position *pos, const Move move) {

    if (!move) return false;

    const Color color = sideToMove;
    const Square from = fromSq(move);
    const Square to = toSq(move);

    // Must move our own piece
    if (!(colorBB(color) & BB(from)))
        return false;

    // Castling
    if (moveIsCastle(move))
        switch (to) {
            case C1: return CastlePseudoLegal(pos, WHITE, OOO);
            case G1: return CastlePseudoLegal(pos, WHITE, OO);
            case C8: return CastlePseudoLegal(pos, BLACK, OOO);
            case G8: return CastlePseudoLegal(pos, BLACK, OO);
            default: assert(0); return false;
        }

    // Must move to a square not occupied by our own pieces, and capture the piece specified
    if (   (colorBB(color) & BB(to))
        || capturing(move) != pieceOn(to))
        return false;

    // All non-pawn, non-castling moves
    if (pieceTypeOn(from) != PAWN) {

        // No flags or promotion, and the piece currently attacks it
        return !moveIsSpecial(move)
            && BB(to) & AttackBB(pieceTypeOn(from), from, pieceBB(ALL));

    // Pawn moves
    } else {

        // En passant
        if (moveIsEnPas(move))
            return to == pos->epSquare
                && BB(to) & PawnAttackBB(color, from);

        // Pawn start
        if (moveIsPStart(move))
            return pieceOn(to ^ 8) == EMPTY
                && (to + 16 - 32 * color) == from;

        // Pawn moves to the last rank must promote
        if (   RelativeRank(color, RankOf(to)) == RANK_8
            && !promotion(move))
            return false;

        // Normal moves and promotions
        return (moveIsCapture(move)) ? BB(to) & PawnAttackBB(color, from)
                                     : (to + 8 - 16 * color) == from;
    }
}

// Translates a move to a string
char *MoveToStr(const Move move) {

    static char moveStr[6];

    int ff = FileOf(fromSq(move));
    int rf = RankOf(fromSq(move));
    int ft = FileOf(toSq(move));
    int rt = RankOf(toSq(move));

    if (chess960 && moveIsCastle(move)) {
        Color color = RankOf(fromSq(move)) == RANK_1 ? WHITE : BLACK;
        int side = ft == FILE_G ? OO : OOO;
        int cr = side & (color == WHITE ? WHITE_CASTLE : BLACK_CASTLE);
        ft = FileOf(RookSquare[cr]);
        rt = RankOf(RookSquare[cr]);
    }

    PieceType promo = PieceTypeOf(promotion(move));
    char pchar = "\0.nbrq"[promo];

    sprintf(moveStr, "%c%c%c%c%c", ('a' + ff), ('1' + rf), ('a' + ft), ('1' + rt), pchar);

    return moveStr;
}

// Translates a string to a move
Move ParseMove(const char *str, const Position *pos) {

    // Translate coordinates into square numbers
    Square from = StrToSq(str);
    Square to   = StrToSq(str+2);

    Piece promo = str[4] == 'q' ? MakePiece(sideToMove, QUEEN)
                : str[4] == 'n' ? MakePiece(sideToMove, KNIGHT)
                : str[4] == 'r' ? MakePiece(sideToMove, ROOK)
                : str[4] == 'b' ? MakePiece(sideToMove, BISHOP)
                                : 0;

    PieceType pt = pieceTypeOn(from);

    int flag = pt == KING && Distance(from, to) > 1           ? FLAG_CASTLE
             : pt == PAWN && Distance(from, to) > 1           ? FLAG_PAWNSTART
             : pt == PAWN && str[0] != str[2] && !pieceOn(to) ? FLAG_ENPAS
                                                              : 0;

    if (chess960 && pt == KING && pieceOn(to) && ColorOf(pieceOn(to)) == sideToMove) {
        to = RelativeSquare(sideToMove, to > from ? G1 : C1);
        flag = FLAG_CASTLE;
    }

    return MOVE(from, to, pieceOn(to), promo, flag);
}
