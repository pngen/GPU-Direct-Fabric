#include "gpudirectfabric/coordinator.hpp"
#include "gpudirectfabric/coordinator_server.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  unsigned short port = 0;
  if (argc >= 2) port = static_cast<unsigned short>(std::atoi(argv[1]));
  gpudirectfabric::Coordinator coord(gpudirectfabric::CoordinatorEpoch(1));
  gpudirectfabric::CoordinatorServer server(coord);
  return server.run(port);
}
