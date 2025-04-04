#ifndef SEARCH_ANALYSISDATA_H_
#define SEARCH_ANALYSISDATA_H_

#include "../game/board.h"
#include "../game/boardhistory.h"

struct SearchNode;

struct AnalysisData {
  //Utilities and scores should all be from white's perspective
  Loc move;
  int64_t numVisits;
  double playSelectionValue; //Similar units to visits, but might have LCB adjustments
  double lcb; //In units of utility
  double radius; //In units of utility
  double utility; //From -1 to 1 or -1.25 to -1.25 or other similar bounds, depending on score utility
  double resultUtility; //Utility from winloss result
  double winLossValue;   // From -1 to 1
  double noResultValue;  // From 0 to 1
  double policyPrior; //From 0 to 1
  double ess; //Effective sample size taking into account weighting, could be somewhat smaller than visits
  double weightFactor; //Due to child value weighting
  double weightSum; //Internal value that is used instead of visits for everything
  double weightSqSum; //Sum of squares
  double utilitySqAvg;
  int64_t childVisits; //Visits of the underlying child node
  double childWeightSum; //Weight sum of the underlying child node
  int order; //Preference order of the moves, 0 is best
  Loc isSymmetryOf; //If not Board::NULL_LOC, this move is a duplicate analysis data reflected from isSymmetryOf
  int symmetry; //The symmetry applied to isSymmetryOf to get move, or 0.
  std::vector<Loc> pv;
  std::vector<int64_t> pvVisits;
  std::vector<int64_t> pvEdgeVisits;

  const SearchNode* node; //ONLY valid so long as search is not cleared

  AnalysisData();
  AnalysisData(const AnalysisData& other);
  AnalysisData(AnalysisData&& other) noexcept;
  ~AnalysisData();

  AnalysisData& operator=(const AnalysisData& other);
  AnalysisData& operator=(AnalysisData&& other) noexcept;

  bool pvContainsPass() const;
  void writePV(std::ostream& out, const Board& board) const;
  void writePVVisits(std::ostream& out) const;
  void writePVEdgeVisits(std::ostream& out) const;
  void writePVUpToPhaseEnd(std::ostream& out, const Board& initialBoard, const BoardHistory& initialHist, Player initialPla) const;
  void writePVVisitsUpToPhaseEnd(std::ostream& out, const Board& initialBoard, const BoardHistory& initialHist, Player initialPla) const;
  void writePVEdgeVisitsUpToPhaseEnd(std::ostream& out, const Board& initialBoard, const BoardHistory& initialHist, Player initialPla) const;
  int getPVLenUpToPhaseEnd(const Board& initialBoard, const BoardHistory& initialHist, Player initialPla) const;
};

bool operator<(const AnalysisData& a0, const AnalysisData& a1);


#endif  // SEARCH_ANALYSISDATA_H_
