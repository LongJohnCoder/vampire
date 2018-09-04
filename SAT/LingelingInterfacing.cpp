/**
* @file LingelingInterfacing.cpp
* Implements class LingelingInterfacing
* @author Ioan Dragan
*/

#include "LingelingInterfacing.hpp"

#include "Debug/Assertion.hpp"
#include "Shell/Statistics.hpp"
#include "Lib/Environment.hpp"
#include "Lib/ScopedLet.hpp"

extern "C" {
	#include "lglib.h"
}

/**
 * Just as a general hint: assuming one wants to trace all the API calls to Lingeling
 * can be done by enabling the LGLAPITRACE=filename from command line. Doing so
 * Lingeling will produce a trace file (filename) with all the calls to its API.
 *
 * e.g: LGLAPITRACE=trace ./vampire options to Vampire
 */
namespace SAT
{
  
using Shell::Statistics;
using Shell::Options;
using Lib::ScopedLet;

/*
 * Constructor for that creates an object containing the Lingeling solver based on the options
 * @opt provided as parameter
 *
 * Note that through out the entire Lingeling integration we shift the SAT variables by one. This means that as
 * soon as we get a new variable that has to be added to Lingeling we add 1 to it. We do this in order
 * to avoid the situation where we would have a variable 0 that has to be added to Lingeling. Adding 0
 * to Lingeling has the DIMACS meaning of terminating a clause.
 * TODO: investigate if the shifting by one can be removed.
 */
LingelingInterfacing::LingelingInterfacing(const Options& opts,
		bool generateProofs) :
		_varCnt(0), _status(SATISFIABLE)
{
	CALL("LingelingInterfacing::LingelingInterfacing");
  
	//here we should take care of all the options passed from the caller
	//TimeCounter ntc(TC_LINGELING);

	_solver = lglinit();	
	//for debugging
	lglsetopt(_solver, "verbose", -1);
	lglsetopt(_solver, "log", -1);
	lglsetopt(_solver, "drup", -1);
	lglsetopt(_solver, "plain", 0);
	//lglsetopt(_solver, "dlim",0);
	size_t remMem =env.options->memoryLimit() - (Allocator::getUsedMemory()/1048576);
	lglsetopt(_solver, "memlim", remMem);
	
	//set signal handler for ABORTIF
	//sig_abort_handler = signal(LING_SIG, alert_abort);
	//set the conflict limit -1 => unlimited
	//this could also be controlled by the opt
	lglsetopt(_solver, "clim", -1);

	//set the decision limit default -1 => unlimited
	lglsetopt(_solver, "dlim", -1);

	//set the propagation limit default value -1 => thousands
	lglsetopt(_solver, "plim", -1);

	//the flipping option allows us to get similar models if they exist, minimal change
	if (env.options->satLingelingSimilarModels())
	{
		lglsetopt(_solver, "flipping", 0);
	}	
}

LingelingInterfacing::~LingelingInterfacing()
{
	CALL("LingelingInterfacing::~LingelingInterfacing");
	//release the memory used by lingeling
	lglrelease(_solver);
}

/**
 * Make the solver handle clauses with variables up to @b newVarCnt-1
 * 
 * NOTE: Calling this function is not strictly necessary with lingeling
 * (adding clauses over "undeclared variables" would work),
 * but it is a way to "agree" with the caller on the used signature
 * (see, e.g., the ranges in collectZeroImplied).
 */
void LingelingInterfacing::ensureVarCount(unsigned newVarCnt)
{
  CALL("LingelingInterfacing::ensureVarCount");
     
  // lingeling starts variables from 1, so maxvar == varcount    
  while(lglmaxvar(_solver) < (int)newVarCnt) {
    // make it frozen right away
    lglincvar(_solver);
    _varCnt++;
  }
  
  ASS_EQ(newVarCnt,_varCnt);
}

unsigned LingelingInterfacing::newVar() 
{ 
  CALL("LingelingInterfacing::newVar");
  
  // just to do the same thing as ensureVarCount inside
  lglincvar(_solver);
  
  return ++_varCnt; 
}

void LingelingInterfacing::suggestPolarity(unsigned var, unsigned pol)
{
   CALL("LingelingInterfacing::suggestPolarity");
   ASS_G(var,0); ASS_LE(var,(unsigned)lglmaxvar(_solver));

   int vvar = vampireVar2Lingeling(var);
   lglsetphase(_solver,pol ? vvar : -vvar);
 }

SATSolver::Status LingelingInterfacing::solveUnderAssumptions(const SATLiteralStack& assumps, unsigned conflictCountLimit, bool)
{
  CALL("LingelingInterfacing::solveUnderAssumptions");

  ASS(!hasAssumptions());

  solveModuloAssumptionsAndSetStatus(assumps,conflictCountLimit);

  if (_status == SATSolver::UNSATISFIABLE) {
    // fill _failedAssumptionBuffer
    _failedAssumptionBuffer.reset();
    for (unsigned i = 0; i < assumps.size(); i++) {
      SATLiteral assump = assumps[i];
      int lassump = vampireLit2Lingeling(assump);
      if (lglfailed(_solver,lassump)) {
        _failedAssumptionBuffer.push(assump);
      }
    }
  }

  return _status;
}

/**
 * Solve modulo assumptions and set status. 
 */
void LingelingInterfacing::solveModuloAssumptionsAndSetStatus(const SATLiteralStack& assumps, int conflictCountLimit)
{
  CALL("LingelingInterfacing::solveModuloAssumptionsAndSetStatus");
  
  ScopedLet<Statistics::ExecutionPhase> phaseLet(env.statistics->phase,Statistics::SAT_SOLVING);  
  env.statistics->satLingelingSATCalls++;
  
  // Limit memory to what the Allocator has not used up
	size_t remMem =env.options->memoryLimit() - (Allocator::getUsedMemory()/1048576);
  lglsetopt(_solver,"memlim",remMem);  
  lglsetopt(_solver,"clim",conflictCountLimit);
  
  for (size_t i=0; i < assumps.size(); i++) {
    lglassume(_solver,vampireLit2Lingeling(assumps[i]));
  }  
  
  TimeCounter tc(TC_LINGELING);
  
  unsigned result = lglsat(_solver);  
  
	switch (result) {
    case LGL_UNKNOWN:
    _status = SATSolver::UNKNOWN;
		break;
	case LGL_SATISFIABLE:
    _status = SATSolver::SATISFIABLE;
		break;
	case LGL_UNSATISFIABLE:
    _status = SATSolver::UNSATISFIABLE;
		break;
	default:
		ASSERTION_VIOLATION;
		break;
	}
}

void LingelingInterfacing::addClause(SATClause* cl)
{
	CALL("LingelingInterfacing::addClauses");
  
  ASS_EQ(_assumptions.size(),0);

	if (_status == SATSolver::UNSATISFIABLE) {
		return;
	}

  // store to later generate the refutation
  PrimitiveProofRecordingSATSolver::addClause(cl);

  //add the statistics for Lingeling total number of clauses
	env.statistics->satLingelingClauses++;
            
  unsigned clen=cl->length();
  for(unsigned i=0;i<clen;i++) {
    SATLiteral l = (*cl)[i];

    int v = vampireVar2Lingeling(l.var());
    ASS(lglusable(_solver, v));
      
    lgladd(_solver, vampireLit2Lingeling(l));

  }
  lgladd(_solver, 0); //add the marker for clause termination

  /* TODO: reconsider implementing env.options->satLingelingIncremental()
   * or removing the option! */
}

SATSolver::Status LingelingInterfacing::solve(unsigned conflictCountLimit) 
{
  CALL("LingelingInterfacing::solve");      
  solveModuloAssumptionsAndSetStatus(_assumptions,conflictCountLimit == UINT_MAX ? -1 : (int)conflictCountLimit);
  return _status;
}
  
/*
void LingelingInterfacing::printAssignment()
{
	CALL("LingelingInterfacing::printAssignment");

  enum AsgnVal {
    //the true and false value also correspond to positive
    //and negative literal polarity values
   		AS_FALSE = 0u,
    	AS_TRUE = 1u,
    	AS_UNDEFINED = 2u
  };
  
	ASS(_status == SATSolver::SATISFIABLE);
	DArray<AsgnVal> _assignm;
	int maxVar = lglmaxvar(_solver);
	_assignm.expand(maxVar, AS_UNDEFINED);

	for (int var = 0; var < maxVar; var++)
	{
		switch (lglderef(_solver, var+1))
		{
		case -1:
			_assignm[var] = AS_FALSE;
			break;
		case 1:
			_assignm[var] = AS_TRUE;
			break;
		case 0:
			_assignm[var] = AS_UNDEFINED;
			break;
		default:
			ASSERTION_VIOLATION;
		}
	}
}
*/

//as this function is used, we only assume single units
//lingeling allows us to also assume more than units, clauses
void LingelingInterfacing::addAssumption(SATLiteral literal) {
	CALL("LingelingInterfacing::addAssumption");
	TimeCounter tc(TC_LINGELING);
	env.statistics->satLingelingAssumptions++;	
  _assumptions.push(literal);
}
  
void LingelingInterfacing::addCAssumption(SATClause* clause,
		unsigned conflictCountLimit)
{
	CALL("LingelingInterfacing::addaCAssumption");
	if (_status == SATSolver::UNSATISFIABLE){
		return;
	}

	unsigned clauseLength = clause->length();

	for (unsigned idx = 0; idx < clauseLength; idx++){
		lglcassume(_solver, vampireLit2Lingeling((*clause)[idx]));		
	}
  //add the end of lglcassume
	lglcassume(_solver, 0);
  
	//something we could do is book-keeping of what we add and which fails!
}

/**
 * get the assignment for @param var
 */
SATSolver::VarAssignment LingelingInterfacing::getAssignment(unsigned var)
{
	CALL("LingelingInterfacing::getAssignment(var)");
	ASS(_status == SATISFIABLE);

	int val = lglderef(_solver, vampireVar2Lingeling(var));
	switch (val)
	{
	case -1:
		return SATSolver::FALSE;
	case 1:
		return SATSolver::TRUE;
	case 0:
		return SATSolver::DONT_CARE;
	default:
		ASSERTION_VIOLATION;
	}
	//Added just in order to get rid of compiler warning!
	return SATSolver::NOT_KNOWN;
}

void LingelingInterfacing::retractAllAssumptions()
{
  CALL("LingelingInterfacing::retractAllAssumptions()");
  
  _assumptions.reset();
  _status = UNKNOWN;
}

bool LingelingInterfacing::hasAssumptions() const
{
  CALL("LingelingInterfacing::hasAssumptions()");
  return !_assumptions.isEmpty();
}

void LingelingInterfacing::printLingelingStatistics()
{
	CALL("LingelingInterfacing::printLingelingStatistics");
	lglstats(_solver);
	cout << "conflicts :" << lglgetconfs(_solver) << endl;
	cout << "memory MB: " << lglmb(_solver) << endl;
	cout << "memory Bytes: " << lglbytes(_solver) << endl;
	cout << "seconds : " << lglsec(_solver) << endl;
	cout << "processtime: " << lglprocesstime() << endl;
}

bool LingelingInterfacing::isZeroImplied(unsigned var)
{
  CALL("LingelingInterfacing::isZeroImplied");
  ASS_G(var,0); ASS_LE(var,(unsigned)lglmaxvar(_solver));
  
  return lglfixed(_solver, vampireVar2Lingeling(var));
}

void LingelingInterfacing::collectZeroImplied(SATLiteralStack& acc)
{
  CALL("LingelingInterfacing::collectZeroImplied");
   
  for (int v = 1; v <= lglmaxvar(_solver); v++) {        
    if (lglfixed(_solver, v)) {
      acc.push(lingelingLit2Vampire(v*lglderef(_solver,v)));
    }              
  }      
}

} //end of the SAT namespace
