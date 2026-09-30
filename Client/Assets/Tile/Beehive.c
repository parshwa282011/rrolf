
#include <math.h>

#include <Client/Assets/Render.h>

#include <Client/Renderer/Renderer.h>

static void bh_hex_path(struct rr_renderer *renderer, float cx, float cy,
                        float radius)
{
    for (uint8_t i = 0; i < 6; ++i)
    {
        float angle = (float)i * (M_PI / 3.0);
        float x = cx + radius * cosf(angle), y = cy + radius * sinf(angle);
        if (i == 0)
            rr_renderer_move_to(renderer, x, y);
        else
            rr_renderer_line_to(renderer, x, y);
    }
}

static void draw_beehive_tile(struct rr_renderer *renderer,
                              uint32_t base_color, uint32_t alt_color,
                              uint32_t line_color, float hex_size,
                              float phase)
{
    rr_renderer_set_fill(renderer, base_color);
    rr_renderer_begin_path(renderer);
    rr_renderer_move_to(renderer, -128, -128);
    rr_renderer_line_to(renderer, 128, -128);
    rr_renderer_line_to(renderer, 128, 128);
    rr_renderer_line_to(renderer, -128, 128);
    rr_renderer_fill(renderer);
    rr_renderer_clip(renderer);

    float col_spacing = hex_size * 1.5f;
    float row_spacing = hex_size * sqrtf(3.0f);
    int32_t col_count = (int32_t)(200.0f / col_spacing) + 2;
    for (int32_t col = -col_count; col <= col_count; ++col)
    {
        float cx = col * col_spacing + phase;
        if (cx < -160.0f || cx > 160.0f)
            continue;
        int32_t row_count = (int32_t)(200.0f / row_spacing) + 2;
        for (int32_t row = -row_count; row <= row_count; ++row)
        {
            float cy = row * row_spacing +
                       (((col & 1) != 0) ? row_spacing * 0.5f : 0.0f);
            if (cy < -160.0f || cy > 160.0f)
                continue;
            uint8_t alt = ((col + row) % 3 + 3) % 3 == 0;
            rr_renderer_set_fill(renderer, alt ? alt_color : base_color);
            rr_renderer_begin_path(renderer);
            bh_hex_path(renderer, cx, cy, hex_size * 0.92f);
            rr_renderer_fill(renderer);
            rr_renderer_set_stroke(renderer, line_color);
            rr_renderer_set_line_width(renderer, 2.5f);
            rr_renderer_begin_path(renderer);
            bh_hex_path(renderer, cx, cy, hex_size * 0.92f);
            rr_renderer_stroke(renderer);
        }
    }
}

void rr_bh_tile_1_draw(struct rr_renderer *renderer)
{
    draw_beehive_tile(renderer, 0xffcf9a3d, 0xffe0b354, 0xff8a5a1a, 28.0f,
                      0.0f);
}

void rr_bh_tile_2_draw(struct rr_renderer *renderer)
{
    draw_beehive_tile(renderer, 0xffc48f36, 0xffd6a548, 0xff7d4f16, 26.0f,
                      14.0f);
}

void rr_bh_tile_3_draw(struct rr_renderer *renderer)
{
    draw_beehive_tile(renderer, 0xffd6a545, 0xffe8bd63, 0xff8f5c1e, 30.0f,
                      -9.0f);
}
