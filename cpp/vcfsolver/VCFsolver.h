#pragma once
#include <iostream>
#include <vector>
#include "VCFHashTable.h"
#include "../core/hash.h"
#include "../core/global.h"
#include "../game/board.h"
#include "../game/rules.h"


//#define NOVCF

using namespace std;

class VCFsolver
{
public:
  static const int sz = Board::MAX_LEN;
  static const uint8_t C_EM = 0; //
  static const uint8_t C_MY = 1; //
  static const uint8_t C_OPP = 2; //

  static uint64_t MAXNODE ;

  static Hash128 zob_board[2][sz][sz];//pla-1,y,x
  static const Hash128 zob_plaWhite;
  static const Hash128 zob_plaBlack;

  //hashTable
  static VCFHashTable hashtable;

  //RULE
  Rules rules;
  uint8_t forbiddenSide; //

  //board
  int xsize, ysize;
  uint8_t rootboard[sz][sz]; //board[y][x]
  uint8_t board[sz][sz]; //board[y][x]
  int32_t movenum; //
  Hash128 boardhash;
  int64_t oppFourPos; //

  //

  uint8_t mystonecount[4][sz][sz];//
  uint8_t oppstonecount[4][sz][sz];//
  //uint8_t mystonecount1[ysize][xsize - 4];//x
  //uint8_t mystonecount2[ysize - 4][xsize];//y
  //uint8_t mystonecount3[ysize - 4][xsize - 4];//+x+y
  //uint8_t mystonecount4[ysize - 4][xsize - 4];//+x-y
  vector<int64_t> threes;
  //mystonecount[t][y][x]¦(t*sz*sz+y*sz+x)<<32|pos1<<16|pos2,   pos=y*sz+x
  //
  uint64_t threeCount;//

  uint64_t nodenum;

  //result
  int32_t rootresultpos;
  int32_t bestmovenum;//
  //result:   0 
  //resultpos:   pos=y*sz+x
  //hashtable result=(resultpos<<32)|result

  static uint64_t totalAborted;
  static uint64_t totalSolved;
  static uint64_t totalnodenum;

  static void init();
  VCFsolver(const Rules rules):rules(rules){ threes.resize(4 * sz * sz); }
  void solve(const Board& kataboard, uint8_t pla,uint8_t& res,uint16_t& loc);
  void print();
  void printRoot();
  static void run(const Board& board,const Rules& rules, uint8_t pla, uint8_t& res, uint16_t& loc)
  {
#ifndef NOVCF
    VCFsolver solver(rules);
    solver.solve(board, pla, res, loc);
#else
    res = 2;
    loc = Board::NULL_LOC;
#endif
  }

//private:
public:
  int32_t setBoard(const Board& board, uint8_t pla);//


  uint32_t findEmptyPos(int t, int y, int x);// pos1<<16|pos2
  uint32_t findDefendPosOfFive(int y, int x);//

//For Renju and Standard
  void addNeighborSix(int y, int x, uint8_t pla, int factor);//factor undo


  int32_t solveIter(bool isRoot);//
  //
  int32_t play(int x, int y, uint8_t pla,bool updateHash);
  void undo(int x, int y, int64_t oppFourPos, uint64_t threeCount, bool updateHash);

  //For Renju
  bool isForbiddenMove(int y, int x,bool fiveForbidden = false);
  bool checkLife3(int y, int x, int t);//
  void printForbiddenMap();
};


