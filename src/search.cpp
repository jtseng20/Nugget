#include "Nugget.h"

extern timeInfo globalLimits;
int ideal_usage, max_usage, initial_ideal_usage, startTime;
bool timeLimit = false, infinite = false; // true if the limit is by time
moveTimeManager timeman = {MOVE_NONE};

void getMyTimeLimit()
{
    infinite = globalLimits.infinite;
    STOPSEARCHING = false;
    if (globalLimits.depthlimit != MAX_PLY)
    {
        timeLimit = false;
        ideal_usage = initial_ideal_usage = max_usage = 10000;
        return;
    }

    timeTuple t = calculate_time();
    ideal_usage = initial_ideal_usage = t.optimum_time; // ideal usage is the time we'd like to take to cleanly finish a depth search
    max_usage = t.maximum_time; // max usage is the time we allocate so that maybe one last depth search may finish
    timeLimit = !infinite;
}

void printInfo(int depth, int seldepth, int score, int alpha, int beta, searchInfo *info)
{
    const char *scoreBound = score >= beta ? " lowerbound" : score <= alpha ? " upperbound" : "";
    const char * type = abs(score) >= MATE_IN_MAX_PLY ? "mate" : "cp";
    int timePassed = time_passed();
    bool exact = score > alpha && score < beta;
    if (!exact && timePassed < 3000)
        return;
    U64 nodes = sum_nodes();
    score = score <= MATED_IN_MAX_PLY ? ((VALUE_MATED - score) / 2) : score >= MATE_IN_MAX_PLY ? ((VALUE_MATE - score + 1) / 2) : score * 100 / PAWN_EG;

    printf("info depth %d seldepth %d time %d nodes %zu score %s %d%s nps %zu hashfull %d tbhits %zu pv ",
           depth, seldepth, timePassed, nodes, type, score, scoreBound, nodes*1000/(timePassed+1), hashfull(), sum_tb_hits());

    if (exact) {
        for (int i = 0; i < info->pvLen; i++)
            cout << move_to_str(info->pv[i]) <<" ";
    } else {
        cout << move_to_str(info->pv[0]);
    }
    cout << endl;
}

void printCurrMove(Move currmove, int currmovenumber)
{
    cout << "info currmove " << currmove << " currmovenumber " << currmovenumber << endl;
}

void updateTime(int depth, Move bestMove)
{
    timeman.pastPVs[depth] = bestMove;

    if (timeman.latestIsEasy && timeman.pastPVs[depth - 1] == bestMove)
    {
        // decrement time
        ideal_usage = max(ideal_usage * 95 / 100, initial_ideal_usage / 4);
        max_usage = ideal_usage * 6;
    }

    // set latestIsEasy if there are 2 repetitions in a row at the end such that a 3rd would trigger the above
    // alternatively make sure it's false if the last move is different
    timeman.latestIsEasy = (timeman.pastPVs[depth - 1] == bestMove);
}

int aspire(SearchThread *thread, searchInfo *info, int lastScore, int depth)
{
    bool isMain = is_main_thread(&thread->position);
    // consider a variable delta per thread
    int delta = ASPIRATION_DELTA + (thread->thread_id % 6), alpha = lastScore, beta = lastScore, score = lastScore;

    while (true)
    {
        alpha = max(alpha - delta, int(VALUE_MATED));
        beta = min(beta + delta, int(VALUE_MATE));
        delta += (delta == ASPIRATION_DELTA + (thread->thread_id % 6)) ? delta * 2 / 3 : delta / 2;

        /// score = alphaBeta()
        if (isMain)
            printInfo(depth, thread->seldepth + 1, score, alpha, beta, info);
        if (score <= alpha) // fails low
        {
            alpha = max(alpha - delta, int(VALUE_MATED));
            beta = (alpha + beta) / 2; // Not sure about this one; experiment
        }
        else if (score >= beta) // fails high
        {
            beta = min(beta + delta, int(VALUE_MATE));
        }
        else // just right! return
        {
            return score;
        }
    }
}

