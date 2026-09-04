# Fetches JUCE via FetchContent, pinned to a fixed tag, replacing the old sibling-checkout
# `add_subdirectory(../JUCE JUCE)` -- a fresh `git clone` of this repo needs no manual JUCE
# setup. Include this BEFORE juce_add_plugin()/juce_add_binary_data().
#
# Only the JUCE *source* checkout is shared across all plugins (via an explicit SOURCE_DIR
# outside any single plugin's build/ tree), so building multiple plugins locally clones JUCE
# once, not once per plugin. Each plugin's *build* of JUCE (BINARY_DIR) is deliberately left at
# its own private default under that plugin's own build/ dir -- sharing BINARY_DIR too was
# considered and rejected: independently-configured CMake projects (potentially even with
# different generators) writing into one shared JUCE binary dir is a collision risk the source
# checkout doesn't have, since FetchContent's per-project "already populated" stamp lives under
# each plugin's own CMAKE_BINARY_DIR regardless of where SOURCE_DIR points.
#
# NOTE: on the very first clone of this monorepo, avoid configuring two plugins at exactly the
# same time (e.g. two `cmake -B build` in parallel) -- the shared checkout step isn't
# lock-protected across separate top-level CMake projects. Configuring one plugin first, then
# the rest, is safe and only needs to happen once per machine.

include(FetchContent)

get_filename_component(WILDJAG_JUCE_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../.deps/juce-9.0.1" ABSOLUTE)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${WILDJAG_JUCE_SOURCE_DIR}"
)

FetchContent_MakeAvailable(juce)
