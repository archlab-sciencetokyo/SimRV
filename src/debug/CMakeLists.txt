simrv_add_component(simrv-debug
  GdbStub.cpp
  SpikeLockstep.cpp
  SymbolTable.cpp
  BreakpointManager.cpp
)
add_library(simrv::debug ALIAS simrv-debug)
