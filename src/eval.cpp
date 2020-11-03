#include "Nugget.h"

/*
    Set up stuff that's needed for later evaluation
*/
void Eval::prepare_info()
{
    // Attacked squares, double attacked squares
    double_targets[0] = double_targets[1] = 0ULL;
    king_ring[WHITE] = kingRing[pos.kingpos[WHITE]];
    king_ring[BLACK] = kingRing[pos.kingpos[BLACK]];
    U64 real_occ = pos.occupiedBB[0] | pos.occupiedBB[1];

    attackedSquares[WPAWN] = PAWNATTACKS(WHITE, pos.pieceBB[WPAWN]);
    attackedSquares[BPAWN] = PAWNATTACKS(BLACK, pos.pieceBB[BPAWN]);
    attackedSquares[WKING] = PseudoAttacks[KING][pos.kingpos[WHITE]];
    attackedSquares[BKING] = PseudoAttacks[KING][pos.kingpos[BLACK]];

    double_targets[WHITE] = ((pos.pieceBB[WPAWN] & ~FileABB) << 7) & ((pos.pieceBB[WPAWN] & ~FileHBB) << 9);
    double_targets[BLACK] = ((pos.pieceBB[BPAWN] & ~FileHBB) >> 7) & ((pos.pieceBB[BPAWN] & ~FileABB) >> 9);
    double_targets[WHITE] |= attackedSquares[WKING] & attackedSquares[WPAWN];
    double_targets[BLACK] |= attackedSquares[BKING] & attackedSquares[BPAWN];

    attackedSquares[WHITE] = attackedSquares[WKING] | attackedSquares[WPAWN];
    attackedSquares[BLACK] = attackedSquares[BKING] | attackedSquares[BPAWN];

    //prepare mobility area
    U64 low_ranks = Rank2BB | Rank3BB;
    U64 blocked_pawns = pos.pieceBB[WPAWN] & (PAWNPUSH(BLACK, real_occ) | low_ranks);
    mobility_area[WHITE] = ~(blocked_pawns | pos.pieceBB[WKING] | attackedSquares[BPAWN]);

    low_ranks = Rank6BB | Rank7BB;
    blocked_pawns = pos.pieceBB[BPAWN] & (PAWNPUSH(WHITE, real_occ) | low_ranks);
    mobility_area[BLACK] = ~(blocked_pawns | pos.pieceBB[BKING] | attackedSquares[WPAWN]);
}

template <Color side> Score Eval::evaluate_pawn_structure()
{
    constexpr Color opponent = ~side;
    U64 pawns, tempPawns, opponentPawns;
    bool doubled, phalanx, isolated, edge, backward, opposed, supported;
    int sq, fwd1;
    uint8_t yesmarker, nomarker;

    pawns = tempPawns = pos.pieceBB[WPAWN + side];
    opponentPawns = pos.pieceBB[WPAWN + opponent];

    Score out = S(0,0);

    pawntte->passedPawns[side] = 0ULL;
    pawntte->semiopenFiles[side] = 0xFF;
    pawntte->doubledFiles[side] = 0;
    pawntte->defendedFiles[side] = 0;

    while (tempPawns)
    {
        sq = popLsb(&tempPawns);
        fwd1 = PAWNPUSHINDEX(side, sq);

        doubled = pawns & pawnBlockerMasks[side][sq]; // my own pawn in front of me
        isolated = !(pawns & neighborMasks[sq]); // none of my own pawns in neighbor files
        phalanx = pawns & phalanxMasks[sq]; // pawns directly next to me
        opposed = opponentPawns & pawnBlockerMasks[side][sq]; // opponent pawn in front of me
        backward = !(pawns & passedPawnMasks[opponent][sq]) && ((opponentPawns & BITSET(fwd1)) || (PAWN_ATTACKS[side][fwd1] & opponentPawns)); // no pawns behind me and front square is attacked or occupied
        edge = isEdge[sq];
        supported = PAWN_ATTACKS[opponent][sq] & pawns;

        if (doubled)
        {
            out -= (supported) ? doubled_penalty[opposed] : doubled_penalty_undefended[opposed];
        }

        if (isolated)
        {
            if (doubled)
                out -= edge ? isolated_doubled_penaltyAH[opposed] : isolated_doubled_penalty[opposed];
            out -= edge ? isolated_penaltyAH[opposed] : isolated_penalty[opposed];
        }

        if (backward)
        {
            out -= backward_penalty[opposed];
        }

        if (supported)
        {
            out += supported_bonus[opposed][RRANK(sq, side)];
        }

        if (phalanx)
        {
            out += phalanx_bonus[opposed][RRANK(sq, side)];
        }

        if (!(passedPawnMasks[side][sq] & opponentPawns))
            pawntte->passedPawns[side] |= BITSET(sq);

        yesmarker = (1 << FILE(sq));
        nomarker = ~(yesmarker);
        // tell the semiopen files tracker that this file is NOT semiopen (because there's a pawn here)
        pawntte->semiopenFiles[side] &= nomarker;

        // update the doubled files tracker if doubled. If NOT doubled, then this pawn is the foremost pawn, so check if it's defended.
        if (doubled)
            pawntte->doubledFiles[side] |= yesmarker;
        else if (supported)
            pawntte->defendedFiles[side] |= yesmarker;

    }
    return out;
}

