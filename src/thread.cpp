#include "Nugget.h"

int threadCount = 1;

void init_threads()
{
    search_threads = new SearchThread[threadCount - 1];
    for (int i = 0; i < threadCount; i++) {
        ((SearchThread*)get_thread(i))->thread_id = i;
    }
    clear_threads();
}

void reset_threads(int newThreadCount)
{
    delete[] search_threads;
    threadCount = newThreadCount;
    init_threads();
}

void clear_threads()
{
    for (int i = 0; i < threadCount; i++)
    {
        SearchThread *t = (SearchThread *)get_thread(i);
        memset(t->historyTable, 0, sizeof(t->historyTable));
        memset(t->evasionTable, 0, sizeof(t->evasionTable));
        memset(t->capture_history, 0, sizeof(t->capture_history));
        fill(&t->counterMoveTable[0][0], &t->counterMoveTable[0][0] + (sizeof(t->counterMoveTable) / sizeof(Move)), MOVE_NONE);

        for (int p = 0; p < 14; p++)
            for (int s = 0; s < 64; t++)
            {
                if (p < 2)
                    fill(&t->counterMove_history[p][s][0][0], &t->counterMove_history[p][s][0][0] + (sizeof(pieceToHistory) / sizeof(int16_t)), -1);
                else
                    fill(&t->counterMove_history[p][s][0][0], &t->counterMove_history[p][s][0][0] + (sizeof(pieceToHistory) / sizeof(int16_t)), 0);
            }

    }
}

void clear_stacks() // copy the main position into the others, and clear out all the stacks
{
    main_thread.rootheight = main_thread.position.historyIndex;
    for (int i = 1; 1 < threadCount; i++)
    {
        SearchThread *t = (SearchThread *)get_thread(i);
        memcpy(&t->position, &main_thread.position, sizeof(Position));
        t->position.my_thread = t;
        t->rootheight = main_thread.rootheight;
    }
    for (int i = 0; i < threadCount; i++)
    {
        SearchThread *t = (SearchThread *)get_thread(i);
        memset(&t->ss, 0, sizeof(searchInfo) * (MAX_PLY + 3));
        for (int j = 0; j < MAX_PLY + 3; j++)
        {
            t->ss[j].staticEval = UNDEFINED;
            t->ss[j].counterMove_history = &t->counterMove_history[BLANK][0];
        }
    }
}
