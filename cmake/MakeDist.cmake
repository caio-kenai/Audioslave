# Creates the release artefacts in dist/ (run by the `dist` target):
#   Audioslave-Setup.exe        installer (Audioslave.exe embedded)
#   Audioslave-Portable.zip     Audioslave.exe + README + LICENSE (no install)
#   SHA256SUMS.txt
# Inputs: BIN_DIR, DIST_DIR, SOURCE_DIR, VERSION.

foreach(var BIN_DIR DIST_DIR SOURCE_DIR VERSION)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "MakeDist.cmake: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${DIST_DIR}")
file(MAKE_DIRECTORY "${DIST_DIR}")

file(COPY_FILE "${BIN_DIR}/Audioslave-Setup.exe" "${DIST_DIR}/Audioslave-Setup.exe")

set(portable "${DIST_DIR}/portable")
file(MAKE_DIRECTORY "${portable}")
file(COPY_FILE "${BIN_DIR}/Audioslave.exe" "${portable}/Audioslave.exe")
file(COPY_FILE "${SOURCE_DIR}/README.md" "${portable}/README.md")
file(COPY_FILE "${SOURCE_DIR}/LICENSE" "${portable}/LICENSE.txt")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${DIST_DIR}/Audioslave-Portable.zip" --format=zip
            Audioslave.exe README.md LICENSE.txt
    WORKING_DIRECTORY "${portable}"
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Could not create Audioslave-Portable.zip")
endif()
file(REMOVE_RECURSE "${portable}")

set(sums "")
foreach(artefact Audioslave-Setup.exe Audioslave-Portable.zip)
    file(SHA256 "${DIST_DIR}/${artefact}" hash)
    string(APPEND sums "${hash}  ${artefact}\n")
endforeach()
file(WRITE "${DIST_DIR}/SHA256SUMS.txt" "${sums}")

message(STATUS "Audioslave ${VERSION} release artefacts in ${DIST_DIR}:\n${sums}")
