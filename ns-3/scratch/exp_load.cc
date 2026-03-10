#include "ns3/core-module.h"

#include <iostream>

int
main(int argc, char* argv[])
{
  ns3::CommandLine cmd;
  cmd.Parse(argc, argv);

  std::cout
    << "exp_load is a stage-1 placeholder binary.\n"
    << "Use ./experiments/run_load_experiment.sh or "
    << "./experiments/runners/run_load_experiment.sh.\n"
    << "The active implementation remains in scratch/iroute-exp-baselines.cc.\n";
  return 0;
}
