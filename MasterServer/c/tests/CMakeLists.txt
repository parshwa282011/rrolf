add_executable(test_core test_core.c)
target_link_libraries(test_core ms_core)
add_test(NAME core COMMAND test_core)

# needs a live database: DB_TYPE + POSTGRES_URI / MONGO_URI in the environment
add_executable(test_db test_db.c)
target_link_libraries(test_db ms_core)
