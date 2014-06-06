/**
 * @file MainLoopContext.cpp
 *
 * @since 2 Jun 2014
 * @author dmitry
 */
#include "MainLoopContext.hpp"

//#include "Kernel/Problem.hpp"
//#include "Lib/Environment.hpp"
//#include "Saturation/SaturationAlgorithm.hpp"
//#include "Shell/Options.hpp"

namespace Kernel {

using Lib::Environment;
//using Lib::env;
using Shell::Options;
using Saturation::SaturationAlgorithm;

	MainLoopContext::MainLoopContext(Problem& prb, const Options& opt):
			_prb(prb), _opt(opt) {

		CALL("MainLoopContext::MainLoopContext");

//		if(Lib::env) {
			_env = new Environment(*Lib::env);
//		}else{
//			_env = new Environment();
//		}

//		init();
	}

	MainLoopContext::~MainLoopContext() {
		CALL("MainLoopContext::~MainLoopContext");

//		cleanUp();
		delete _env;
	}


	void MainLoopContext::switchIn() {
		CALL("MainLoopContext::switchIn");
		_temp_env = Lib::env;
		Lib::env = _env; //TODO: Potential change of context by other MainLoop
	}

	void MainLoopContext::switchOut() {
		CALL("MainLoopContext::switchOut");

		Lib::env = _temp_env;
	}

}