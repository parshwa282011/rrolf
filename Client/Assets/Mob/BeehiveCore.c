
#include <math.h>

#include <Client/Assets/Render.h>

#include <Client/Renderer/Renderer.h>

static void flat_top_hex_path(struct rr_renderer *renderer, float cx,
                              float cy, float radius)
{
    for (uint32_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float x = cx + radius * cosf(angle);
        float y = cy + radius * sinf(angle);
        if (i == 0)
            rr_renderer_move_to(renderer, x, y);
        else
            rr_renderer_line_to(renderer, x, y);
    }
}

// A 7-hex cluster (one center hex ringed by 6 petal hexes) so the core
// visually reads as bigger/more important than a single honeycomb wall
// tile.
void rr_beehive_core_draw(struct rr_renderer *renderer)
{
    const float petal_radius = 70.0f;
    const float petal_distance = 118.0f;

    rr_renderer_set_fill(renderer, 0xff6b2a12);
    for (uint32_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float cx = petal_distance * cosf(angle);
        float cy = petal_distance * sinf(angle);
        rr_renderer_begin_path(renderer);
        flat_top_hex_path(renderer, cx, cy, petal_radius);
        rr_renderer_fill(renderer);
    }

    rr_renderer_set_fill(renderer, 0xffa8461c);
    for (uint32_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float cx = petal_distance * cosf(angle);
        float cy = petal_distance * sinf(angle);
        rr_renderer_begin_path(renderer);
        flat_top_hex_path(renderer, cx, cy, petal_radius * 0.82f);
        rr_renderer_fill(renderer);
    }

    rr_renderer_set_fill(renderer, 0xff4a1a0a);
    rr_renderer_begin_path(renderer);
    flat_top_hex_path(renderer, 0, 0, 100.0f);
    rr_renderer_fill(renderer);

    rr_renderer_set_fill(renderer, 0xffe0862f);
    rr_renderer_begin_path(renderer);
    flat_top_hex_path(renderer, 0, 0, 78.0f);
    rr_renderer_fill(renderer);

    rr_renderer_set_fill(renderer, 0xfff5c542);
    rr_renderer_begin_path(renderer);
    flat_top_hex_path(renderer, 0, 0, 40.0f);
    rr_renderer_fill(renderer);
}
