
/*
 * File InferenceStore.cpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 *
 * In summary, you are allowed to use Vampire for non-commercial
 * purposes but not allowed to distribute, modify, copy, create derivatives,
 * or use in competitions. 
 * For other uses of Vampire please contact developers for a different
 * licence, which we will make an effort to provide. 
 */
/**
 * @file InferenceStore.cpp
 * Implements class InferenceStore.
 */

#include "Lib/Allocator.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/ScopedPtr.hpp"
#include "Lib/SharedSet.hpp"
#include "Lib/Stack.hpp"
#include "Lib/StringUtils.hpp"
#include "Lib/ScopedPtr.hpp"
#include "Lib/Sort.hpp"

#include "Shell/LaTeX.hpp"
#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/UIHelper.hpp"

#include "Parse/TPTP.hpp"

#include "Saturation/Splitter.hpp"

#include "Signature.hpp"
#include "Clause.hpp"
#include "Formula.hpp"
#include "FormulaUnit.hpp"
#include "FormulaVarIterator.hpp"
#include "Inference.hpp"
#include "Term.hpp"
#include "TermIterators.hpp"
#include "SortHelper.hpp"

#include "InferenceStore.hpp"

//TODO: when we delete clause, we should also delete all its records from the inference store

namespace Kernel
{

using namespace std;
using namespace Lib;
using namespace Shell;

void InferenceStore::FullInference::increasePremiseRefCounters()
{
  CALL("InferenceStore::FullInference::increasePremiseRefCounters");

  for(unsigned i=0;i<premCnt;i++) {
    if (premises[i]->isClause()) {
      premises[i]->incRefCnt();
    }
  }
}



InferenceStore::InferenceStore()
{
}

vstring InferenceStore::getUnitIdStr(Unit* cs)
{
  CALL("InferenceStore::getUnitIdStr");

  if (!cs->isClause()) {
    return Int::toString(cs->number());
  }
  return Int::toString(cs->number());
}

/**
 * Records informations needed for outputting proofs of general splitting
 */
void InferenceStore::recordSplittingNameLiteral(Unit* us, Literal* lit)
{
  CALL("InferenceStore::recordSplittingNameLiteral");

  //each clause is result of a splitting only once
  ALWAYS(_splittingNameLiterals.insert(us, lit));
}


/**
 * Record the introduction of a new symbol
 */
void InferenceStore::recordIntroducedSymbol(Unit* u, bool func, unsigned number)
{
  CALL("InferenceStore::recordIntroducedSymbol");

  SymbolStack* pStack;
  _introducedSymbols.getValuePtr(u->number(),pStack);
  pStack->push(SymbolId(func,number));
}

/**
 * Record the introduction of a split name
 */
void InferenceStore::recordIntroducedSplitName(Unit* u, vstring name)
{
  CALL("InferenceStore::recordIntroducedSplitName");
  ALWAYS(_introducedSplitNames.insert(u->number(),name));
}

/**
 * Get the parents of unit represented by us and fill in the rule used to generate this unit
 *
 * This will first check if the unit was generated by a special inference that was
 * recorded in the InferenceStore and if not, use the inference stored in the unit itself
 */
UnitIterator InferenceStore::getParents(Unit* us, Inference::Rule& rule)
{
  CALL("InferenceStore::getParents/2");
  ASS_NEQ(us,0);

  // The unit itself stores the inference
  UnitList* res = 0;
  Inference* inf = us->inference();

  // opportunity to shrink the premise list
  // (currently applies if this was a SAT-based inference
  // and the solver didn't provide a proper proof nor a core)
  inf->minimizePremises();

  Inference::Iterator iit = inf->iterator();
  while(inf->hasNext(iit)) {
    Unit* premUnit = inf->next(iit);
    UnitList::push(premUnit, res);
  }
  rule = inf->rule();
  res = UnitList::reverse(res); //we want items in the same order
  return pvi(UnitList::DestructiveIterator(res));
}

/**
 * Get parents where we do not care about the generating rule
 */
UnitIterator InferenceStore::getParents(Unit* us)
{
  CALL("InferenceStore::getParents/1");

  Inference::Rule aux;
  return getParents(us, aux);
}

/**
 * Return @b inner quantified over variables in @b vars
 *
 * It is caller's responsibility to ensure that variables in @b vars are unique.
 */
template<typename VarContainer>
vstring getQuantifiedStr(const VarContainer& vars, vstring inner, DHMap<unsigned,unsigned>& t_map, bool innerParentheses=true){
  CALL("getQuantifiedStr(VarContainer, vstring, map)");

  VirtualIterator<unsigned> vit=pvi( getContentIterator(vars) );
  vstring varStr;
  bool first=true;
  while(vit.hasNext()) {
    unsigned var =vit.next();
    if (!first) {
      varStr+=",";
    }
    vstring ty="";
    unsigned t;
    if(t_map.find(var,t) && t!=Sorts::SRT_DEFAULT){
      //TODO should assert that we are in tff mode here
      ty=":" + env.sorts->sortName(t);
    }
    varStr+=vstring("X")+Int::toString(var)+ty;
    first=false;
  }

  if (first) {
    //we didn't quantify any variable
    return inner;
  }

  if (innerParentheses) {
    return "( ! ["+varStr+"] : ("+inner+") )";
  }
  else {
    return "( ! ["+varStr+"] : "+inner+" )";
  }
}

/**
 * Return @b inner quentified over variables in @b vars
 *
 * It is caller's responsibility to ensure that variables in @b vars are unique.
 */
template<typename VarContainer>
vstring getQuantifiedStr(const VarContainer& vars, vstring inner, bool innerParentheses=true)
{
  CALL("getQuantifiedStr(VarContainer, vstring)");
  static DHMap<unsigned,unsigned> d;
  return getQuantifiedStr(vars,inner,d,innerParentheses);
}

/**
 * Return vstring containing quantified unit @b u.
 */
vstring getQuantifiedStr(Unit* u, List<unsigned>* nonQuantified=0)
{
  CALL("getQuantifiedStr(Unit*...)");

  Set<unsigned> vars;
  vstring res;
  DHMap<unsigned,unsigned> t_map;
  SortHelper::collectVariableSorts(u,t_map);
  if (u->isClause()) {
    Clause* cl=static_cast<Clause*>(u);
    unsigned clen=cl->length();
    for(unsigned i=0;i<clen;i++) {
      TermVarIterator vit( (*cl)[i] );
      while(vit.hasNext()) {
	unsigned var=vit.next();
	if (List<unsigned>::member(var, nonQuantified)) {
	  continue;
	}
	vars.insert(var);
      }
    }
    res=cl->literalsOnlyToString();
  } else {
    Formula* formula=static_cast<FormulaUnit*>(u)->formula();
    FormulaVarIterator fvit( formula );
    while(fvit.hasNext()) {
      unsigned var=fvit.next();
      if (List<unsigned>::member(var, nonQuantified)) {
        continue;
      }
      vars.insert(var);
    }
    res=formula->toString();
  }

  return getQuantifiedStr(vars, res, t_map);
}

struct UnitNumberComparator
{
  static Comparison compare(Unit* u1, Unit* u2)
  {
    return Int::compare(u1->number(), u2->number());
  }
};

struct InferenceStore::ProofPrinter
{
  CLASS_NAME(InferenceStore::ProofPrinter);
  USE_ALLOCATOR(InferenceStore::ProofPrinter);
  