void Eval::evaluate_pawns()
{
    pawntte = get_pawntte(pos);
    if (pawntte->pawn_hash == pos.pawnhash) // if pawn hash matches, then all good
        return;

    // no match, so now we gotta fill out the scores, and get the net score
    pawntte->pawn_hash = pos.pawnhash;
    pawntte->scores[WHITE] = evaluate_pawn_structure<WHITE>();
    pawntte->scores[BLACK] = evaluate_pawn_structure<BLACK>();
    pawntte->netScore = Net.getScore(&pos) * NETSCALEFACTOR;
}

template <Color side, PieceType type> Score Eval::evaluate_piece()
{
    constexpr PieceCode pc = make_piece(side, type);
    constexpr Color opponent = ~side;
    Score out = S(0,0);
    int sq;
    uint8_t fileType;
    U64 attacks;

    // Calculate the attacked squares and mobilities
    U64 pieces = pos.pieceBB[make_piece(side, type)];
    while (pieces)
    {
        sq = popLsb(&pieces);
        attacks = (type == BISHOP) ? pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]) ^ pos.pieceBB[WQUEEN] ^ pos.pieceBB[BQUEEN])
            : (type == ROOK) ? pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]) ^ pos.pieceBB[WQUEEN] ^ pos.pieceBB[BQUEEN] ^ pos.pieceBB[pc])
            : pos.getAttackSet(sq, (pos.occupiedBB[0] | pos.occupiedBB[1]));
        if ((pos.blockersForKing[side][0] | pos.blockersForKing[side][1]) & BITSET(sq))
            attacks &= RAY_MASKS[sq][pos.kingpos[side]];

        double_targets[side] |= attackedSquares[side] & attacks;
        attackedSquares[side] |= attackedSquares[pc] |= attacks;

        if (type == BISHOP)
        {
            out += bishopMobilityBonus[POPCOUNT(attacks & mobility_area[side])];
        }
        else if (type == KNIGHT)
        {
            out += knightMobilityBonus[POPCOUNT(attacks & mobility_area[side])];
        }
        else if (type == ROOK)
        {
            out += rookMobilityBonus[POPCOUNT(attacks & mobility_area[side])];

            if (pawntte->semiopenFiles[side] & (1 << FILE(sq))) // semiopen file
            {
                if (pawntte->defendedFiles[opponent] & (1 << FILE(sq))) // supported file
                    fileType = 1;
                else if (pawntte->doubledFiles[opponent] & (1 << FILE(sq))) // weak file
                    fileType = 2;
                else if (pawntte->semiopenFiles[opponent] & (1 << FILE(sq))) // open file
                    fileType = 3;
                else
                    fileType = 0;

                out += rookFile[fileType];
            }
        }
        else if (type == QUEEN)
        {
            out += queenMobilityBonus[POPCOUNT(attacks & mobility_area[side])];
        }
    }

    return out;
}

template <Color side> Score Eval::evaluate_passers()
{
    constexpr Color opponent = ~side;
    constexpr int Up = side == WHITE ? NORTH : SOUTH;
    U64 passers, tempPassers, occ, myRooks, opponentRooks;
    passers = tempPassers = pawntte->passedPawns[side];
    int sq, r;
    bool unsafe, blocked, safePath, clearPath;
    occ = pos.occupiedBB[0] | pos.occupiedBB[1];
    myRooks = pos.pieceBB[make_piece(side, ROOK)];
    opponentRooks = pos.pieceBB[make_piece(opponent, ROOK)];
    Score out = S(0,0);

    while(tempPassers)
    {
        sq = popLsb(&tempPassers);
        r = RRANK(sq, side);
        unsafe = SQUARE_MASKS[sq+Up] & attackedSquares[opponent];
        blocked = SQUARE_MASKS[sq+Up] & occ;
        safePath = !((pawnBlockerMasks[side][sq] & attackedSquares[opponent]));
        clearPath = !((pawnBlockerMasks[side][sq] & occ));

        out += passedUnsafeBonus[unsafe][r];
        out += passedBlockedBonus[blocked][r];
        out += passedRankBonus[r];
        if (connectedMasks[sq] & passers)
            out += passedConnectedBonus[r];

        if (safePath)
            out += passedSafePath[r];
        if (clearPath)
            out += passedClearPath[r];

        out += passedFriendlyDistance[r] * squareDistance[pos.kingpos[side]][sq + Up];
        out += passedEnemyDistance[r] * squareDistance[pos.kingpos[opponent]][sq + Up];

        if (r >= 3)
        {
            if (myRooks & pawnBlockerMasks[opponent][sq])
            {
                out += tarraschRule_friendly[r];
            }

            if (opponentRooks & pawnBlockerMasks[opponent][sq])
            {
                out -= tarraschRule_enemy;
            }
        }
    }

    return out;
}

