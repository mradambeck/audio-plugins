# Enables the AAX plugin format when an AAX SDK is available locally. The SDK is under Avid
# NDA and isn't redistributable, so it can't be fetched like JUCE is -- each dev machine points
# at its own local copy via the WILDJAG_AAX_SDK_PATH cache variable (or the AAX_SDK_PATH env
# var as a convenience default). Machines without it just build AU/VST3/Standalone as before.
#
# Include this AFTER FetchJUCE.cmake (juce_set_aax_sdk_path needs JUCE's CMake functions to
# exist) and BEFORE juce_add_plugin(). Sets WILDJAG_AAX_FORMAT to either "AAX" or "" so callers
# can drop it straight into their FORMATS list, e.g.:
#
#   FORMATS AU VST3 Standalone ${WILDJAG_AAX_FORMAT}
#
# NOTE: juce_set_aax_sdk_path() may only be called once per CMake configure -- harmless here
# since every plugin is its own top-level CMake project with its own configure.
#
# NOTE: this produces an UNSIGNED .aaxplugin bundle. Pro Tools requires AAX plugins to be
# signed via Avid's PACE/iLok wraptool to load outside of a debug/dev-unlocked install --
# wraptool signing isn't wired up here and needs a PACE developer account to add.

if(DEFINED ENV{AAX_SDK_PATH} AND NOT DEFINED WILDJAG_AAX_SDK_PATH)
    set(WILDJAG_AAX_SDK_PATH "$ENV{AAX_SDK_PATH}")
endif()

set(WILDJAG_AAX_SDK_PATH "${WILDJAG_AAX_SDK_PATH}" CACHE PATH
    "Path to a local Avid AAX SDK checkout (leave empty to skip building the AAX format)")

set(WILDJAG_AAX_FORMAT "")

if(WILDJAG_AAX_SDK_PATH AND EXISTS "${WILDJAG_AAX_SDK_PATH}/Interfaces/ACF")
    if(NOT TARGET juce_aax_sdk)
        juce_set_aax_sdk_path("${WILDJAG_AAX_SDK_PATH}")
    endif()
    set(WILDJAG_AAX_FORMAT "AAX")
elseif(WILDJAG_AAX_SDK_PATH)
    message(WARNING "WILDJAG_AAX_SDK_PATH is set to '${WILDJAG_AAX_SDK_PATH}' but no "
        "Interfaces/ACF folder was found there -- skipping AAX format")
else()
    message(STATUS "AAX SDK path not set (WILDJAG_AAX_SDK_PATH or $AAX_SDK_PATH) - skipping AAX format")
endif()
