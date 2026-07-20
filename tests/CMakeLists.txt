include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(debufr-profile-extractor-tests test_parser.cpp)
target_link_libraries(debufr-profile-extractor-tests PRIVATE debufr_profile_extractor_lib GTest::gtest_main)
target_compile_features(debufr-profile-extractor-tests PRIVATE cxx_std_17)
target_compile_definitions(debufr-profile-extractor-tests PRIVATE DEBUFR_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(GoogleTest)
gtest_discover_tests(debufr-profile-extractor-tests)