template <Color side> Score Eval::kingSafetyScore()
{
    constexpr Color opponent = ~side;
    Score out = S(0,0);
    int danger = 0, sq = pos.kingpos[side];
    U64 occ = pos.occupiedBB[WHITE] | pos.occupiedBB[BLACK];

    /// Score how open the king is on diagonals and ranks/files

    U64 attacks = bishopAttacks(occ, sq);
    out -= diagTropism * POPCOUNT(attacks);
    attacks = rookAttacks(occ, sq);
    out -= rookTropism * POPCOUNT(attacks);

    /// Adjust for a vulnerable back rank
    if (RRANK(sq, side) == 0)
        out -= backRank * POPCOUNT(pawntte->semiopenFiles[side] & pawntte->semiopenFiles[opponent]);

    /// Score castling rights and pawn shield

    out += pawnShieldBonus * POPCOUNT(king_ring[side] & pos.pieceBB[make_piece(side, PAWN)]);

    // If on home square award a bonus for being able to castle away
    if (FILE(sq) == 4)
        out += canCastle * bool(pos.castleRights & (side == WHITE ? WHITECASTLE : BLACKCASTLE));

    if (king_attackers_count[side] > 1)
    {
        U64 weak = attackedSquares[opponent]
            & ~double_targets[side]
            & (~attackedSquares[side] | attackedSquares[make_piece(side, KING)] | attackedSquares[make_piece(side, QUEEN)]);

        U64 pinned = pos.blockersForKing[side][0] | pos.blockersForKing[side][1];
        danger =    kingDangerBase +
                    king_attackers_weight[side] * king_attackers_count[side] -
                    king_defenders_weight[side] * king_defenders_count[side] +
                    king_attacks_count[side] * kingAttack +
                    bool(pinned & pos.occupiedBB[side]) * kingPin +
                    bool(pinned & pos.occupiedBB[opponent]) * discoveredCheck +
                    POPCOUNT(weak & king_ring[side]) * kingWeak -
                    !pos.pieceCount[make_piece(opponent, QUEEN)] * noQueen -
                    bool(phalanxMasks[sq] & ~occ) * kingMobilityWeight;

        /// Score safe / unsafe checks

        U64 safe = ~pos.occupiedBB[opponent] & (~attackedSquares[side] | (weak & double_targets[opponent]));

        U64 rookSquares = rookAttacks(occ, sq);
        U64 bishopSquares = bishopAttacks(occ, sq);
        U64 knightSquares = PseudoAttacks[KNIGHT][sq];

        U64 queenChecks, rookChecks, bishopChecks, knightChecks;

        /// Rook checks

        rookChecks = ((rookSquares) & attackedSquares[make_piece(opponent, ROOK)]);
        if (rookChecks)
        {
            danger += (rookChecks & safe) ? safeChecks[ROOK] : unsafeChecks[ROOK];

            if (rookChecks & attackedSquares[make_piece(side, KING)] & double_targets[opponent] & weak)
                danger += contactChecks[0];
        }

        /// Queen checks

        queenChecks = ((rookSquares | bishopSquares)
            & attackedSquares[make_piece(opponent, QUEEN)]
            & ~attackedSquares[make_piece(side, QUEEN)]
            & ~rookChecks);

        if (queenChecks)
        {
            if (queenChecks & ~attackedSquares[make_piece(side, KING)])
                danger += (queenChecks & safe) ? safeChecks[QUEEN] : unsafeChecks[QUEEN];

            if (queenChecks & attackedSquares[make_piece(side, KING)] & double_targets[opponent] & weak)
                danger += contactChecks[1];
        }

        /// Bishop checks

        bishopChecks = ((bishopSquares) & attackedSquares[make_piece(opponent, BISHOP)] & ~queenChecks);

        if (bishopChecks)
        {
            danger += (bishopChecks & safe) ? safeChecks[BISHOP] : unsafeChecks[BISHOP];
        }

        /// Knight checks

        knightChecks = ((knightSquares) & attackedSquares[make_piece(opponent, KNIGHT)]);

        if (knightChecks)
        {
            danger += (knightChecks & safe) ? safeChecks[KNIGHT] : unsafeChecks[KNIGHT];
        }

        if (danger > 0)
            out -= S(danger * danger / 4096, danger / 20);
    }

    return out;
}

