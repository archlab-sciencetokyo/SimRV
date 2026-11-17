include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(SimRV PROPERTIES EXPORT_NAME cli OUTPUT_NAME simrv)

install(
  TARGETS SimRV simrv-runtime simrv-devel
  EXPORT SimRVTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  DIRECTORY include/simrv
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT Development
  FILES_MATCHING
  PATTERN "*.hpp"
  PATTERN "*.h")
install(
  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  COMPONENT Development
  FILES_MATCHING
  PATTERN "*.hpp")
install(
  DIRECTORY schemas/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/schemas
  COMPONENT Tools
  FILES_MATCHING
  PATTERN "*.json"
  PATTERN "simrv-build-metadata.schema.json" EXCLUDE)
install(
  FILES schemas/simrv-build-metadata.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/schemas
  COMPONENT Runtime)

install(
  TARGETS simrv-config-wizard simrv-tools simrv-parity simrv-benchmark
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT Tools)
if(TARGET simrv-linux-builder)
  install(
    TARGETS simrv-linux-builder simrv-linux-builder-post-build
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools)
  install(
    FILES scripts/linux-versions.conf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/LinuxBuilder/scripts
    COMPONENT Tools)
  install(
    DIRECTORY scripts/templates/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/LinuxBuilder/scripts/templates
    COMPONENT Tools)
  install(
    DIRECTORY scripts/linux/templates/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/LinuxBuilder/scripts/linux/templates
    COMPONENT Tools)
  install(
    DIRECTORY configs/buildroot/simrv-buildroot/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/LinuxBuilder/configs/buildroot/simrv-buildroot
    COMPONENT Tools)
endif()
install(
  TARGETS simrv-act simrv-vector
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT Tests)

# Native release packages contain the Runtime, Tools, Tests, and Development
# components. Keep distro release builds limited to the executable payloads
# those components install; the architecture qualification workflow builds the
# full project independently.
add_custom_target(
  simrv-package-binaries
  DEPENDS SimRV simrv-config-wizard simrv-tools simrv-parity simrv-benchmark
          simrv-act simrv-vector)
if(TARGET simrv-linux-builder)
  add_dependencies(simrv-package-binaries simrv-linux-builder)
endif()
install(
  DIRECTORY configs/models/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV/models
  COMPONENT Runtime
  FILES_MATCHING
  PATTERN "*.cfg")
install(
  FILES include/simrv/rtl/ParityPlugin.h include/simrv/rtl/ParityTarget.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/simrv/rtl
  COMPONENT Development)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/SimRVBuildMetadata.json"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/SimRV
  COMPONENT Runtime)

install(FILES docs/user/index.md DESTINATION ${CMAKE_INSTALL_DOCDIR}
        RENAME USER_GUIDE.md COMPONENT Runtime)
install(
  DIRECTORY docs/user/
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/user
  COMPONENT Runtime
  FILES_MATCHING
  PATTERN "index.md" EXCLUDE
  PATTERN "*.md")
install(
  DIRECTORY examples/hello/
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/hello
  COMPONENT Runtime
  PATTERN ".gitignore" EXCLUDE
  PATTERN "build-*" EXCLUDE
  PATTERN "*.o" EXCLUDE)
install(
  DIRECTORY examples/isa/
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples/isa
  COMPONENT Runtime
  PATTERN ".gitignore" EXCLUDE
  PATTERN "build-*" EXCLUDE
  PATTERN "*.o" EXCLUDE)

install(FILES docs/man/simrv.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        COMPONENT Runtime)
install(
  FILES docs/man/simrv-tools.1 docs/man/simrv-config-wizard.1 docs/man/simrv-parity.1
       docs/man/simrv-benchmark.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
  COMPONENT Tools)
install(
  FILES docs/man/simrv-act.1 docs/man/simrv-vector.1
  DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
  COMPONENT Tests)

configure_package_config_file(
  cmake/SimRVConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/SimRV")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(
  EXPORT SimRVTargets
  FILE SimRVTargets.cmake
  NAMESPACE SimRV::
  DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/SimRV"
  COMPONENT Development)
export(
  EXPORT SimRVTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/SimRVTargets.cmake"
  NAMESPACE SimRV::)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/SimRVConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/SimRV"
        COMPONENT Development)
install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/SimRV
        COMPONENT Runtime)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "ArchLab Science Tokyo")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Explainable RISC-V architectural simulator")
set(CPACK_PACKAGE_DESCRIPTION
    "SimRV is an explainable RISC-V architectural simulator.\n It provides CLI execution and an interactive terminal workbench,\n microarchitecture models, and Linux support.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
if(NOT SIMRV_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-(.+))?$")
  message(FATAL_ERROR "SIMRV_VERSION is not packageable: ${SIMRV_VERSION}")
endif()
set(SIMRV_PACKAGE_BASE_VERSION "${CMAKE_MATCH_1}")
set(SIMRV_PACKAGE_PRERELEASE "${CMAKE_MATCH_3}")
set(CPACK_PACKAGE_VERSION "${SIMRV_PACKAGE_BASE_VERSION}")
set(CPACK_PACKAGE_CONTACT "ArchLab Science Tokyo")
set(CPACK_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SimRV")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_FILE_NAME
    "SimRV-${SIMRV_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_GENERATOR "TGZ" CACHE STRING
    "Semicolon-separated CPack generators (for example TGZ;RPM;DEB)")
set(CPACK_COMPONENTS_ALL Runtime Tools Tests Development)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_THREADS 0)
set(CPACK_STRIP_FILES TRUE)
set(CPACK_COMPONENT_TOOLS_DEPENDS Runtime)
set(CPACK_COMPONENT_TESTS_DEPENDS Runtime)
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "SimRV")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
    "Explainable RISC-V simulator runtime for RV64 and RV32 guests.")
