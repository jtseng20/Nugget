/*
  Nugget is a UCI-compliant chess engine.
  Copyright (C) 2020 Jonathan Tseng.

  Nugget is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Nugget is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Nugget.h"

vector<string> args;
Position *root_position = &main_thread.position;
volatile bool ISPONDERING = false;
volatile bool STOPSEARCHING = false;
int depthLimit = MAX_PLY;
extern timeInfo globalLimits;
Position globalPosition;

bool word_equal(int index, string comparison_str) {
    if (args.size() > (unsigned) index)
        return args[index] == comparison_str;
    return false;
}

vector<string> split_words(string s) {
    vector<string> out;
    istringstream iss(s);
    for(string t; iss >> t; )
        out.push_back(t);

    return out;
}

Move uci2Move(Position *pos, string s)
{
    int from = ( (s[0] - 'a') + 8*(s[1] - '1') );
    int to = ( (s[2] - 'a') + 8*(s[3] - '1') );
    PieceCode pc = pos->mailbox[from];
    SpecialType type = NORMAL;

    if (pc == WPAWN || pc == BPAWN)
    {
        if (RANK(to) == 0 || RANK(to) == 7)
            type = PROMOTION;
        else if (to == pos->epSquare)
            type = ENPASSANT;
    }
    else if (pc >= WKING && (abs(from - to) == 2 || (BITSET(to) & pos->pieceBB[make_piece(pos->activeSide, ROOK)])))
    {
        if (abs(from - to) == 2)
            to = (to > from) ? to + 1 : to - 2; // convert to king-captures-rook
        type = CASTLING;
    }

    return makeMove(from, to, type);
}

void setPosition(string fen, int movesindex)
{
    globalPosition.readFEN(fen.c_str());

    if (word_equal(movesindex, "moves"))
    {
        for (unsigned i = movesindex + 1 ; i < args.size() ; i++) {
            Move m = MOVE_NONE;
            if (args[i].length() == 4 || args[i].length() == 5)
            {
                m = uci2Move(&globalPosition, args[i]);
                if (args[i].length() == 5)
                {
                    PieceType promote = GetPieceType(args[i][4]);
                    m = Move(m | ((promote - KNIGHT) << 12));
                }
            }
            if (m != MOVE_NONE && globalPosition.isPseudoLegal(m)) {
                globalPosition.do_move(m);
                if (globalPosition.halfmoveClock == 0)
                    globalPosition.historyIndex = 0;
            }
        }
    }
}

void prepareThreads()
{
    memcpy(&main_thread.position, &globalPosition, sizeof(Position));
    main_thread.position.my_thread = &main_thread;
    clear_stacks();
}

void ucinewgame()
{
    clear_threads();
    clear_tt();
}

template<bool Root>
U64 Perft(Position& pos, int depth) {
    U64 cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<ALL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m);
            cnt = leaf ? MoveList<ALL>(pos).size() : Perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }
        if (Root)
            cout << m.toString()<<": "<< m.code<<" : "<<cnt <<endl;
    }
    return nodes;
}

void perft()
{
    prepareThreads();
    U64 nodes = Perft <true> (*root_position, stoi(args[1]));
    cout << "Nodes searched: "<<nodes<<endl;
}

void cmd_position() {
    string fen;
    int movesIndex = 0;
    if (args[1] == "fen")
    {
        for (movesIndex = 2;movesIndex <=7;movesIndex++)
        {
            if (args[movesIndex] == "moves")
                break;
            fen += args[movesIndex] + " ";
        }
    }

    else if (args[1] == "startpos")
    {
        fen = STARTFEN;
        movesIndex = 3;
    }

    setPosition(fen, movesIndex);
}

void option(string name, string value) {
    if (name == "Hash")
    {
        int mb = stoi(value);
        reset_tt(mb);
    }
    else if (name == "Threads")
    {
        reset_threads(std::min(MAX_THREADS, std::max(1, stoi(value))));
    }
    else if (name == "MoveOverhead")
    {
        move_overhead = stoi(value);
    }
    else if (name == "ClearHash")
    {
        clear_tt();
    }
}

void setoption()
{
    if ( args[1] != "name" || args[3] != "value")
        return;

    string name = args[2];
    string value = args[4];
    option(name, value);
}

void setoption_fast()
{
    string name = args[1];
    string value = args[2];
    option(name, value);
}

void debug()
{
    prepareThreads();
    cout << *root_position << endl;
    searchInfo *info = &main_thread.ss[3];
    MoveGen movegen = MoveGen(root_position, NORMAL_SEARCH, MOVE_NONE, 0, 0);
    Move m;
    cout << "Move ordering: " << endl;
    while ((m = movegen.next_move(info, 0)) != MOVE_NONE)
    {
        cout << move_to_str(m) << " ";
    }
    cout << endl;
}

void uci() {
    cout << "id name "<< NAME << " " << VERSION << endl << "id author " << AUTHOR << endl;
    cout << "option name Hash type spin default "<< TRANSPOSITION_MB <<" min 1 max 65536" << endl;
    cout << "option name ClearHash type button" << endl;
    cout << "option name Threads type spin default 1 min 1 max " << MAX_THREADS << endl;
    cout << "option name MoveOverhead type spin default 100 min 0 max 5000" << endl;
    cout << "option name Ponder type check default false" << endl;
    cout << "uciok" << endl;
}

void go() {
    prepareThreads();
    int depth = MAX_PLY;
    bool infinite = false, timelimited = false;
    int wtime = 0, btime = 0, movetime = 0;
    int winc = 0, binc = 0, movestogo = 0;
    depthLimit = MAX_PLY;
    memset(&globalLimits, 0, sizeof(timeInfo));

    if (args.size() <= 1) {
        globalLimits.movesToGo = 0;
        globalLimits.totalTimeLeft = 10000;
        globalLimits.increment = 0;
        globalLimits.movetime = 0;
        globalLimits.depthlimit = depth;
        globalLimits.timelimited = true;
        globalLimits.infinite = false;
    }

    else {

        for (unsigned i = 1; i < args.size(); ++i) {

            if (args[i] == "wtime")
            {
                wtime = stoi(args[i + 1]);
            }
            else if (args[i] == "btime")
            {
                btime = stoi(args[i + 1]);
            }
            else if (args[i] == "winc")
            {
                winc = stoi(args[i + 1]);
            }
            else if (args[i] == "binc")
            {
                binc = stoi(args[i + 1]);
            }
            else if (args[i] == "movestogo")
            {
                movestogo = stoi(args[i + 1]);
            }
            else if (args[i] == "depth")
            {
                depth = stoi(args[i + 1]);
            }

            else if (args[i] == "ponder")
            {
                ISPONDERING = true;
            }
            else if (args[i] == "infinite")
            {
                infinite = true;
            }
            else if (args[i] == "movetime")
            {
                movetime = stoi(args[i + 1]) * 99 / 100;
                timelimited = true;
            }
        }

    globalLimits.movesToGo = movestogo;
    globalLimits.totalTimeLeft = root_position->activeSide == WHITE ? wtime : btime;
    globalLimits.increment = root_position->activeSide == WHITE ? winc : binc;
    globalLimits.movetime = movetime;
    globalLimits.depthlimit = depth;
    globalLimits.timelimited = timelimited;
    globalLimits.infinite = infinite;
    }

    std::thread think_thread(get_best_move, root_position);
    think_thread.detach();
}

void loop()
{
    cout << NAME << " "<< VERSION <<" by "<< AUTHOR <<endl;
#ifdef USE_POPCNT
    cout << "Using POPCOUNT" << endl;
#endif
    string input;

    globalPosition.readFEN(STARTFEN);
    prepareThreads();

    while (true)
    {
        getline(cin, input);
        args = split_words(input);
        if (args.size() > 0)
        {
            if (args[0] == "ucinewgame")
                ucinewgame();
            if (args[0] == "position")
                cmd_position();
            if (args[0] == "go")
                go();
            if (args[0] == "setoption")
                setoption();
            if (args[0] == "set")
                setoption_fast();
            if (args[0] == "isready")
                cout << "readyok" << endl;
            if (args[0] == "uci")
                uci();
            if (args[0] == "perft")
                perft();
            if (args[0] == "debug")
                debug();
            if (args[0] == "quit")
                break;
            if (args[0] == "stop")
            {
                STOPSEARCHING = true;
                ISPONDERING = false;
            }
            if (args[0] == "ponderhit")
                ISPONDERING = false;
        }
    }
}