void iterative_deepening(SearchThread *thread)
{
    int score = UNDEFINED;
    Move bestMove;
    searchInfo *info = &thread->ss[3];

    for (int depth = 1; depth < globalLimits.depthlimit; depth++)
    {
        thread->seldepth = 0;
        if (setjmp(thread->jbuffer)) break;

        if (depth <= 5)
        {
            // score = alphaBeta
        }
        else
        {
            score = aspire(thread, info, score, depth);
        }

        // Main thread manages time usage based on its chosen move. Note that even though all threads get to vote for the final bestmove,
        // only main thread's iterative PV changes the time management.
        // also, if not on a time limit, we don't care about this part
        if (thread->thread_id !=0 || !timeLimit || ISPONDERING)
            continue;

        /// TODO: get bestmove
        bestMove = info->pv[0];
        updateTime(depth, bestMove);

        /// if time is up and we're past the ideal time limit, set STOPSEARCHING and break
        if (time_passed() > ideal_usage)
        {
            STOPSEARCHING = true;
            break;
        }
    }
}

/// Considers the given position and prints out UCI responses.
void get_best_move(Position *pos)
{
    /// Get the time limit we will use
    getMyTimeLimit();

    thread *threads = new thread[threadCount];
    initialize_nodes();
    increment_TT_age();
    startTime = getCurrentTime();

    for (int i = 0; i < threadCount; i++)
        threads[i] = thread(iterative_deepening, get_thread(i));

    for (int i = 0; i < threadCount; i++)
        threads[i].join();

    delete[] threads;

    while (ISPONDERING) {}

    // vote on the best move

    // print out the bestmove + ponder

    cout<<endl;
    fflush(stdout);
}

bool isDraw(Position *pos)
{
    return true;
}

inline void check_time(SearchThread *thread)
{
    if ((thread->nodes & 1024) == 1024 && time_passed() >= max_usage && !ISPONDERING && timeLimit)
        STOPSEARCHING = true;
}

int alphaBeta(SearchThread *thread, searchInfo *info, int depth, int alpha, int beta)
{
    if (depth < 1)
        return qSearch(thread, info, 0, alpha, beta);

    Position *pos = &thread->position;
    bool isPV, isRoot, inCheck, isQuiet, skipQuiets;
    int score, reduction, moveCount, newDepth, ply = info->ply;
    Move m, hashMove, bestMove;

    isPV = beta - alpha > 1;
    isRoot = ply == 0;
    inCheck = pos->checkBB;

    if (isPV)
        info->pvLen = 0;

    if (thread->thread_id == 0)
        check_time(thread);

    // Early return
    if (!isRoot)
    {
        if (STOPSEARCHING)
            longjmp(thread->jbuffer, 1);

        if (isDraw(pos))
            return 1 - (thread->nodes & 2);

        if (ply >= MAX_PLY)
            return inCheck ? 0 : evaluate(*pos);

        // Mate distance pruning
        alpha = max(VALUE_MATED + ply, alpha);
        beta = min(VALUE_MATE - ply, beta);
        if (alpha >= beta)
            return alpha;
    }

    // Hash table

    // TB

    // Static eval

    // Razoring

    // Reverse Futility

    // NMP

    // ProbCut

    // IID

    // Move loop
    MoveGen movegen = MoveGen(pos, NORMAL_SEARCH, hashMove, 0, depth);
    bestMove = MOVE_NONE;
    int bestScore = -VALUE_INF;
    while((m = movegen.next_move(info, skipQuiets)) != MOVE_NONE)
    {
        moveCount++;
        // Early / Late pruning

        // Do Move
        pos->do_move(m);
        thread->nodes++;

        // Extensions
        newDepth = depth;

        // Reductions

        // Full Search for first move
        if (isPV && moveCount == 1)
            score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha);
        else
        {
            reduction = 0;
            // do LMR


            // Reduced search
            score = -alphaBeta(thread, info+1, newDepth - reduction, -alpha-1, -alpha);

            // Null window search
            if (reduction > 0 && score > alpha)
                score = -alphaBeta(thread, info+1, newDepth, -alpha-1, -alpha);

            // Full search for first move or for any later exact scores
            if (isPV && ((score > alpha && score < beta) || moveCount == 1))
                score = -alphaBeta(thread, info+1, newDepth, -beta, -alpha);
        }

        // Undo Move
        pos->undo_move(m);

        // New best move
        if (score > bestScore)
        {
            bestScore = score;
            if (score > alpha)
            {
                // Update PV

                bestMove = m;
                if (isPV && score < beta)
                    alpha = score;
                else
                    break;
            }
        }
    }

    // Stalemate or mate?
    if (!moveCount)
        return inCheck ? VALUE_MATED + ply : 0;

    // Update all history scores

    // Save Hash entry

    return bestScore;
}