set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "SimRV architecture and benchmarking tools")
set(CPACK_COMPONENT_TOOLS_DESCRIPTION
    "Optional configuration, Linux image building, and benchmark tools.")
set(CPACK_COMPONENT_TESTS_DISPLAY_NAME "SimRV compliance test runners")
set(CPACK_COMPONENT_TESTS_DESCRIPTION
    "Optional RISC-V architectural compliance test drivers (ACT4 and Vector).")
set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "SimRV C++ development files")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION
    "C++23 headers, runtime library, and CMake package for downstream projects.")

# Native Linux package metadata. CPack's RPM and DEB generators derive runtime
# library dependencies from the installed ELF when the host packaging tools are
# available; package creation never bundles a compiler or guest image.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(SIMRV_DEB_ARCH "amd64")
  set(SIMRV_RPM_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(SIMRV_DEB_ARCH "arm64")
  set(SIMRV_RPM_ARCH "aarch64")
else()
  set(SIMRV_DEB_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  set(SIMRV_RPM_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(CPACK_DEBIAN_PACKAGE_NAME "simrv")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${SIMRV_DEB_ARCH}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ArchLab Science Tokyo")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
if(SIMRV_PACKAGE_PRERELEASE)
  set(CPACK_DEBIAN_PACKAGE_VERSION
      "${SIMRV_PACKAGE_BASE_VERSION}~${SIMRV_PACKAGE_PRERELEASE}")
else()
  set(CPACK_DEBIAN_PACKAGE_VERSION "${SIMRV_PACKAGE_BASE_VERSION}")
endif()
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_TOOLS_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_TESTS_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME "simrv")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_DEPENDS "libc6, libgcc-s1, libstdc++6")
set(CPACK_DEBIAN_TOOLS_PACKAGE_NAME "simrv-tools")
set(CPACK_DEBIAN_TESTS_PACKAGE_NAME "simrv-tests")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "simrv-devel")
set(CPACK_DEBIAN_RUNTIME_FILE_NAME
    "simrv_${CPACK_DEBIAN_PACKAGE_VERSION}_${SIMRV_DEB_ARCH}.deb")
set(CPACK_DEBIAN_TOOLS_FILE_NAME
    "simrv-tools_${CPACK_DEBIAN_PACKAGE_VERSION}_${SIMRV_DEB_ARCH}.deb")
set(CPACK_DEBIAN_TESTS_FILE_NAME
    "simrv-tests_${CPACK_DEBIAN_PACKAGE_VERSION}_${SIMRV_DEB_ARCH}.deb")
set(CPACK_DEBIAN_DEVELOPMENT_FILE_NAME
    "simrv-devel_${CPACK_DEBIAN_PACKAGE_VERSION}_${SIMRV_DEB_ARCH}.deb")
set(CPACK_DEBIAN_TOOLS_PACKAGE_DEPENDS
    "simrv (= ${CPACK_DEBIAN_PACKAGE_VERSION})")
set(CPACK_DEBIAN_TESTS_PACKAGE_DEPENDS
    "simrv (= ${CPACK_DEBIAN_PACKAGE_VERSION})")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS
    "simrv (= ${CPACK_DEBIAN_PACKAGE_VERSION})")

set(CPACK_RPM_PACKAGE_NAME "simrv")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${SIMRV_RPM_ARCH}")
set(CPACK_RPM_PACKAGE_SUMMARY "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/include
    /usr/lib
    /usr/lib64
    /usr/share/cmake
    /usr/share/doc
    /usr/share/licenses
    /usr/share/man
    /usr/share/man/man1)
# Keep rpmbuild temporary files inside the caller-selected temporary area. This
# avoids host-specific /var/tmp policy and makes local/CI package builds easier
# to isolate.
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _tmppath /tmp")
set(CPACK_RPM_PACKAGE_VERSION "${SIMRV_PACKAGE_BASE_VERSION}")
if(SIMRV_PACKAGE_PRERELEASE)
  string(REPLACE "-" "." SIMRV_RPM_PRERELEASE "${SIMRV_PACKAGE_PRERELEASE}")
  set(CPACK_RPM_PACKAGE_RELEASE "0.${SIMRV_RPM_PRERELEASE}")
else()
  set(CPACK_RPM_PACKAGE_RELEASE "1")
endif()
set(CPACK_RPM_RUNTIME_PACKAGE_NAME "simrv")
set(CPACK_RPM_TOOLS_PACKAGE_NAME "simrv-tools")
set(CPACK_RPM_TESTS_PACKAGE_NAME "simrv-tests")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_NAME "simrv-devel")
set(CPACK_RPM_RUNTIME_FILE_NAME
    "simrv-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${SIMRV_RPM_ARCH}.rpm")
set(CPACK_RPM_TOOLS_FILE_NAME
    "simrv-tools-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${SIMRV_RPM_ARCH}.rpm")
set(CPACK_RPM_TESTS_FILE_NAME
    "simrv-tests-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${SIMRV_RPM_ARCH}.rpm")
set(CPACK_RPM_DEVELOPMENT_FILE_NAME
    "simrv-devel-${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${SIMRV_RPM_ARCH}.rpm")
set(CPACK_RPM_TOOLS_PACKAGE_REQUIRES
    "simrv = ${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}")
set(CPACK_RPM_TESTS_PACKAGE_REQUIRES
    "simrv = ${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES
    "simrv = ${CPACK_RPM_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}")
# Reproducible archive metadata (CMP0206 in CMake 4.3+)
set(CPACK_ARCHIVE_UID 0)
set(CPACK_ARCHIVE_GID 0)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3")
  set(CPACK_ARCHIVE_COMPRESSION_LEVEL 9)
endif()
include(CPack)
