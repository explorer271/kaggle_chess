/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <bitset>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "material.h"
#include "movepick.h"
#include "pawns.h"
#include "position.h"
#include "search.h"

const int MAX_THREADS = 128;
const int MAX_SPLITPOINTS_PER_THREAD = 8;

struct Thread;

struct SplitPoint {

  // Const data after split point has been setup
  const Position* pos;
  const Search::Stack* ss;
  Thread* masterThread;
  Depth depth;
  Value beta;
  int nodeType;
  bool cutNode;

  // Const pointers to shared data
  MovePicker* movePicker;
  SplitPoint* parentSplitPoint;

  // Shared data
  std::mutex mutex;
  std::bitset<MAX_THREADS> slavesMask;
  volatile bool allSlavesSearching;
  volatile uint64_t nodes;
  volatile Value alpha;
  volatile Value bestValue;
  volatile Move bestMove;
  volatile int moveCount;
  volatile bool cutoff;
};


/// ThreadBase struct is the base of the hierarchy from where we derive all the
/// specialized thread classes.

struct ThreadBase {

  ThreadBase() : exit(false) {}
  virtual ~ThreadBase() {}
  virtual void idle_loop() = 0;
  void notify_one();
  void wait_for(volatile const bool& b);

  std::thread nativeThread;
  std::mutex mutex;
  std::condition_variable sleepCondition;
  volatile bool exit;
};


/// Thread struct keeps together all the thread related stuff like locks, state
/// and especially split points. We also use per-thread pawn and material hash
/// tables so that once we get a pointer to an entry its life time is unlimited
/// and we don't have to care about someone changing the entry under our feet.

struct Thread : public ThreadBase {

  Thread();
  virtual void idle_loop();
  bool cutoff_occurred() const;
  bool available_to(const Thread* master) const;

  template <bool Fake>
  void split(Position& pos, const Search::Stack* ss, Value alpha, Value beta, Value* bestValue, Move* bestMove,
             Depth depth, int moveCount, MovePicker* movePicker, int nodeType, bool cutNode);

  SplitPoint splitPoints[MAX_SPLITPOINTS_PER_THREAD];
  Material::Table materialTable;
  //Endgames endgames;
  Pawns::Table pawnsTable;
  Position* activePosition;
  size_t idx;
  int maxPly;
  SplitPoint* volatile activeSplitPoint;
  volatile int splitPointsSize;
  volatile bool searching;
};


/// MainThread and TimerThread are derived classes used to characterize the two
/// special threads: the main one and the recurring timer.

struct MainThread : public Thread {
  MainThread() : thinking(true) {} // Avoid a race with start_thinking()
  virtual void idle_loop();
  volatile bool thinking;
};

struct TimerThread : public ThreadBase {
  TimerThread() : run(false) {}
  virtual void idle_loop();
  bool run;
  static const int Resolution = 5; // msec between two check_time() calls
};


/// ThreadPool struct handles all the threads related stuff like init, starting,
/// parking and, most importantly, launching a slave thread at a split point.
/// All the access to shared thread data is done through this class.

struct ThreadPool : public std::vector<Thread*> {

  void init(); // No c'tor and d'tor, threads rely on globals that should
  void exit(); // be initialized and are valid during the whole thread lifetime.

  MainThread* main() { return static_cast<MainThread*>((*this)[0]); }
  void read_uci_options();
  Thread* available_slave(const Thread* master) const;
  void wait_for_think_finished();
  void start_thinking(const Position&, const Search::LimitsType&, Search::StateStackPtr&);

  Depth minimumSplitDepth;
  std::mutex mutex;
  std::condition_variable sleepCondition;
  TimerThread* timer;
};

extern ThreadPool Threads;

#endif // #ifndef THREAD_H_INCLUDED
