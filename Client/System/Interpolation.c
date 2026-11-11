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

#include <Client/System/Interpolation.h>

#include <math.h>

#include <Shared/Entity.h>
#include <Shared/SimulationCommon.h>
#include <Shared/Tags.h>
#include <Shared/Vector.h>

struct function_captures
{
    float delta;
    struct rr_simulation *simulation;
};

void system_interpolation_for_each_function(EntityIdx entity, void *_captures)
{
    struct function_captures *captures = _captures;
    struct rr_simulation *this = captures->simulation;
    float delta = captures->delta;
    if (rr_simulation_has_physical(this, entity))
    {
        struct rr_component_physical *physical =
            rr_simulation_get_physical(this, entity);
        if (physical->lerp_x == 0)
            physical->lerp_x = physical->x;
        if (physical->lerp_y == 0)
            physical->lerp_y = physical->y;

        physical->velocity.x = physical->x - physical->lerp_x;
        physical->velocity.y = physical->y - physical->lerp_y;

        physical->lerp_x = rr_lerp(physical->lerp_x, physical->x, 10 * delta);
        physical->lerp_y = rr_lerp(physical->lerp_y, physical->y, 10 * delta);
        physical->lerp_velocity.x =
            rr_lerp(physical->lerp_velocity.x, physical->velocity.x, 5 * delta);
        physical->lerp_velocity.y =
            rr_lerp(physical->lerp_velocity.y, physical->velocity.y, 5 * delta);
        if (!physical->animation_started)
            physical->lerp_angle = physical->angle;
        physical->animation_started = 1;

        physical->lerp_radius =
            rr_lerp(physical->lerp_radius, physical->radius, 10 * delta);
        physical->lerp_angle =
            rr_angle_lerp(physical->lerp_angle, physical->angle, 10 * delta);
        if (rr_simulation_has_mob(this, entity))
        {
            if (physical->turning_animation == 0)
                physical->turning_animation = physical->angle;

            physical->lerp_radius =
                rr_lerp(physical->lerp_radius, physical->radius, 10 * delta);
            physical->lerp_angle = rr_angle_lerp(physical->lerp_angle,
                                                 physical->angle, 10 * delta);
            physical->turning_animation = rr_angle_lerp(
                physical->turning_animation, physical->angle, 6 * delta);

            // the legs of the spider step once a round of the timer: it speeds up much less
            // with the speed than the other mobs, or the legs blur when it starts to chase
            float speed_gain =
                rr_simulation_get_mob(this, entity)->id == rr_mob_id_spider
                    ? 0.12f
                    : 0.5f;
            physical->animation_timer +=
                (2 * (physical->parent_id % 2) - 1) * delta *
                (fmin(rr_vector_get_magnitude(&physical->lerp_velocity), 20) *
                     speed_gain +
                 1) *
                2;
        }
        else
            physical->animation_timer += 0.5;
    }

    if (rr_simulation_has_flower(this, entity))
    {
        struct rr_component_flower *flower =
            rr_simulation_get_flower(this, entity);
        struct rr_component_physical *physical =
            rr_simulation_get_physical(this, entity);
        flower->eye_x = cosf(flower->eye_angle - physical->angle) * 3;
        flower->eye_y = sinf(flower->eye_angle - physical->angle) * 3;
        if (flower->lerp_eye_x == 0)
            flower->lerp_eye_x = flower->eye_x;
        if (flower->lerp_eye_y == 0)
            flower->lerp_eye_y = flower->eye_y;
        if (flower->lerp_mouth == 0)
            flower->lerp_mouth = 15;
        // the legs of a spider flower step faster the faster it goes, and stand still when it does
        if (flower->skin == rr_flower_skin_spider)
        {
            float speed = fminf(rr_vector_get_magnitude(&physical->lerp_velocity), 20);
            flower->leg_phase = fmodf(flower->leg_phase + delta * speed * 1.1f, 2 * (float)M_PI);
        }
        flower->lerp_eye_x =
            rr_lerp(flower->lerp_eye_x, flower->eye_x, 20 * delta);
        flower->lerp_eye_y =
            rr_lerp(flower->lerp_eye_y, flower->eye_y, 20 * delta);
        if (flower->face_flags & 1)
            flower->lerp_mouth = rr_lerp(flower->lerp_mouth, 5, 20 * delta);
        else if (flower->face_flags & 2)
            flower->lerp_mouth = rr_lerp(flower->lerp_mouth, 8, 20 * delta);
        else
            flower->lerp_mouth = rr_lerp(flower->lerp_mouth, 15, 20 * delta);
    }

    if (rr_simulation_has_player_info(this, entity))
    {
        struct rr_component_player_info *player_info =
            rr_simulation_get_player_info(this, entity);
        if (player_info->camera_fov != player_info->camera_fov_last_tick)
            player_info->fov_adjustment = 1;
        player_info->camera_fov_last_tick = player_info->camera_fov;
        float fov = rr_lerp(RR_BASE_FOV, player_info->camera_fov,
                            player_info->fov_adjustment);
        if (player_info->lerp_camera_fov == 0)
            player_info->lerp_camera_fov = fov;
        if (player_info->lerp_camera_x == 0)
            player_info->lerp_camera_x = player_info->camera_x;
        if (player_info->lerp_camera_y == 0)
            player_info->lerp_camera_y = player_info->camera_y;
        player_info->lerp_camera_fov = rr_lerp(
            player_info->lerp_camera_fov, fov, 15 * delta);
        player_info->lerp_camera_x = rr_lerp(player_info->lerp_camera_x,
                                             player_info->camera_x, 10 * delta);
        player_info->lerp_camera_y = rr_lerp(player_info->lerp_camera_y,
                                             player_info->camera_y, 10 * delta);
    }

    if (rr_simulation_has_health(this, entity))
    {
        struct rr_component_health *health =
            rr_simulation_get_health(this, entity);
        health->damage_animation =
            rr_lerp(health->damage_animation, 0, 5 * delta);
        if (health->flags & 2)
        {
            if (health->damage_animation < 0.25)
                health->damage_animation = 1;
        }
        if (health->lerp_health == 0)
            health->lerp_health = health->health;
        if (health->lerp_prev_health == 0)
            health->lerp_prev_health = health->health;
        health->lerp_health =
            rr_lerp(health->lerp_health, health->health, 20 * delta);
        if (health->health < health->health_last_tick)
            health->prev_health_animation = 0.25;
        else if (health->prev_health_animation > 0)
            health->prev_health_animation -= delta;
        if (health->prev_health_animation <= 0)
            health->lerp_prev_health =
                rr_lerp(health->lerp_prev_health, health->health, 5 * delta);
        health->health_last_tick = health->health;
    }
}

void rr_system_interpolation_tick(struct rr_simulation *simulation, float delta)
{
    struct function_captures captures;
    captures.simulation = simulation;
    captures.delta = delta;
    rr_simulation_for_each_entity(simulation, &captures,
                                  system_interpolation_for_each_function);
    simulation->updated_this_tick = 0;
}
