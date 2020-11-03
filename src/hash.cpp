#include "Nugget.h"

transpositionTable TT;

void init_tt()
{
    size_t MB = TRANSPOSITION_MB;

    TT.table_size = MB *1024 * 1024;
    TT.size_mask = uint64_t(TT.table_size / sizeof(TTBucket) - 1);
    TT.table = (TTBucket *)aligned_ttmem_alloc(TT.table_size, TT.mem);
    clear_tt();
}

void reset_tt(int MB)
{
    free(TT.mem);

    // Round MB to nearest power of 2
    if (!ONLYONE(MB))
    {
        MB = (1 << MSB(MB));
    }

    TT.table_size = (uint64_t)MB * 1024 * 1024;
    TT.size_mask = uint64_t(TT.table_size / sizeof(TTBucket) - 1);
    TT.table = (TTBucket *)(aligned_ttmem_alloc(TT.table_size, TT.mem));
    if (!TT.mem)
    {
        cerr << "Failed to allocate " << MB << "MB for transposition table." << endl;
        exit(EXIT_FAILURE);
    }

    clear_tt();
}

void clear_tt()
{
    memset(TT.table, 0, TT.table_size);
}

void storeEntry(TTEntry *entry, U64 key, Move m, int depth, int score, int staticEval, uint8_t flag)
{
    uint16_t upper = (uint16_t)(key >> 48);
    if (m || upper != entry->hashupper) // update the move even if the hash is the same
        entry->movecode = m;
    if (upper != entry->hashupper || depth > entry->depth) // otherwise update only if it's a different hash or if the depth is better
    {
        entry->hashupper = upper;
        entry->depth = (int8_t)depth;
        entry->flags = (TT.generation << 2) | flag;
        entry->static_eval = (int16_t)staticEval;
        entry->value = (int16_t)score;
    }
}

TTEntry *probeTT(U64 key, bool &ttHit)
{
    U64 index = key & TT.size_mask;
    TTBucket *bucket = &TT.table[index];
    uint16_t upper = (uint16_t)(key >> 48);
    for (int i = 0; i < 3; i++)
    {
        ///entry found
        if (bucket->entries[i].hashupper == upper)
        {
            ttHit = true;
            return &bucket->entries[i];
        }

        ///blank entry found, this key has never been used
        if (!bucket->entries[i].hashupper)
        {
            ttHit = false;
            return &bucket->entries[i];
        }
    }

    ///no matching entry found + no empty entries found, return the least valuable entry
    TTEntry *cheapest = &bucket->entries[0];
    for (int i = 1; i< 3; i++)
    {
        // bucket "value" = depth - 4 * age difference; pick lowest value
        if ((bucket->entries[i].depth - (TT.generation - (bucket->entries[i].flags & 0x3F))) < (cheapest->depth - (TT.generation - (cheapest->flags & 0x3F))))
            cheapest = &bucket->entries[i];
    }
    ttHit = false;
    return cheapest;
}

zobrist::zobrist()
{
    PRNG rng(1070372);
    int i;
    int j = 0;

    U64 c[4];
    U64 ep[8];
    for (i = 0; i < 4; i++)
        c[i] = rng.rand();
    for (i = 0; i < 8; i++)
        ep[i] = rng.rand();
    for (i = 0; i < 16; i++)
    {
        castle[i] = 0ULL;
        for (j = 0; j < 4; j++)
        {
            if (i & (1 << (j)))
                castle[i] ^= c[j];
        }
    }
    for (i = 0; i < 64; i++)
    {
        epSquares[i] = 0ULL;
        if (RANK(i) == 2 || RANK(i) == 5)
            epSquares[i] = ep[FILE(i)];
    }

    for (i = 0; i < 64*16; i++)
        pieceKeys[i] = rng.rand();

    activeSide = rng.rand();
}

U64 zobrist::getHash(Position *pos)
{
	U64 out = 0ULL;
	for (int i = WPAWN; i<=BKING;i++)
	{
		U64 pieces = pos->pieceBB[i];
		unsigned int index;
		while (pieces)
		{
			index = popLsb(&pieces);
			out ^= pieceKeys[(index << 4) | i];
		}
	}

	if (pos->activeSide)
		out ^= activeSide;
	out ^= castle[pos->castleRights];
	out ^= epSquares[pos->epSquare];
	return out;
}

U64 zobrist::getPawnHash(Position *pos)
{
	U64 out = 0ULL;
	for (int i = WPAWN; i<=BPAWN; i++)
	{
		U64 pawns = pos->pieceBB[i];
		unsigned int index;
		while (pawns)
		{
			index = popLsb(&pawns);
			out ^= pieceKeys[(index << 4) | i];
		}
	}

	out ^= pieceKeys[(pos->kingpos[0] << 4) | WKING] ^ pieceKeys[(pos->kingpos[1] << 4) | BKING];
	return out;
}