Score Eval::evaluate_pairs()
{
    Score out = S(0,0);
    if (pos.pieceCount[WBISHOP] > 1)
        out += bishopPair;
    if (pos.pieceCount[WKNIGHT] > 1)
        out += knightPair;
    if (pos.pieceCount[WROOK] > 1)
        out += rookPair;

    if (pos.pieceCount[BBISHOP] > 1)
        out -= bishopPair;
    if (pos.pieceCount[BKNIGHT] > 1)
        out -= knightPair;
    if (pos.pieceCount[BROOK] > 1)
        out -= rookPair;

    uint8_t closedness = pos.pieceCount[WPAWN] + pos.pieceCount[BPAWN] + POPCOUNT((pos.pieceBB[WPAWN] | pos.pieceBB[BPAWN]) & centerSquare);

    out += knightPawn * (closedness - 16) * (pos.pieceCount[WKNIGHT] - pos.pieceCount[BKNIGHT]);
    out += rookPawn * (closedness - 16) * (pos.pieceCount[WROOK] - pos.pieceCount[BROOK]);

    return out;
}

int Position::scaleFactor() const
{
    // Opposite color bishops, with just pawns or with other pieces
    if (ONLYONE(pieceBB[WBISHOP]) && ONLYONE(pieceBB[BBISHOP]) && ONLYONE((pieceBB[WBISHOP] | pieceBB[BBISHOP]) & DarkSquares))
    {
        if (nonPawn[0] == BISHOP_MG && nonPawn[1] == BISHOP_MG)
            return 16;
        else
            return 27;
    }

    Color strongerSide = (eg_value(psqt_score) > 0) ? WHITE : BLACK;

    // Scaling for situations with just pieces for the attacking side
    if (!pieceBB[make_piece(strongerSide, PAWN)])
    {
        // Should also catch Minor vs pawn and scale to 0
        if (nonPawn[strongerSide] <= nonPawn[~strongerSide] + BISHOP_MG) // If stronger side has a small edge it's basically a draw
            return nonPawn[strongerSide] < ROOK_MG ? 0 : 3; // If there is a lot of firepower, then it's not quite a draw
    }

    return 32;
}

int Eval::value()
{
    pawntte = get_pawntte(pos);
    evaluate_pawns();

    Score out = pos.psqt_score;

    out += pawntte->scores[WHITE] - pawntte->scores[BLACK];
    out += S(pawntte->netScore,0);

#ifndef __TUNE__
    if (abs((mg_value(out) + eg_value(out)) / 2) >= lazyThreshold + (pos.nonPawn[0] + pos.nonPawn[1]) / 64)
        goto return_flag;
#endif

    prepare_info();

    out += evaluate_pairs();

    out += evaluate_piece<WHITE, KNIGHT>() - evaluate_piece<BLACK, KNIGHT>() +
        evaluate_piece<WHITE, BISHOP>() - evaluate_piece<BLACK, BISHOP>() +
        evaluate_piece<WHITE, ROOK>() - evaluate_piece<BLACK, ROOK>() +
        evaluate_piece<WHITE, QUEEN>() - evaluate_piece<BLACK, QUEEN>();

    out += evaluate_passers<WHITE>() - evaluate_passers<BLACK>() +
        kingSafetyScore<WHITE>() - kingSafetyScore<BLACK>();

#ifndef __TUNE__
    return_flag :
#endif

    int phase = ((24 - (pos.pieceCount[WBISHOP] + pos.pieceCount[BBISHOP] + pos.pieceCount[WKNIGHT] + pos.pieceCount[BKNIGHT])
        - 2 * (pos.pieceCount[WROOK] + pos.pieceCount[BROOK])
        - 4 * (pos.pieceCount[WQUEEN] + pos.pieceCount[BQUEEN])) * 256) / 24;

    int v = ((mg_value(out) * (256 - phase) + eg_value(out) * phase * pos.scaleFactor() / 32) / 256);

    v = ((v)*S2MSIGN(pos.activeSide) + tempo);

    return v;
}

int evaluate(const Position& pos)
{
    return Eval(pos).value();
}
