add_executable(
  moonlit-tests
  ExportMathTests.cpp
  JsonImportTests.cpp
  MediaTests.cpp
  MoonLitPathsTests.cpp
  MoonLitTest.hpp
  SqliteRepositoryTests.cpp
  main.cpp
)

target_link_libraries(moonlit-tests PRIVATE MoonLit::ClipCore sqlite3)

set_target_properties(moonlit-tests PROPERTIES FOLDER test AUTOMOC ON)

add_test(NAME moonlit-tests COMMAND moonlit-tests)

if(WIN32)
  set_tests_properties(
    moonlit-tests
    PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
  )
endif()
