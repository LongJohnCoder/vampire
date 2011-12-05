/**
 * @file EPRRestoringScanner.hpp
 * Defines class EPRRestoringScanner.
 */

#ifndef __EPRRestoringScanner__
#define __EPRRestoringScanner__

#include "Forwards.hpp"

#include "Shell/Options.hpp"


namespace VUtils {

using namespace Shell;

class EPRRestoringScanner {
public:
  int perform(int argc, char** argv);
private:
  enum EprResult {
    MADE_EPR_WITH_RESTORING = 0,
    CANNOT_MAKE_EPR = 1,
    EASY_EPR = 2,
    FORM_NON_EPR = 3,
    UNDEF
  };

  Options _opts;

  void countClauses(Problem& prb, unsigned& allClauseCnt, unsigned& nonEprClauseCnt);
  unsigned countDefinitions(Problem& prb);

  void computeEprResults(Problem& prb);
  unsigned _baseClauseCnt;
  unsigned _baseNonEPRClauseCnt;
  unsigned _erClauseCnt;
  unsigned _erNonEPRClauseCnt;
  EprResult _eprRes;

  void computeInliningResults(Problem& prb);
  unsigned _predDefCnt;
  unsigned _predDefsNonGrowing;
  unsigned _predDefsMerged;
  unsigned _predDefsAfterNGAndMerge;
  unsigned _ngmClauseCnt;
  unsigned _ngmNonEPRClauseCnt;


  void reportResults();
};

}

#endif // __EPRRestoringScanner__