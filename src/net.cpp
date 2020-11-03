#include "Nugget.h"

kingNet Net;

void kingNet::loadWeights(string path)
{
    ifstream file(path);
    string str;
    int currentIndex = 0;
    float *myPointer;
    while (getline(file, str))
    {
        stringstream linestream(str);
        string data;
        int numTerms, term;
        linestream >> numTerms;

        if (currentIndex < NETINPUTS)
            myPointer = &inputWeights[currentIndex][0];
        else if (currentIndex < NETINPUTS + NETHIDDEN)
            myPointer = &inputBiases[0];
        else if (currentIndex < NETINPUTS + 2*NETHIDDEN)
            myPointer = &hiddenWeights[0];
        else
            myPointer = &hiddenBias;

        for (int i = 0; i < numTerms; i++)
        {
            linestream >> term;
            *(myPointer++) = term;
        }
        currentIndex++;
    }
}

int kingNet::getScore(const Position *p)
{
    float out = hiddenBias;
    U64 WP = p->pieceBB[WPAWN];
    U64 BP = p->pieceBB[BPAWN];
    int b;

    memcpy(&cache, &inputBiases, sizeof(float)*NETHIDDEN);

    calculateLine(p->kingpos[WHITE]);
    calculateLine(p->kingpos[BLACK]);
    while (WP)
    {
        b = popLsb(&WP);
        calculateLine(120+b);
    }
    while (BP)
    {
        b = popLsb(&BP);
        calculateLine(168+b);
    }

    for (int i = 0; i < NETHIDDEN; i++)
    {
        if ( cache[i] > 0)
            out += cache[i] * hiddenWeights[i];
    }

    return p->activeSide ? -round(out) : round(out);
}

void kingNet::calculateLine(int inputIndex)
{
    for (int i = 0; i < NETHIDDEN; i++)
    {
        cache[i] += inputWeights[inputIndex][i];
    }
}
