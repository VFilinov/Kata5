#include "main.h"

#include "core/os.h"
#include "core/mainargs.h"

#ifdef NO_GIT_REVISION
#define GIT_REVISION "<omitted>"
#else
#include "program/gitinfo.h"
#endif

#include <sstream>
#include "core/logger.h"

//------------------------
#include "core/using.h"
//------------------------
#ifdef USE_VCF
#include "../vcfsolver/VCFsolver.h"
#endif
bool brain = false;
bool extra_time_log = false;

static void printHelp(const vector<string>& args) {
  cout << endl;
  if(args.size() >= 1)
    cout << "Usage: " << args[0] << " SUBCOMMAND ";
  else
    cout << "Usage: " << "./katago" << " SUBCOMMAND ";
  cout << endl;

  cout << R"%%(
---Common subcommands------------------

gtp : Runs GTP engine that can be plugged into any standard Go GUI for play/analysis.
gom : Runs engine for gomocup protocol (Piskvork).
benchmark : Test speed with different numbers of search threads.
genconfig : User-friendly interface to generate a config with rules and automatic performance tuning.

contribute : Connect to online distributed KataGo training and run perpetually contributing selfplay games.

match : Run self-play match games based on a config, more efficient than gtp due to batching.
version : Print version and exit.

analysis : Runs an engine designed to analyze entire games in parallel.
tuner : (OpenCL only) Run tuning to find and optimize parameters that work on your GPU.

---Selfplay training subcommands---------

selfplay : Play selfplay games and generate training data.
gatekeeper : Poll directory for new nets and match them against the latest net so far.

---Testing/debugging subcommands-------------
evalsgf : Utility/debug tool, analyze a single position of a game from an SGF file.

testgpuerror : Print the average error of the neural net between current config and fp32 config.


)%%" << endl;
}

static int handleSubcommand(const string& subcommand, const vector<string>& args) {
  vector<string> subArgs(args.begin()+1,args.end());
  if(subcommand == "analysis")
    return MainCmds::analysis(subArgs);
  if(subcommand == "benchmark")
    return MainCmds::benchmark(subArgs);
  if(subcommand == "contribute")
    return MainCmds::contribute(subArgs);
  if(subcommand == "evalsgf")
    return MainCmds::evalsgf(subArgs);
  else if(subcommand == "gatekeeper")
    return MainCmds::gatekeeper(subArgs);
  else if(subcommand == "genconfig")
    return MainCmds::genconfig(subArgs,args[0]);
  else if(subcommand == "gtp")
    return MainCmds::gtp(subArgs);
  //#ifdef FORGOMOCUP
  else if(subcommand == "gom")
    return MainCmds::gomprotocol(subArgs);
  //#endif
  else if(subcommand == "tuner")
    return MainCmds::tuner(subArgs);
  else if(subcommand == "match")
    return MainCmds::match(subArgs);
  else if(subcommand == "selfplay")
    return MainCmds::selfplay(subArgs);
  else if(subcommand == "testgpuerror")
    return MainCmds::testgpuerror(subArgs);
  else if(subcommand == "samplesgfs")
    return MainCmds::samplesgfs(subArgs);
  else if(subcommand == "dataminesgfs")
    return MainCmds::dataminesgfs(subArgs);
  else if(subcommand == "genbook")
    return MainCmds::genbook(subArgs);
  else if(subcommand == "writebook")
    return MainCmds::writebook(subArgs);
  else if(subcommand == "checkbook")
    return MainCmds::checkbook(subArgs);
  else if(subcommand == "booktoposes")
    return MainCmds::booktoposes(subArgs);
  else if(subcommand == "trystartposes")
    return MainCmds::trystartposes(subArgs);
  else if(subcommand == "viewstartposes")
    return MainCmds::viewstartposes(subArgs);
  else if(subcommand == "checksgfhintpolicy")
    return MainCmds::checksgfhintpolicy(subArgs);
  else if(subcommand == "demoplay")
    return MainCmds::demoplay(subArgs);
  //else if(subcommand == "writetrainingdata")
  //  return MainCmds::writetrainingdata(subArgs);
  else if(subcommand == "sampleinitializations")
    return MainCmds::sampleinitializations(subArgs);
  else if(subcommand == "evalrandominits")
    return MainCmds::evalrandominits(subArgs);
  else if(subcommand == "printclockinfo")
    return MainCmds::printclockinfo(subArgs);
  else if(subcommand == "sandbox")
    return MainCmds::sandbox();
  else if(subcommand == "version") {
    cout << Version::getKataGoVersionFullInfo() << std::flush;
    return 0;
  }
  else {
    cout << "Unknown subcommand: " << subcommand << endl;
    printHelp(args);
    return 1;
  }
  return 0;
}


