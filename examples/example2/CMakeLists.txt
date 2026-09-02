cmake_minimum_required(VERSION 3.20)
project(extract C)

include(CTest)

find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBMAGIC REQUIRED IMPORTED_TARGET libmagic)

add_executable(${PROJECT_NAME} src/main.c src/handlers.c)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::LIBMAGIC
    LibXml2::LibXml2
    ZLIB::ZLIB
)
target_compile_features(${PROJECT_NAME} PRIVATE c_std_11)
