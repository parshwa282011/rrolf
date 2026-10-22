
#include <math.h>

#include <Client/Assets/Render.h>

#include <Client/Renderer/Renderer.h>

// the points of a hexagon centered on the origin; closed, so it can be stroked
static void post_hex_path(struct rr_renderer *renderer, float radius)
{
    for (uint8_t i = 0; i <= 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float x = radius * cosf(angle), y = radius * sinf(angle);
        if (i == 0)
            rr_renderer_move_to(renderer, x, y);
        else
            rr_renderer_line_to(renderer, x, y);
    }
}

// The beehive post: a hexagonal hive with honey pooled in the center. It is
// drawn for a radius of 250 (the post's size in the world is scaled to match),
// so the outer hexagon just fits inside the post's circle.
void rr_beehive_post_draw(struct rr_renderer *renderer)
{
    rr_renderer_set_line_join(renderer, 1);
    rr_renderer_set_line_cap(renderer, 1);

    // outer wall of the hive
    rr_renderer_set_fill(renderer, 0xffb8873a);
    rr_renderer_set_stroke(renderer, 0xff6b4a1a);
    rr_renderer_set_line_width(renderer, 14);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 235);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);

    // lighter wax ring
    rr_renderer_set_fill(renderer, 0xffd9a95c);
    rr_renderer_set_stroke(renderer, 0xff8a651c);
    rr_renderer_set_line_width(renderer, 6);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 188);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);

    // the six walls between the cells around the honey
    rr_renderer_begin_path(renderer);
    for (uint8_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        rr_renderer_move_to(renderer, 132 * cosf(angle), 132 * sinf(angle));
        rr_renderer_line_to(renderer, 188 * cosf(angle), 188 * sinf(angle));
    }
    rr_renderer_stroke(renderer);

    // the honey: an amber edge, then the bright pool
    rr_renderer_set_fill(renderer, 0xffe0862f);
    rr_renderer_set_stroke(renderer, 0xff8a651c);
    rr_renderer_set_line_width(renderer, 6);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 132);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);
    rr_renderer_set_fill(renderer, 0xfff5c542);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 104);
    rr_renderer_fill(renderer);

    // a shine on the honey
    rr_renderer_set_fill(renderer, 0x99fff3b0);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, -30, -34, 24);
    rr_renderer_fill(renderer);
}
