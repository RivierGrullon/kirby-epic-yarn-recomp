# Run after upstream has declared its runtime and product targets.
cmake_language(DEFER CALL include "${KIRBY_PROJECT_ROOT}/cmake/native_targets.cmake")
