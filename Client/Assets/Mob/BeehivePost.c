
#include <math.h>

#include <Client/Assets/Render.h>

#include <Client/Renderer/Renderer.h>

static void post_hex_path(struct rr_renderer *renderer, float radius)
{
    for (uint8_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float x = radius * cosf(angle), y = radius * sinf(angle);
        if (i == 0)
            rr_renderer_move_to(renderer, x, y);
        else
            rr_renderer_line_to(renderer, x, y);
    }
}

// A wooden stake driven into the ground with a honeycomb lure mounted on
// top - distinct from both the plain twig-bundle nest and the beehive
// core's hex cluster, so it reads as its own structure in the world.
void rr_beehive_post_draw(struct rr_renderer *renderer)
{
    // shadow
    rr_renderer_set_fill(renderer, 0x33000000);
    rr_renderer_begin_path(renderer);
    rr_renderer_arc(renderer, 0, 150, 60);
    rr_renderer_fill(renderer);

    // post
    rr_renderer_set_fill(renderer, 0xff6b4a2a);
    rr_renderer_set_stroke(renderer, 0xff42301a);
    rr_renderer_set_line_width(renderer, 4);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -24, 145);
    rr_renderer_line_to(renderer, -14, -65);
    rr_renderer_line_to(renderer, 14, -65);
    rr_renderer_line_to(renderer, 24, 145);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);

    // crossbeam
    rr_renderer_set_fill(renderer, 0xff7a5731);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -55, -20);
    rr_renderer_line_to(renderer, 55, -32);
    rr_renderer_line_to(renderer, 55, -12);
    rr_renderer_line_to(renderer, -55, 0);
    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);

    // honeycomb lure on top, double-toned like the honeycomb wall art
    struct rr_renderer_context_state state;
    rr_renderer_context_state_init(renderer, &state);
    rr_renderer_translate(renderer, 0, -115);
    rr_renderer_set_fill(renderer, 0xff8a651c);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 78);
    rr_renderer_fill(renderer);
    rr_renderer_set_fill(renderer, 0xffb8873a);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 63);
    rr_renderer_fill(renderer);
    rr_renderer_set_fill(renderer, 0xffd9a95c);
    rr_renderer_begin_path(renderer);
    post_hex_path(renderer, 42);
    rr_renderer_fill(renderer);
    rr_renderer_context_state_free(renderer, &state);
}
