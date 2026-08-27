simrv_add_component(simrv-memory
  ../cache/ICache.cpp
  ../cache/DCache.cpp
  MemoryAccess.cpp
  MemorySubsystem.cpp
  MmioRouter.cpp
  MmioDevice.cpp
  Mmu.cpp
  TileLinkBus.cpp
  CoherenceHub.cpp
)
add_library(simrv::memory ALIAS simrv-memory)
