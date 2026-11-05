# Damage numbers: a native mod for OpenRAC's port of Ratchet & Clank (from the SDK's template).
#
#   cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk
#   cmake --build build
#   python3 <OpenRAC>/sdk/tools/package_mod.py build/package/damage_numbers
#
# The mod needs interface 1.12, which no SDK release carries yet (the newest is sdk-v1.11.0), so
# OPENRAC_SDK points at an OpenRAC checkout's sdk/ folder. Once sdk-v1.12.0 is released,
# python3 tools/fetch_sdk.py puts the release sdk.lock names in .sdk/ and no -DOPENRAC_SDK is needed.
#
# Without CMake on Windows: build.bat (MSVC only).
cmake_minimum_required(VERSION 3.20)
project(damage_numbers LANGUAGES C CXX)

if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.12 REQUIRED)

openrac_add_mod(damage_numbers MANIFEST openrac-mod.json SOURCES src/mod.cpp)

# The digit props (tools/make_digits.py makes them) go into the package beside the library.
get_target_property(package_dir damage_numbers OPENRAC_MOD_PACKAGE_DIR)
add_custom_command(TARGET damage_numbers POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${package_dir}/assets"
  VERBATIM)
