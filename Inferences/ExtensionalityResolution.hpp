/**
 * @file ExtensionalityResolution.hpp
 * Defines class ExtensionalityResolution
 *
 */

#ifndef __ExtensionalityResolution__
#define __ExtensionalityResolution__

#include "Forwards.hpp"

#include "Saturation/ExtensionalityClauseContainer.hpp"

#include "InferenceEngine.hpp"

namespace Inferences
{

using namespace Kernel;

class ExtensionalityResolution
: public GeneratingInferenceEngine
{
public:
  CLASS_NAME(ExtensionalityResolution);
  USE_ALLOCATOR(ExtensionalityResolution);

  ExtensionalityResolution() {}
  
  ClauseIterator generateClauses(Clause* premise);

  static Clause* performExtensionalityResolution(
    Clause* extCl, Literal* extLit,
    Clause* otherCl, Literal* otherLit,
    RobSubstitution* subst,
    unsigned& counter);
private:
  struct ForwardPairingFn;
  struct ForwardUnificationsFn;
  struct ForwardResultFn;

  struct NegEqSortFn;
  struct BackwardPairingFn;
  struct BackwardUnificationsFn;
  struct BackwardResultFn;
};

};

#endif /*__ExtensionalityResolution__*/