  ProofPrinter(ostream& out, InferenceStore* is)
  : _is(is), out(out)
  {
    CALL("InferenceStore::ProofPrinter::ProofPrinter");

    outputAxiomNames=env.options->outputAxiomNames();
    delayPrinting=true;
    proofExtra=env.options->proofExtra()!=Options::ProofExtra::OFF;
  }

  void scheduleForPrinting(Unit* us)
  {
    CALL("InferenceStore::ProofPrinter::scheduleForPrinting");

    outKernel.push(us);
    handledKernel.insert(us);
  }

  virtual ~ProofPrinter() {}

  virtual void print()
  {
    CALL("InferenceStore::ProofPrinter::print");

    while(outKernel.isNonEmpty()) {
      Unit* cs=outKernel.pop();
      handleStep(cs);
    }
    if(delayPrinting) printDelayed();
  }

protected:

  virtual bool hideProofStep(Inference::Rule rule)
  {
    return false;
  }

  void requestProofStep(Unit* prem)
  {
    if (!handledKernel.contains(prem)) {
      handledKernel.insert(prem);
      outKernel.push(prem);
    }
  }

  virtual void printStep(Unit* cs)
  {
    CALL("InferenceStore::ProofPrinter::printStep");

    Inference::Rule rule;
    UnitIterator parents=_is->getParents(cs, rule);

    if(rule == Inference::INDUCTION){
      //cout << "ping" << endl;
      env.statistics->inductionInProof++;
    }

    out << _is->getUnitIdStr(cs) << ". ";
    if (cs->isClause()) {
      Clause* cl=cs->asClause();

      if (env.colorUsed) {
        out << " C" << cl->color() << " ";
      }

      out << cl->literalsOnlyToString() << " ";
      if (cl->splits() && !cl->splits()->isEmpty()) {
        out << "<- {" << cl->splits()->toString() << "} ";
      }
      if(proofExtra){
        out << "("<<cl->age()<<':'<<cl->weight();
        if (cl->numSelected()>0) {
          out<< ':'<< cl->numSelected();
        }
        out<<") ";
      }
      if(cl->isTheoryDescendant()){
        out << "(TD) ";
      }
      if(cl->inductionDepth()>0){
        out << "(I " << cl->inductionDepth() << ") ";
      }
    }
    else {
      FormulaUnit* fu=static_cast<FormulaUnit*>(cs);
      if (env.colorUsed && fu->inheritedColor() != COLOR_INVALID) {
        out << " IC" << fu->inheritedColor() << " ";
      }
      out << fu->formula()->toString() << ' ';
    }

    out <<"["<<Inference::ruleName(rule);

    if (outputAxiomNames && rule==Inference::INPUT) {
      ASS(!parents.hasNext()); //input clauses don't have parents
      vstring name;
      if (Parse::TPTP::findAxiomName(cs, name)) {
	out << " " << name;
      }
    }

    bool first=true;
    while(parents.hasNext()) {
      Unit* prem=parents.next();
      out << (first ? ' ' : ',');
      out << _is->getUnitIdStr(prem);
      first=false;
    }

    // print Extra
    vstring extra = cs->inference()->extra(); 
    if(extra != ""){
      out << ", " << extra;
    }
    out << "]" << endl;
  }

