# QtTest unit tests over cwb_core (the Widgets-free logic). Run with `ctest`
# (or `ctest --output-on-failure`) from the build dir.

qt_add_executable(test_cesurl test_cesurl.cpp)
target_link_libraries(test_cesurl PRIVATE cwb_core Qt6::Test)
add_test(NAME cesurl COMMAND test_cesurl)

qt_add_executable(test_filekind test_filekind.cpp)
target_link_libraries(test_filekind PRIVATE cwb_core Qt6::Test)
add_test(NAME filekind COMMAND test_filekind)

qt_add_executable(test_cache test_cache.cpp)
target_link_libraries(test_cache PRIVATE cwb_core Qt6::Test)
add_test(NAME cache COMMAND test_cache)


qt_add_executable(test_minermath test_minermath.cpp)
target_link_libraries(test_minermath PRIVATE cwb_core Qt6::Test)
add_test(NAME minermath COMMAND test_minermath)

qt_add_executable(test_url_history test_url_history.cpp)
target_link_libraries(test_url_history PRIVATE cwb_core Qt6::Test)
add_test(NAME url_history COMMAND test_url_history)

qt_add_executable(test_settings test_settings.cpp)
target_link_libraries(test_settings PRIVATE cwb_core Qt6::Test)
add_test(NAME settings COMMAND test_settings)

qt_add_executable(test_codehighlight test_codehighlight.cpp)
target_link_libraries(test_codehighlight PRIVATE cwb_core Qt6::Test)
add_test(NAME codehighlight COMMAND test_codehighlight)

qt_add_executable(test_csvtable test_csvtable.cpp)
target_link_libraries(test_csvtable PRIVATE cwb_core Qt6::Test)
add_test(NAME csvtable COMMAND test_csvtable)

qt_add_executable(test_names test_names.cpp)
target_link_libraries(test_names PRIVATE cwb_core Qt6::Test)
add_test(NAME names COMMAND test_names)

qt_add_executable(test_worktree test_worktree.cpp)
target_link_libraries(test_worktree PRIVATE cwb_core Qt6::Test)
add_test(NAME worktree COMMAND test_worktree)

qt_add_executable(test_credits test_credits.cpp)
target_link_libraries(test_credits PRIVATE cwb_core Qt6::Test)
add_test(NAME credits COMMAND test_credits)

# GUI self-tests: drive the real MainWindow/RenderPane headlessly (offscreen)
# and assert by exit code. These cover the embedded-widget lifecycle and the
# navigation guards that live in the cwb executable, not cwb_core:
#   widgetreuse - the writer survives a relayout with its text + dirty flag
#                 (the resize-resets-the-writer and focus-steal fixes)
#   navguard    - a re-nav to the current URL is ignored (the text-wipe fix)
add_test(NAME widgetreuse COMMAND cwb selftest-widgetreuse)
set_tests_properties(widgetreuse PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME navguard COMMAND cwb selftest-navguard)
set_tests_properties(navguard PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME navprogress COMMAND cwb selftest-navprogress)
set_tests_properties(navprogress PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME writerdocument COMMAND cwb selftest-writerdocument)
set_tests_properties(writerdocument PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME writercursor COMMAND cwb selftest-writercursor)
set_tests_properties(writercursor PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME renderselection COMMAND cwb selftest-renderselection)
set_tests_properties(renderselection PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
