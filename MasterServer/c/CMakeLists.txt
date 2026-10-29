# Copyright (C) 2024  Paul Johnson

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.

# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)
project(rysteria-master C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(WITH_POSTGRES "PostgreSQL backend" ON)
option(WITH_MONGO "MongoDB backend" ON)

find_package(PkgConfig REQUIRED)

# Homebrew keeps some of these keg-only / unlinked
foreach(prefix /opt/homebrew /usr/local)
    foreach(pkg libpq mongo-c-driver libsodium libwebsockets curl openssl@3)
        if(EXISTS "${prefix}/opt/${pkg}/lib/pkgconfig")
            set(ENV{PKG_CONFIG_PATH} "${prefix}/opt/${pkg}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        endif()
    endforeach()
endforeach()

pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(WEBSOCKETS REQUIRED IMPORTED_TARGET libwebsockets)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

set(MS_SOURCES
    api.c
    auth.c
    config.c
    db.c
    discord.c
    games.c
    jobs.c
    oauth.c
    proto.c
    ratelimit.c
    reset.c
    server.c
    srv.c
    tmpstore.c
    tokens.c
    util.c
    ../../Shared/cJSON.c
)

set(MS_LIBS PkgConfig::SODIUM PkgConfig::WEBSOCKETS PkgConfig::CURL pthread m)
set(MS_DEFS "")

if(WITH_POSTGRES)
    pkg_check_modules(PQ IMPORTED_TARGET libpq)
    if(PQ_FOUND)
        list(APPEND MS_SOURCES db_postgres.c)
        list(APPEND MS_LIBS PkgConfig::PQ)
        list(APPEND MS_DEFS MS_WITH_POSTGRES)
    else()
        message(WARNING "libpq not found: building without PostgreSQL")
    endif()
endif()

if(WITH_MONGO)
    pkg_check_modules(MONGOC IMPORTED_TARGET mongoc2)
    if(NOT MONGOC_FOUND)
        pkg_check_modules(MONGOC IMPORTED_TARGET libmongoc-1.0)
    endif()
    if(MONGOC_FOUND)
        list(APPEND MS_SOURCES db_mongo.c)
        list(APPEND MS_LIBS PkgConfig::MONGOC)
        list(APPEND MS_DEFS MS_WITH_MONGO)
    else()
        message(WARNING "mongo-c-driver not found: building without MongoDB")
    endif()
endif()

add_library(ms_core STATIC ${MS_SOURCES})
target_include_directories(ms_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                           ${CMAKE_CURRENT_SOURCE_DIR}/../../Shared)
target_compile_definitions(ms_core PUBLIC ${MS_DEFS})
target_link_libraries(ms_core PUBLIC ${MS_LIBS})
target_compile_options(ms_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(rysteria-master main.c)
target_link_libraries(rysteria-master ms_core)
target_compile_options(rysteria-master PRIVATE -Wall -Wextra -Wno-unused-parameter)

enable_testing()
add_subdirectory(tests)
