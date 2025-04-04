#include "../search/reportedsearchvalues.h"

#include "../neuralnet/nninputs.h"
#include "../search/search.h"

ReportedSearchValues::ReportedSearchValues()
{}
ReportedSearchValues::~ReportedSearchValues()
{}
ReportedSearchValues::ReportedSearchValues(
  const Search& search,
  double winLossValueAvg,
  double noResultValueAvg,
  double utilityAvg,
  double totalWeight,
  int64_t totalVisits
) {
  (void)search;
  winLossValue = winLossValueAvg;
  noResultValue = noResultValueAvg;
  utility = utilityAvg;

  //Clamp. Due to tiny floating point errors, these could be outside range.
  if(winLossValue < -1.0) winLossValue = -1.0;
  if(winLossValue > 1.0) winLossValue = 1.0;
  if(noResultValue < 0.0) noResultValue = 0.0;
  if(noResultValue > 1.0-std::fabs(winLossValue)) noResultValue = 1.0-std::fabs(winLossValue);

  winValue = 0.5 * (winLossValue + (1.0 - noResultValue));
  lossValue = 0.5 * (-winLossValue + (1.0 - noResultValue));

  //Handle float imprecision
  if(winValue < 0.0) winValue = 0.0;
  if(winValue > 1.0) winValue = 1.0;
  if(lossValue < 0.0) lossValue = 0.0;
  if(lossValue > 1.0) lossValue = 1.0;

  weight = totalWeight;
  visits = totalVisits;
}

std::ostream& operator<<(std::ostream& out, const ReportedSearchValues& values) {
  out << "winValue " << values.winValue << "\n";
  out << "lossValue " << values.lossValue << "\n";
  out << "noResultValue " << values.noResultValue << "\n";
  out << "winLossValue " << values.winLossValue << "\n";
  out << "utility " << values.utility << "\n";
  out << "weight " << values.weight << "\n";
  out << "visits " << values.visits << "\n";
  return out;
}
