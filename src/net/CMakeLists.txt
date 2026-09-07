simrv_add_component(simrv-net
  Server.cpp
  Client.cpp
)
add_library(simrv::net ALIAS simrv-net)
