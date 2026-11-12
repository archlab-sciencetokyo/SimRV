# ==============================================================================
# SimRV External Device and Extension Plugins
# ==============================================================================
option(SIMRV_BUILD_RVCOMP_PLUGINS "Build RVComp reference external device plugins" ON)
option(SIMRV_BUILD_CFU_PLUGINS "Build CFU hardware reference plugins" ON)

if(SIMRV_BUILD_RVCOMP_PLUGINS)
  add_subdirectory(rvcomp-mmc)
  add_subdirectory(rvcomp-reset)
  add_subdirectory(rvcomp-ethernet)
  add_subdirectory(rvcomp-uart)
  add_subdirectory(rv98-keyboard)

  install(TARGETS simrv-mmc simrv-reset simrv-ethernet simrv-rvcomp-uart simrv-rv98-keyboard
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/simrv/plugins
    COMPONENT Tools
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()

if(SIMRV_BUILD_CFU_PLUGINS)
  add_subdirectory(cfu-provingground)

  install(TARGETS simrv-cfu-provingground
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/simrv/plugins
    COMPONENT Tools
    PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()



