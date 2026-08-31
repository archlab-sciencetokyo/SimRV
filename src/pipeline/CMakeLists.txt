simrv_add_component(simrv-isa
  Decoder.cpp
)
add_library(simrv::isa ALIAS simrv-isa)

simrv_add_component(simrv-pipeline
  PipelineSim.cpp
  PipelineConfig.cpp
  BranchPredictor.cpp
  PipelineStages.cpp
  CycleKernel.cpp
)
add_library(simrv::pipeline ALIAS simrv-pipeline)
