# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/marti/.pico-sdk/sdk/2.3.1/tools/pioasm")
  file(MAKE_DIRECTORY "C:/Users/marti/.pico-sdk/sdk/2.3.1/tools/pioasm")
endif()
file(MAKE_DIRECTORY
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pioasm"
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pioasm-install"
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/tmp"
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp"
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/src"
  "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Documents/MUSIQUE/EPINETTE/Kit Heugel_Bedard/build/clavier/pico-sdk/src/rp2_common/pico_status_led/pioasm/src/pioasmBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
