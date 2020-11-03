#include "Nugget.h"

timeInfo globalLimits = {0};
int move_overhead = 100;

int getCurrentTime()
{
    int now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return now;
}

timeTuple calculate_time()
{
    int movesToGo = globalLimits.movesToGo;
    int timeLeft = globalLimits.totalTimeLeft;
    int inc = globalLimits.increment;

    int optimal, maximum;
    if (movesToGo)
    {

        if (movesToGo == 1)
        {
            return {timeLeft - move_overhead, timeLeft - move_overhead};
        }
        else
        {
            optimal = timeLeft / (movesToGo + 5) + inc;
            maximum = min(optimal * 6, timeLeft/5);
        }
    }
    else
    {
        optimal = timeLeft / 50 + inc;
        maximum = min(optimal * 6, timeLeft/5);
    }

    optimal = min(optimal, timeLeft - move_overhead);
    maximum = min(maximum, timeLeft - move_overhead);

    return {optimal, maximum};
}