int main(int argc, const char* const* argv) {
  vector<string> args = MainArgs::getCommandLineArgsUTF8(argc,argv);
  MainArgs::makeCoutAndCerrAcceptUTF8();

  if(args.size() < 2) {
    //#ifdef FORGOMOCUP
    std::string _progName = args.front();
    std::string _fileName;

    size_t slash = _progName.find_last_of("/\\");
    if(slash != string::npos)
      _fileName = _progName.substr(slash + 1, _progName.length() - slash - 1);
    else
      _fileName = _progName;

    if(Global::toLower(_fileName).substr(0, 7) == "pbrain-")
      brain = true;
    //#endif

    if(brain) {
      //#ifdef FORGOMOCUP
      vector<string> subArgs(args.begin(), args.end());
      subArgs.push_back("-config");
      subArgs.push_back("config.cfg");
      subArgs.push_back("-model");
      subArgs.push_back("model.bin.gz");
      Logger logger;
      logger.addFile("kata.log");
      logger.write("Run gomprotocol");
#ifdef USE_VCF
      VCFsolver::init();
#endif
      return MainCmds::gomprotocol(subArgs);
      //#endif
    } else {
      printHelp(args);
      return 0;
    }
  }
  string cmdArg = string(args[1]);
  if(cmdArg == "-h" || cmdArg == "--h" || cmdArg == "-help" || cmdArg == "--help" || cmdArg == "help") {
    printHelp(args);
    return 0;
  }

  // init
#ifdef USE_VCF
  VCFsolver::init();
#endif

#if defined(OS_IS_WINDOWS)
  //On windows, uncaught exceptions reaching toplevel don't normally get printed out,
  //so explicitly catch everything and print
  int result;
  try {
    result = handleSubcommand(cmdArg, args);
  }
  catch(std::exception& e) {
    cerr << "Uncaught exception: " << e.what() << endl;
    return 1;
  }
  catch(...) {
    cerr << "Uncaught exception that is not a std::exception... exiting due to unknown error" << endl;
    return 1;
  }
  return result;
#else
  return handleSubcommand(cmdArg, args);
#endif
}


string Version::getKataGoVersion() {
  return string("1.15.3");
}

string Version::getKataGoVersionForHelp() {
  return string("KataGo v1.15.3.5 (modified by Vladimir Filinov)");
}

string Version::getKataGoVersionFullInfo() {
  ostringstream out;
  out << Version::getKataGoVersionForHelp() << endl;
#ifndef NO_GIT_REVISION
  out << "Git revision: " << Version::getGitRevision() << endl;
#endif
  out << "Compile Time: " << __DATE__ << " " << __TIME__ << endl;
#if defined(USE_CUDA_BACKEND)
  out << "Using CUDA backend" << endl;
#if defined(CUDA_TARGET_VERSION)
#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
  out << "Compiled with CUDA version " << STRINGIFY2(CUDA_TARGET_VERSION) << endl;
#endif
#elif defined(USE_TENSORRT_BACKEND)
  out << "Using TensorRT backend" << endl;
#elif defined(USE_OPENCL_BACKEND)
  out << "Using OpenCL backend" << endl;
#elif defined(USE_EIGEN_BACKEND)
  out << "Using Eigen(CPU) backend" << endl;
#else
  out << "Using dummy backend" << endl;
#endif

#if defined(USE_AVX)
  out << "Compiled with AVX instructions" << endl;
#endif
#if defined(USE_AVX2)
  out << "Compiled with AVX2 and FMA instructions" << endl;
#endif
#if defined(USE_AVX512)
  out << "Compiled with AVX512 and FMA instructions" << endl;
#endif
#if defined(COMPILE_MAX_BOARD_LEN)
  out << "Compiled to allow boards of size up to " << COMPILE_MAX_BOARD_LEN << endl;
#endif
#if defined(CACHE_TENSORRT_PLAN) && defined(USE_TENSORRT_BACKEND)
  out << "Compiled with TensorRT plan cache" << endl;
#elif defined(BUILD_DISTRIBUTED)
  out << "Compiled to support contributing to online distributed selfplay" << endl;
#endif

#if defined(USE_VCF)
  out << "Compiled with VCF search algorithm" << endl;
#endif


#if defined(QUANTIZED_OUTPUT)
  out << "Compiled with QUANTIZED OUTPUT" << endl;
#endif

  return out.str();
}

string Version::getGitRevision() {
  return string(GIT_REVISION);
}

string Version::getGitRevisionWithBackend() {
  string s = string(GIT_REVISION);

#if defined(USE_CUDA_BACKEND)
  s += "-cuda";
#elif defined(USE_TENSORRT_BACKEND)
  s += "-trt";
#elif defined(USE_OPENCL_BACKEND)
  s += "-opencl";
#elif defined(USE_EIGEN_BACKEND)
  s += "-eigen";
#else
  s += "-dummy";
#endif
  return s;
}
