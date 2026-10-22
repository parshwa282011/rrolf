// Copyright (C) 2024 Paul Johnson
// Copyright (C) 2024-2025 Maxim Nesterov

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <Shared/Ctf.h>

#include <string.h>

#include <Shared/StaticData.h>

// The maze is compiled into the server and into the client, it is not sent
// over the network. Dropping the walls therefore rewrites the same cells and
// rebuilds the grid on both sides, from the same list.

static uint8_t dropped = 0;
// the seam cells as the map file has them, to put the walls back
static uint8_t seam_closed[RR_CTF_TEMPLATE_DIM * RR_CTF_TEMPLATE_DIM];

static void rebuild()
{
    rr_init_maze_grid(sizeof(RR_MAZE_CTF[0]) / sizeof(struct rr_maze_grid),
                      &RR_MAZE_TEMPLATE_CTF[0][0], &RR_MAZE_CTF[0][0]);
}

void rr_ctf_drop_walls()
{
    if (dropped)
        return;
    dropped = 1;
    for (uint16_t i = 0; i < RR_CTF_SEAM_COUNT; ++i)
    {
        struct rr_ctf_seam_cell *cell = &RR_CTF_SEAM_CELLS[i];
        RR_MAZE_TEMPLATE_CTF[cell->y][cell->x] = cell->value;
    }
    rebuild();
}

void rr_ctf_reset_walls()
{
    if (!dropped)
        return;
    dropped = 0;
    for (uint16_t i = 0; i < RR_CTF_SEAM_COUNT; ++i)
    {
        struct rr_ctf_seam_cell *cell = &RR_CTF_SEAM_CELLS[i];
        RR_MAZE_TEMPLATE_CTF[cell->y][cell->x] = 0;
    }
    rebuild();
}

uint8_t rr_ctf_walls_dropped() { return dropped; }

uint8_t rr_ctf_team_quadrant(uint8_t team)
{
    // two teams face each other from opposite corners
    static uint8_t const order[RR_CTF_MAX_TEAMS] = {0, 3, 1, 2};
    return order[team % RR_CTF_MAX_TEAMS];
}

uint32_t rr_ctf_team_color(uint8_t team)
{
    static uint32_t const colors[RR_CTF_MAX_TEAMS] = {
        0xffe03c3c, 0xff3c7ee0, 0xff3cc45a, 0xffe8c93a};
    return colors[team % RR_CTF_MAX_TEAMS];
}

char const *rr_ctf_team_name(uint8_t team)
{
    static char const *const names[RR_CTF_MAX_TEAMS] = {"Red", "Blue", "Green",
                                                        "Yellow"};
    return names[team % RR_CTF_MAX_TEAMS];
}

float rr_ctf_cell_size() { return 2 * RR_MAZES[rr_biome_id_ctf].grid_size; }
