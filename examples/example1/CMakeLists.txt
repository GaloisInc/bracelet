cmake_minimum_required(VERSION 3.15)
project(compressor CXX)

include(CTest)

find_package(ZLIB REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(LibXml2 REQUIRED)

pkg_check_modules(LIBMAGIC IMPORTED_TARGET libmagic)
add_executable(${PROJECT_NAME} src/main.cpp src/file_handler.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} ZLIB::ZLIB PkgConfig::LIBMAGIC CLI11::CLI11 LibXml2::LibXml2)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

if(BUILD_TESTING)
  add_test(NAME example1-text
    COMMAND compressor --file ${CMAKE_SOURCE_DIR}/example-input/input.txt)
  set_tests_properties(example1-text PROPERTIES
    PASS_REGULAR_EXPRESSION "The quick brown fox jumps over the lazy dog")

  add_test(NAME example1-xml
    COMMAND compressor --file ${CMAKE_SOURCE_DIR}/example-input/input.xml)
  set_tests_properties(example1-xml PROPERTIES
    PASS_REGULAR_EXPRESSION "Root: catalog")

  add_test(NAME example1-gzip
    COMMAND compressor --file ${CMAKE_SOURCE_DIR}/example-input/input.txt.gz)
  set_tests_properties(example1-gzip PROPERTIES
    PASS_REGULAR_EXPRESSION "Finished inflate call current total: 43")
endif()
