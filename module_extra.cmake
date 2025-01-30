# default ollama directory
find_path(OLLAMA_DIR
        REQUIRED
        NO_CMAKE_FIND_ROOT_PATH
        NAMES
        ollama.hpp
        HINTS
        ${NAP_ROOT}/modules/napollama/ollama/singleheader
)

message(STATUS "ollama dir: ${OLLAMA_DIR}")

target_include_directories(${PROJECT_NAME} PUBLIC ${OLLAMA_DIR})
