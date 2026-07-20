cmake_minimum_required(VERSION 3.20)

project(debufr-profile-extractor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
include(CTest)

option(DEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP "Fetch yaml-cpp automatically when not installed" OFF)

find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
  find_package(yaml-cpp QUIET)
endif()

if(NOT yaml-cpp_FOUND)
  if(DEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP)
    FetchContent_Declare(
      yaml-cpp
      URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
  else()
    message(FATAL_ERROR "yaml-cpp was not found. Install libyaml-cpp-dev or configure with -DDEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP=ON")
  endif()
endif()

add_library(debufr_profile_extractor_lib
  src/Config.cpp
  src/DebufrParser.cpp
  src/CsvWriter.cpp
)

target_include_directories(debufr_profile_extractor_lib
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(debufr_profile_extractor_lib PUBLIC cxx_std_17)

target_compile_options(debufr_profile_extractor_lib PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(TARGET yaml-cpp::yaml-cpp)
  target_link_libraries(debufr_profile_extractor_lib PUBLIC yaml-cpp::yaml-cpp)
else()
  target_link_libraries(debufr_profile_extractor_lib PUBLIC yaml-cpp)
endif()

add_executable(debufr-profile-extractor src/main.cpp)
target_link_libraries(debufr-profile-extractor PRIVATE debufr_profile_extractor_lib)

install(TARGETS debufr-profile-extractor RUNTIME DESTINATION bin)
install(FILES config/profile_extractor.yaml DESTINATION share/debufr-profile-extractor)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
