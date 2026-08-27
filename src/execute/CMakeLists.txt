simrv_add_component(simrv-execution
  ExecuteUnitInt.cpp
  ExecuteUnitB.cpp
  ExecuteUnitFloat.cpp
  ExecuteUnit.cpp
  ExecuteUnitVector.cpp
  vector/VectorConfig.cpp
  vector/VectorMemory.cpp
  vector/VectorInteger.cpp
  vector/VectorFixedPoint.cpp
  vector/VectorFloat.cpp
  vector/VectorPermute.cpp
)
add_library(simrv::execution ALIAS simrv-execution)
