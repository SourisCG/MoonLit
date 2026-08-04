add_executable(
  moonlit-tests
  CaptureStateMachineTests.cpp
  EncoderResolverTests.cpp
  ExportMathTests.cpp
  ExportQueueTests.cpp
  JsonImportTests.cpp
  MediaTests.cpp
  MoonLitPathsTests.cpp
  MoonLitTest.hpp
  SqliteRepositoryTests.cpp
  TestMedia.hpp
  TimelineExportTests.cpp
  TimelineTests.cpp
  main.cpp
)

target_link_libraries(moonlit-tests PRIVATE MoonLit::ClipCore sqlite3)

set_target_properties(moonlit-tests PROPERTIES FOLDER test AUTOMOC ON)

add_test(NAME moonlit-tests COMMAND moonlit-tests)

if(WIN32)
  set_tests_properties(
    moonlit-tests
    PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$<TARGET_FILE_DIR:FFmpeg::avutil>"
  )
endif()
