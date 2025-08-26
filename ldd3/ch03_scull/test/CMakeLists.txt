add_executable(test_scull test_scull.c)
target_include_directories(test_scull PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_scull PRIVATE -Wall -Wextra -O2)
include(CTest)
enable_testing()

# Make sure the smoke script is available/executable from the build tree
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/run_scull_smoke.sh
        DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(CHMOD ${CMAKE_CURRENT_BINARY_DIR}/run_scull_smoke.sh
        PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# --- Setup: run your existing scull_insmod target, then chmod the node ---
# Uses the current build dir (${CMAKE_BINARY_DIR}) so it works in CLion too.

# --- Smoke: run your tester via the smoke script (with sudo) ---
add_test(
        NAME scull_smoke
        COMMAND /bin/bash -c "sudo ${CMAKE_CURRENT_BINARY_DIR}/run_scull_smoke.sh $<TARGET_FILE:test_scull>"
)
set_tests_properties(scull_smoke PROPERTIES
        FIXTURES_REQUIRED scull_env
        ENVIRONMENT "SCULL_MODULE=scull;SCULL_DEV=/dev/scull0"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# (Optional) If you want an unload step, uncomment:
# add_test(NAME scull_teardown
#   COMMAND /bin/bash -c "sudo rmmod scull || true")
# set_tests_properties(scull_teardown PROPERTIES FIXTURES_CLEANUP scull_env)
