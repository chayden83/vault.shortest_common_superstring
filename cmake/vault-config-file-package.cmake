# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(${CMAKE_CURRENT_LIST_DIR}/vault-configure.cmake)

if(NOT VAULT_UNORDERED_SET_ALGORITHM_CONFIG_FILE_PACKAGE)
    return()
endif()

set(${PROJECT_NAME}_DIR
    "${PROJECT_BINARY_DIR}/cmake"
    CACHE PATH
    "Build location of config file package for ${PROJECT_NAME}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/package-config-file.cmake.in"
    "${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}-config.cmake"
    INSTALL_DESTINATION "${VAULT_UNORDERED_SET_ALGORITHM_INSTALL_CMAKEDIR}"
    PATH_VARS PROJECT_NAME PROJECT_VERSION VAULT_SHORT_NAME_UPPER
)

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY
        ${VAULT_UNORDERED_SET_ALGORITHM_CONFIG_FILE_PACKAGE_COMPATIBILITY}
)

# [CMAKE.CONFIG]
install(
    FILES
        "${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}-config.cmake"
        "${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}-version.cmake"
    DESTINATION "${VAULT_UNORDERED_SET_ALGORITHM_INSTALL_CMAKEDIR}"
    COMPONENT
        ${VAULT_UNORDERED_SET_ALGORITHM_CONFIG_FILE_PACKAGE_INSTALL_COMPONENT}
)