  void handleStep(Unit* cs)
  {
    CALL("InferenceStore::ProofPrinter::handleStep");
    Inference::Rule rule;
    UnitIterator parents=_is->getParents(cs, rule);

    while(parents.hasNext()) {
      Unit* prem=parents.next();
      ASS(prem!=cs);
      requestProofStep(prem);
    }

    if (!hideProofStep(rule)) {
      if(delayPrinting) delayed.push(cs);
      else printStep(cs);
    }
  }

  void printDelayed()
  {
    CALL("InferenceStore::ProofPrinter::printDelayed");

    // Sort
    sort<UnitNumberComparator>(delayed.begin(),delayed.end());

    // Print
    for(unsigned i=0;i<delayed.size();i++){
      printStep(delayed[i]);
    }

  }



  Stack<Unit*> outKernel;
  Set<Unit*> handledKernel; // use UnitSpec to provide its own hash and equals
  Stack<Unit*> delayed;

  InferenceStore* _is;
  ostream& out;

  bool outputAxiomNames;
  bool delayPrinting;
  bool proofExtra;
};

struct InferenceStore::ProofPropertyPrinter
: public InferenceStore::ProofPrinter
{
  CLASS_NAME(InferenceStore::ProofPropertyPrinter);
  USE_ALLOCATOR(InferenceStore::ProofPropertyPrinter);

  ProofPropertyPrinter(ostream& out, InferenceStore* is) : ProofPrinter(out,is)
  {
    CALL("InferenceStore::ProofPropertyPrinter::ProofPropertyPrinter");

    max_theory_clause_depth = 0;
    for(unsigned i=0;i<11;i++){ buckets.push(0); }
    last_one = false;
  }

  void print()
  {
    ProofPrinter::print();
    for(unsigned i=0;i<11;i++){ out << buckets[i] << " ";}
    out << endl;
    if(last_one){ out << "yes" << endl; }
    else{ out << "no" << endl; }
  }

protected:

  void printStep(Unit* us)
  {
    static unsigned lastP = Unit::getLastParsingNumber();
    static float chunk = lastP / 10.0;
    if(us->number() <= lastP){
      if(us->number() == lastP){ 
        last_one = true;
      }
      unsigned bucket = (unsigned)(us->number() / chunk);
      buckets[bucket]++;
    }

    // TODO we could make clauses track this information, but I am not sure that that's worth it
    if(us->isClause() && static_cast<Clause*>(us)->isTheoryDescendant()){
      //cout << "HERE with " << us->toString() << endl;
      Inference* inf = us->inference();
      while(inf->rule() == Inference::EVALUATION){
              Inference::Iterator piit = inf->iterator();
              inf = inf->next(piit)->inference();
     }
      Stack<Inference*> current;
      current.push(inf);
      unsigned level = 0;
      while(!current.isEmpty()){
        //cout << current.size() << endl;
        Stack<Inference*> next;
        Stack<Inference*>::Iterator it(current);
        while(it.hasNext()){
          Inference* inf = it.next();
          Inference::Iterator iit=inf->iterator();
          while(inf->hasNext(iit)) {
            Unit* premUnit=inf->next(iit);
            Inference* premInf = premUnit->inference();
            while(premInf->rule() == Inference::EVALUATION){
              Inference::Iterator piit = premInf->iterator();
              premUnit = premInf->next(piit);
              premInf = premUnit->inference(); 
            }

//for(unsigned i=0;i<level;i++){ cout << ">";}; cout << premUnit->toString() << endl;
            next.push(premInf);
          }
        }
        level++;
        current = next;
      }
      level--;
      //cout << "level is " << level << endl;
      
      if(level > max_theory_clause_depth){
        max_theory_clause_depth=level;
      }
    }
  }

  unsigned max_theory_clause_depth;
  bool last_one;
  Stack<unsigned> buckets;

};


struct InferenceStore::TPTPProofPrinter
: public InferenceStore::ProofPrinter
{
  CLASS_NAME(InferenceStore::TPTPProofPrinter);
  USE_ALLOCATOR(InferenceStore::TPTPProofPrinter);
  
  TPTPProofPrinter(ostream& out, InferenceStore* is)
  : ProofPrinter(out, is) {
    splitPrefix = Saturation::Splitter::splPrefix; 
  }

  void print()
  {
    UIHelper::outputSortDeclarations(env.out());
    UIHelper::outputSymbolDeclarations(env.out());
    ProofPrinter::print();
  }

protected:
  vstring splitPrefix;

  vstring getRole(Inference::Rule rule, Unit::InputType origin)
  {
    switch(rule) {
    case Inference::INPUT:
      if (origin==Unit::CONJECTURE) {
	return "conjecture";
      }
      else {
	return "axiom";
      }
    case Inference::NEGATED_CONJECTURE:
      return "negated_conjecture";
    default:
      return "plain";
    }
  }

  vstring tptpRuleName(Inference::Rule rule)
  {
    return StringUtils::replaceChar(Inference::ruleName(rule), ' ', '_');
  }

  vstring unitIdToTptp(vstring unitId)
  {
    return "f"+unitId;
  }

  vstring tptpUnitId(Unit* us)
  {
    return unitIdToTptp(_is->getUnitIdStr(us));
  }

  vstring tptpDefId(Unit* us)
  {
    return unitIdToTptp(Int::toString(us->number())+"_D");
  }

  vstring splitsToString(SplitSet* splits)
  {
    CALL("InferenceStore::TPTPProofPrinter::splitsToString");
    ASS_G(splits->size(),0);

    if (splits->size()==1) {
      return "~"+splitPrefix+Int::toString(splits->sval());
    }
    SplitSet::Iterator sit(*splits);
    vstring res("(");
    while(sit.hasNext()) {
      res+= "~"+splitPrefix+Int::toString(sit.next());
      if (sit.hasNext()) {
	res+=" | ";
      }
    }
    res+=")";
    return res;
  }

  vstring quoteAxiomName(vstring n)
  {
    CALL("InferenceStore::TPTPProofPrinter::quoteAxiomName");

    static vstring allowedFirst("0123456789abcdefghijklmnopqrstuvwxyz");
    const char* allowed="_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";

    if (n.size()==0 || allowedFirst.find(n[0])==vstring::npos ||
	n.find_first_not_of(allowed)!=vstring::npos) {
      n='\''+n+'\'';
    }
    return n;
  }

  vstring getFofString(vstring id, vstring formula, vstring inference, Inference::Rule rule, Unit::InputType origin=Unit::AXIOM)
  {
    CALL("InferenceStore::TPTPProofPrinter::getFofString");

    vstring kind = "fof";
    if(env.statistics->hasTypes){ kind="tff"; }

    return kind+"("+id+","+getRole(rule,origin)+",("+"\n"
	+"  "+formula+"),\n"
	+"  "+inference+").";
  }

  vstring getFormulaString(Unit* us)
  {
    CALL("InferenceStore::TPTPProofPrinter::getFormulaString");

    vstring formulaStr;
    if (us->isClause()) {
      Clause* cl=us->asClause();
      formulaStr=getQuantifiedStr(cl);
      if (cl->splits() && !cl->splits()->isEmpty()) {
	formulaStr+=" | "+splitsToString(cl->splits());
      }
    }
    else {
      FormulaUnit* fu=static_cast<FormulaUnit*>(us);
      formulaStr=getQuantifiedStr(fu);
    }
    return formulaStr;
  }

  bool hasNewSymbols(Unit* u) {
    CALL("InferenceStore::TPTPProofPrinter::hasNewSymbols");
    bool res = _is->_introducedSymbols.find(u->number());
    ASS(!res || _is->_introducedSymbols.get(u->number()).isNonEmpty());
    if(!res){
      res = _is->_introducedSplitNames.find(u->number());
    }
    return res;
  }
  vstring getNewSymbols(vstring origin, vstring symStr) {
    CALL("InferenceStore::TPTPProofPrinter::getNewSymbols(vstring,vstring)");
    return "new_symbols(" + origin + ",[" +symStr + "])";
  }
  /** It is an iterator over SymbolId */
  template<class It>
  vstring getNewSymbols(vstring origin, It symIt) {
    CALL("InferenceStore::TPTPProofPrinter::getNewSymbols(vstring,It)");

    vostringstream symsStr;
    while(symIt.hasNext()) {
      SymbolId sym = symIt.next();
      if (sym.first) {
	symsStr << env.signature->functionName(sym.second);
      }
      else {
	symsStr << env.signature->predicateName(sym.second);
      }
      if (symIt.hasNext()) {
	symsStr << ',';
      }
    }
    return getNewSymbols(origin, symsStr.str());
  }
  vstring getNewSymbols(vstring origin, Unit* u) {
    CALL("InferenceStore::TPTPProofPrinter::getNewSymbols(vstring,Unit*)");
    ASS(hasNewSymbols(u));

    if(_is->_introducedSplitNames.find(u->number())){
      return getNewSymbols(origin,_is->_introducedSplitNames.get(u->number()));
    }

    SymbolStack& syms = _is->_introducedSymbols.get(u->number());
    return getNewSymbols(origin, SymbolStack::ConstIterator(syms));
  }

  void printStep(Unit* us)
  {
    CALL("InferenceStore::TPTPProofPrinter::printStep");

    Inference::Rule rule;
    UnitIterator parents=_is->getParents(us, rule);

    switch(rule) {
    //case Inference::AVATAR_COMPONENT:
    //  printSplittingComponentIntroduction(us);
    //  return;
    case Inference::GENERAL_SPLITTING_COMPONENT:
      printGeneralSplittingComponent(us);
      return;
    case Inference::GENERAL_SPLITTING:
      printSplitting(us);
      return;
    default: ;
    }


    //get vstring representing the formula

    vstring formulaStr=getFormulaString(us);

    //get inference vstring

    vstring inferenceStr;
    if (rule==Inference::INPUT) {
      vstring fileName;
      if (env.options->inputFile()=="") {
	fileName="unknown";
      }
      else {
	fileName="'"+env.options->inputFile()+"'";
      }
      vstring axiomName;
      if (!outputAxiomNames || !Parse::TPTP::findAxiomName(us, axiomName)) {
	axiomName="unknown";
      }
      inferenceStr="file("+fileName+","+quoteAxiomName(axiomName)+")";
    }
    else if (!parents.hasNext()) {
      vstring newSymbolInfo;
      if (hasNewSymbols(us)) {
	newSymbolInfo = getNewSymbols("naming",us);
      }
      inferenceStr="introduced("+tptpRuleName(rule)+",["+newSymbolInfo+"])";
    }
    else {
      ASS(parents.hasNext());
      vstring statusStr;
      if (rule==Inference::SKOLEMIZE) {
	statusStr="status(esa),"+getNewSymbols("skolem",us);
      }

      inferenceStr="inference("+tptpRuleName(rule);

      inferenceStr+=",["+statusStr+"],[";
      bool first=true;
      while(parents.hasNext()) {
        Unit* prem=parents.next();
        if (!first) {
          inferenceStr+=',';
        }
        inferenceStr+=tptpUnitId(prem);
        first=false;
      }
      inferenceStr+="])";
    }

    out<<getFofString(tptpUnitId(us), formulaStr, inferenceStr, rule, us->inputType())<<endl;
  }

  void printSplitting(Unit* us)
  {
    CALL("InferenceStore::TPTPProofPrinter::printSplitting");
    ASS(us->isClause());

    Inference::Rule rule;
    UnitIterator parents=_is->getParents(us, rule);
    ASS(rule==Inference::GENERAL_SPLITTING);

    vstring inferenceStr="inference("+tptpRuleName(rule)+",[],[";

    //here we rely on the fact that the base premise is always put as the first premise in
    //GeneralSplitting::apply 

    ALWAYS(parents.hasNext());
    Unit* base=parents.next();
    inferenceStr+=tptpUnitId(base);

    ASS(parents.hasNext()); //we always split off at least one component
    while(parents.hasNext()) {
      Unit* comp=parents.next();
      ASS(_is->_splittingNameLiterals.find(comp));
      inferenceStr+=","+tptpDefId(comp);
    }
    inferenceStr+="])";

    out<<getFofString(tptpUnitId(us), getFormulaString(us), inferenceStr, rule)<<endl;
  }

  void printGeneralSplittingComponent(Unit* us)
  {
    CALL("InferenceStore::TPTPProofPrinter::printGeneralSplittingComponent");
    ASS(us->isClause());

    Inference::Rule rule;
    UnitIterator parents=_is->getParents(us, rule);
    ASS(!parents.hasNext());

    Literal* nameLit=_is->_splittingNameLiterals.get(us); //the name literal must always be stored

    vstring defId=tptpDefId(us);

    out<<getFofString(tptpUnitId(us), getFormulaString(us),
	"inference("+tptpRuleName(Inference::CLAUSIFY)+",[],["+defId+"])", Inference::CLAUSIFY)<<endl;


    List<unsigned>* nameVars=0;
    VariableIterator vit(nameLit);
    while(vit.hasNext()) {
      unsigned var=vit.next().var();
      ASS(!List<unsigned>::member(var, nameVars)); //each variable appears only once in the naming literal
      List<unsigned>::push(var,nameVars);
    }

    vstring compStr;
    List<unsigned>* compOnlyVars=0;
    Clause::Iterator lits(*us->asClause());
    bool first=true;
    bool multiple=false;
    while(lits.hasNext()) {
      Literal* lit=lits.next();
      if (lit==nameLit) {
	continue;
      }
      if (first) {
	first=false;
      }
      else {
	multiple=true;
	compStr+=" | ";
      }
      compStr+=lit->toString();

      VariableIterator lvit(lit);
      while(lvit.hasNext()) {
        unsigned var=lvit.next().var();
        if (!List<unsigned>::member(var, nameVars) && !List<unsigned>::member(var, compOnlyVars)) {
          List<unsigned>::push(var,compOnlyVars);
        }
      }
    }
    ASS(!first);

    compStr=getQuantifiedStr(compOnlyVars, compStr, multiple);
    List<unsigned>::destroy(compOnlyVars);

    vstring defStr=compStr+" <=> "+Literal::complementaryLiteral(nameLit)->toString();
    defStr=getQuantifiedStr(nameVars, defStr);
    List<unsigned>::destroy(nameVars);

    SymbolId nameSymbol = SymbolId(false,nameLit->functor());
    vostringstream originStm;
    originStm << "introduced(" << tptpRuleName(rule)
	      << ",[" << getNewSymbols("naming",getSingletonIterator(nameSymbol))
	      << "])";

    out<<getFofString(defId, defStr, originStm.str(), rule)<<endl;
  }

  void printSplittingComponentIntroduction(Unit* us)
  {
    CALL("InferenceStore::TPTPProofPrinter::printSplittingComponentIntroduction");
    ASS(us->isClause());

    Clause* cl=us->asClause();
    ASS(cl->splits());
    ASS_EQ(cl->splits()->size(),1);

    Inference::Rule rule=Inference::AVATAR_COMPONENT;

    vstring defId=tptpDefId(us);
    vstring splitPred = splitsToString(cl->splits());
    vstring defStr=getQuantifiedStr(cl)+" <=> ~"+splitPred;

    out<<getFofString(tptpUnitId(us), getFormulaString(us),
  "inference("+tptpRuleName(Inference::CLAUSIFY)+",[],["+defId+"])", Inference::CLAUSIFY)<<endl;

    vstringstream originStm;
    originStm << "introduced(" << tptpRuleName(rule)
        << ",[" << getNewSymbols("naming",splitPred)
        << "])";

    out<<getFofString(defId, defStr, originStm.str(), rule)<<endl;
  }

};

struct InferenceStore::ProofCheckPrinter
: public InferenceStore::ProofPrinter
{
  CLASS_NAME(InferenceStore::ProofCheckPrinter);
  USE_ALLOCATOR(InferenceStore::ProofCheckPrinter);
  
  ProofCheckPrinter(ostream& out, InferenceStore* is)
  : ProofPrinter(out, is) {}

protected:
  void printStep(Unit* cs)
  {
    CALL("InferenceStore::ProofCheckPrinter::printStep");
    Inference::Rule rule;
    UnitIterator parents=_is->getParents(cs, rule);
 
    UIHelper::outputSortDeclarations(out);
    UIHelper::outputSymbolDeclarations(out);

    vstring kind = "fof";
    if(env.statistics->hasTypes){ kind="tff"; } 

    out << kind
        << "(r"<<_is->getUnitIdStr(cs)
    	<< ",conjecture, "
    	<< getQuantifiedStr(cs)
    	<< " ). %"<<Inference::ruleName(rule)<<"\n";

    while(parents.hasNext()) {
      Unit* prem=parents.next();
      out << kind 
        << "(pr"<<_is->getUnitIdStr(prem)
  	<< ",axiom, "
  	<< getQuantifiedStr(prem);
      out << " ).\n";
    }
    out << "%#\n";
  }


  bool hideProofStep(Inference::Rule rule)
  {
    switch(rule) {
    case Inference::INPUT:
    case Inference::CLAUSE_NAMING:
    case Inference::INEQUALITY_SPLITTING_NAME_INTRODUCTION:
    case Inference::INEQUALITY_SPLITTING:
    case Inference::SKOLEMIZE:
    case Inference::EQUALITY_PROXY_REPLACEMENT:
    case Inference::EQUALITY_PROXY_AXIOM1:
    case Inference::EQUALITY_PROXY_AXIOM2:
    case Inference::NEGATED_CONJECTURE:
    case Inference::RECTIFY:
    case Inference::FLATTEN:
    case Inference::ENNF:
    case Inference::NNF:
    case Inference::CLAUSIFY:
    case Inference::AVATAR_DEFINITION:
    case Inference::AVATAR_COMPONENT:
    case Inference::AVATAR_REFUTATION:
    case Inference::AVATAR_SPLIT_CLAUSE:
    case Inference::AVATAR_CONTRADICTION_CLAUSE:
    case Inference::FOOL_LET_ELIMINATION:
    case Inference::FOOL_ITE_ELIMINATION:
    case Inference::FOOL_ELIMINATION:
    case Inference::BOOLEAN_TERM_ENCODING:
    case Inference::CHOICE_AXIOM:
    case Inference::PREDICATE_DEFINITION:
      return true;
    default:
      return false;
    }
  }

  void print()
  {
    ProofPrinter::print();
    out << "%#\n";
  }
};

InferenceStore::ProofPrinter* InferenceStore::createProofPrinter(ostream& out)
{
  CALL("InferenceStore::createProofPrinter");

  switch(env.options->proof()) {
  case Options::Proof::ON:
    return new ProofPrinter(out, this);
  case Options::Proof::PROOFCHECK:
    return new ProofCheckPrinter(out, this);
  case Options::Proof::TPTP:
    return new TPTPProofPrinter(out, this);
  case Options::Proof::PROPERTY:
    return new ProofPropertyPrinter(out,this);
  case Options::Proof::OFF:
    return 0;
  }
  ASSERTION_VIOLATION;
  return 0;
}

/**
 * Output a proof of refutation to out
 *
 *
 */
void InferenceStore::outputProof(ostream& out, Unit* refutation)
{
  CALL("InferenceStore::outputProof(ostream&,Unit*)");

  ProofPrinter* p = createProofPrinter(out);
  if (!p) {
    return;
  }
  ScopedPtr<ProofPrinter> pp(p);
  pp->scheduleForPrinting(refutation);
  pp->print();
}

/**
 * Output a proof of units to out
 *
 */
void InferenceStore::outputProof(ostream& out, UnitList* units)
{
  CALL("InferenceStore::outputProof(ostream&,UnitList*)");

  ProofPrinter* p = createProofPrinter(out);
  if (!p) {
    return;
  }
  ScopedPtr<ProofPrinter> pp(p);
  UnitList::Iterator uit(units);
  while(uit.hasNext()) {
    Unit* u = uit.next();
    pp->scheduleForPrinting(u);
  }
  pp->print();
}

InferenceStore* InferenceStore::instance()
{
  static ScopedPtr<InferenceStore> inst(new InferenceStore());
  
  return inst.ptr();
}

}
