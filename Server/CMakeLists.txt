# Copyright (C) 2024 Paul Johnson
# Copyright (C) 2024-2025 Maxim Nesterov

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

set(CMAKE_SYSTEM_NAME Generic)

cmake_minimum_required(VERSION 3.16)

project(rrolf-server)
include_directories(..)

set(SRCS
    MobAi/Ant.c
    MobAi/Helpers.c
    MobAi/Hybrid.c
    System/Ai.c
    System/Camera.c
    System/Centipede.c
    System/Checkpoints.c
    System/CollisionDetection.c
    System/CollisionResolution.c
    System/Drops.c
    System/Health.c
    System/PetalBehavior.c
    System/Velocity.c
    System/Web.c
    System/BeehiveGen.c
    System/AntHoleGen.c
    Main.c
    EntityAllocation.c
    EntityDetection.c
    Client.c
    Logs.c
    Server.c
    Simulation.c
    SpatialHash.c
    Squad.c
    UpdateProtocol.c
    Waves.c
    ../Shared/Component/Ai.c
    ../Shared/Component/Arena.c
    ../Shared/Component/Centipede.c
    ../Shared/Component/Drop.c
    ../Shared/Component/Flower.c
    ../Shared/Component/Health.c
    ../Shared/Component/Mob.c
    ../Shared/Component/Nest.c
    ../Shared/Component/Petal.c
    ../Shared/Component/Physical.c
    ../Shared/Component/PlayerInfo.c
    ../Shared/Component/Relations.c
    ../Shared/Component/Web.c
    ../Shared/Api.c
    ../Shared/Binary.c
    ../Shared/Bitset.c
    # ../Shared/cJSON.c
    ../Shared/Crypto.c
    ../Shared/pb.c
    ../Shared/SimulationCommon.c
    ../Shared/StaticData.c
    ../Shared/Ctf.c
    Ctf.c
    CtfRules.c
    Snail.c
    ../Shared/Utilities.c
    ../Shared/Vector.c
)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRR_SERVER=1")

# release by default
if(DEBUG_BUILD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -gdwarf-4")
    if (WINDOWS)
    else()
#        add_compile_options(-fsanitize=undefined,address)
 #       add_link_options(-fsanitize=undefined,address)
    endif()
elseif(CALLGRIND_BUILD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG -gdwarf-4 -march=native")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG -O3 -ffast-math -gdwarf-4")
    # link time optimiations
    if(WINDOWS)
    else()
        # set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

if(RIVET_BUILD)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRIVET_BUILD")
    set(SRCS ${SRCS} ../Shared/Rivet.c)
endif()

if(SANDBOX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSANDBOX")
endif()

if(PVP)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPVP")
endif()

# the garden server: a bug filled backyard with its own map, see RR_MAZES
if(GARDEN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DGARDEN")
endif()

# capture the flag minigame. It reuses everything the pvp build does (teams per
# squad, who can see whom, kill drops), so it turns that on too
if(CTF)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCTF -DPVP")
endif()

# a capture the flag build for the automated test in tools/ctf. It lets any
# client move itself, so it must never be deployed
if(CTF_TEST)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCTF_TEST")
endif()

# lets a client with the PIN give itself petals, see rr_dev_cheat_give_petals
if(PDEV)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPDEV")
endif()

add_executable(rrolf-server ${SRCS})

# tools/garden/run-sim-test.sh: the garden's mobs on a simulation with no network
if(GARDEN_SIM_TEST)
    set(TEST_SRCS ${SRCS})
    list(REMOVE_ITEM TEST_SRCS Main.c)
    add_executable(garden-sim-test ${TEST_SRCS} ../tools/garden/sim-test.c)
    target_link_libraries(garden-sim-test pthread websockets m curl)
endif()

if (WINDOWS)
    target_link_directories(rrolf-server PRIVATE /c/libwebsockets/unix-build/bin)
    target_link_libraries(rrolf-server ws2_32 wsock32)
endif()
target_link_libraries(rrolf-server pthread websockets m)

if (NUSE_CURL)
else()
    target_link_libraries(rrolf-server curl)
endif()
