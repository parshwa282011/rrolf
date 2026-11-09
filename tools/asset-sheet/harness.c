// Native stand-in for the canvas renderer. It compiles the REAL draw code of the
// client (Client/Assets/RenderMobs.c, RenderPetals.c, Mob/*.c, ...) against a
// renderer that, instead of drawing, writes every instruction as JSON. Loops,
// helper functions and maths in the draw code all run for real, nothing is parsed.
// gen-asset-sheet.py builds this and turns its output into the html sheet.
//
// The instruction numbers are the ones of Client/Renderer/Wasm.c, so the page
// can replay them with the same switch.

#define _GNU_SOURCE
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Client/Assets/Render.h>
#include <Client/Assets/RenderFunctions.h>
#include <Client/Renderer/Renderer.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>

uint8_t g_poor_eqm = 0;

extern struct rr_renderer_spritesheet mob_sprites[rr_mob_id_max];
extern struct rr_renderer_spritesheet friendly_mob_sprites[2];
void rr_renderer_mob_cache_init();
void rr_renderer_draw_mob(struct rr_renderer *, uint8_t, float, float, uint8_t);
void rr_renderer_draw_petal(struct rr_renderer *, uint8_t, uint8_t);
void rr_renderer_draw_static_petal(struct rr_renderer *, uint8_t, uint8_t,
                                   uint8_t);

// ---------------------------------------------------------------- recording
static FILE *rec = NULL;
static char *rec_buf = NULL;
static size_t rec_len = 0;
static uint32_t rec_count = 0;
static uint32_t skipped_images = 0;

static void rec_begin(void)
{
    rec = open_memstream(&rec_buf, &rec_len);
    rec_count = 0;
    fputc('[', rec);
}

static char *rec_end(void)
{
    fputc(']', rec);
    fclose(rec);
    rec = NULL;
    return rec_buf; // freed by the caller
}

static void op(int type, int n, ...)
{
    if (rec == NULL)
        return;
    if (rec_count++)
        fputc(',', rec);
    fprintf(rec, "[%d", type);
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    for (int i = 0; i < n; ++i)
        fprintf(rec, ",%.5g", __builtin_va_arg(ap, double));
    __builtin_va_end(ap);
    fputc(']', rec);
}

static void op_text(int type, float x, float y, char const *c)
{
    if (rec == NULL)
        return;
    if (rec_count++)
        fputc(',', rec);
    fprintf(rec, "[%d,%.5g,%.5g,\"", type, x, y);
    for (; *c; ++c)
    {
        if (*c == '"' || *c == '\\')
            fputc('\\', rec);
        if ((unsigned char)*c >= 32)
            fputc(*c, rec);
    }
    fprintf(rec, "\"]");
}

// ------------------------------------------- the renderer (same as Wasm.c)
static void update_if_transformed(struct rr_renderer *this)
{
    if (this->matrix_moddified)
    {
        this->matrix_moddified = 0;
        float *m = this->state.transform_matrix;
        op(9, 6, m[0], m[1], m[2], m[3], m[4], m[5]);
    }
}

void rr_renderer_init(struct rr_renderer *this)
{
    memset(this, 0, sizeof(*this));
    this->state.transform_matrix[0] = 1;
    this->state.transform_matrix[4] = 1;
}

void rr_renderer_set_dimensions(struct rr_renderer *this, float w, float h)
{
    this->width = w;
    this->height = h;
}

static void colour(struct rr_renderer *this, int type, uint32_t c)
{
    float a = this->state.filter.amount;
    uint32_t fc = this->state.filter.color;
    uint8_t red = rr_fclamp(
        (((c >> 16) & 255) * (1 - a) + ((fc >> 16) & 255) * a), 0, 255);
    uint8_t green =
        rr_fclamp((((c >> 8) & 255) * (1 - a) + ((fc >> 8) & 255) * a), 0, 255);
    uint8_t blue =
        rr_fclamp((((c >> 0) & 255) * (1 - a) + ((fc >> 0) & 255) * a), 0, 255);
    op(type, 4, (double)red, (double)green, (double)blue,
       (double)(c >> 24) / 255.0);
}

void rr_renderer_set_fill(struct rr_renderer *this, uint32_t c)
{
    colour(this, 0, c);
}
void rr_renderer_set_stroke(struct rr_renderer *this, uint32_t c)
{
    colour(this, 1, c);
}
void rr_renderer_set_line_width(struct rr_renderer *this, float w)
{
    op(2, 1, w);
}
void rr_renderer_set_text_size(struct rr_renderer *this, float s)
{
    op(3, 1, s);
}
void rr_renderer_set_global_alpha(struct rr_renderer *this, float a)
{
    this->state.global_alpha = a;
    op(4, 1, a);
}
void rr_renderer_set_line_cap(struct rr_renderer *this, uint8_t l)
{
    op(5, 1, (double)l);
}
void rr_renderer_set_line_join(struct rr_renderer *this, uint8_t l)
{
    op(6, 1, (double)l);
}
void rr_renderer_set_text_align(struct rr_renderer *this, uint8_t l)
{
    op(7, 1, (double)l);
}
void rr_renderer_set_text_baseline(struct rr_renderer *this, uint8_t l)
{
    op(8, 1, (double)l);
}
void rr_renderer_update_transform(struct rr_renderer *this)
{
    this->matrix_moddified = 1;
}
void rr_renderer_save(struct rr_renderer *this)
{
    op(10, 0);
}
void rr_renderer_restore(struct rr_renderer *this)
{
    this->matrix_moddified = 1;
    op(11, 0);
}
void rr_renderer_begin_path(struct rr_renderer *this)
{
    update_if_transformed(this);
    op(12, 0);
}
void rr_renderer_move_to(struct rr_renderer *this, float x, float y)
{
    update_if_transformed(this);
    op(13, 2, x, y);
}
void rr_renderer_line_to(struct rr_renderer *this, float x, float y)
{
    update_if_transformed(this);
    op(14, 2, x, y);
}
void rr_renderer_quadratic_curve_to(struct rr_renderer *this, float x1,
                                    float y1, float x, float y)
{
    update_if_transformed(this);
    op(15, 4, x1, y1, x, y);
}
void rr_renderer_bezier_curve_to(struct rr_renderer *this, float x1, float y1,
                                 float x2, float y2, float x, float y)
{
    update_if_transformed(this);
    op(16, 6, x1, y1, x2, y2, x, y);
}
void rr_renderer_partial_arc(struct rr_renderer *this, float x, float y,
                             float r, float sa, float ea, uint8_t ccw)
{
    update_if_transformed(this);
    op(17, 6, x, y, r, sa, ea, (double)ccw);
}
void rr_renderer_ellipse(struct rr_renderer *this, float x, float y, float rx,
                         float ry)
{
    update_if_transformed(this);
    op(18, 4, x, y, rx, ry);
}
void rr_renderer_rect(struct rr_renderer *this, float x, float y, float w,
                      float h)
{
    update_if_transformed(this);
    op(19, 4, x, y, w, h);
}
// the cached images are not used here, the draw code is run directly
void rr_renderer_draw_clipped_image(struct rr_renderer *this,
                                    struct rr_renderer *image, float sx,
                                    float sy, float sw, float sh, float dx,
                                    float dy)
{
    ++skipped_images;
}
void rr_renderer_draw_translated_image(struct rr_renderer *this,
                                       struct rr_renderer *image, float x,
                                       float y)
{
    ++skipped_images;
}
void rr_renderer_draw_image(struct rr_renderer *this, struct rr_renderer *image)
{
    ++skipped_images;
}
void rr_renderer_draw_svg(struct rr_renderer *this, char *svg, float x, float y)
{
    ++skipped_images;
}
void rr_renderer_fill_rect(struct rr_renderer *this, float x, float y, float w,
                           float h)
{
    update_if_transformed(this);
    op(21, 4, x, y, w, h);
}
void rr_renderer_stroke_rect(struct rr_renderer *this, float x, float y,
                             float w, float h)
{
    update_if_transformed(this);
    op(22, 4, x, y, w, h);
}
void rr_renderer_fill(struct rr_renderer *this)
{
    update_if_transformed(this);
    op(23, 0);
}
void rr_renderer_stroke(struct rr_renderer *this)
{
    update_if_transformed(this);
    op(24, 0);
}
void rr_renderer_clip(struct rr_renderer *this)
{
    update_if_transformed(this);
    op(25, 0);
}
void rr_renderer_clip2(struct rr_renderer *this)
{
    update_if_transformed(this);
    op(26, 0);
}
void rr_renderer_fill_text(struct rr_renderer *this, char const *c, float x,
                           float y)
{
    update_if_transformed(this);
    op_text(27, x, y, c);
}
void rr_renderer_stroke_text(struct rr_renderer *this, char const *c, float x,
                             float y)
{
    update_if_transformed(this);
    op_text(28, x, y, c);
}
float rr_renderer_get_text_size(char const *c)
{
    return strlen(c) * 0.55f;
}
void rr_renderer_set_global_composite_operation(struct rr_renderer *this,
                                                uint8_t o)
{
    op(30, 1, (double)o);
}
void rr_renderer_execute_instructions() {}
uint32_t rr_renderer_get_op_size() { return 0; }
void rr_renderer_reset_instruction_queue() {}

// -------------------------------------------------------------- sprite sheets
// Replaces the one of Client/Renderer/Common.c (it is compiled with the real
// name changed). It remembers every sprite of every sheet and counts how often
// the composite of a mob draws each of them, to find the ones that never are.
#define MAX_SLOTS 16
typedef void (*draw_fn)(struct rr_renderer *);
struct sheet_info
{
    void (*mask)(struct rr_renderer *);
    uint32_t size;
    draw_fn orig[MAX_SLOTS];
    float w[MAX_SLOTS];
    float h[MAX_SLOTS];
    uint32_t used[MAX_SLOTS];
    uint32_t used_alt[MAX_SLOTS];
    uint8_t known;
};
static struct sheet_info sheets[rr_mob_id_max + 2];
static uint32_t current_sheet = 0;

#define TRAMP(k)                                                               \
    static void tramp##k(struct rr_renderer *r)                                \
    {                                                                          \
        ++sheets[current_sheet].used[k];                                       \
        sheets[current_sheet].orig[k](r);                                      \
    }
TRAMP(0) TRAMP(1) TRAMP(2) TRAMP(3) TRAMP(4) TRAMP(5) TRAMP(6) TRAMP(7)
TRAMP(8) TRAMP(9) TRAMP(10) TRAMP(11) TRAMP(12) TRAMP(13) TRAMP(14) TRAMP(15)
static draw_fn const tramps[MAX_SLOTS] = {
    tramp0, tramp1, tramp2,  tramp3,  tramp4,  tramp5,  tramp6,  tramp7,
    tramp8, tramp9, tramp10, tramp11, tramp12, tramp13, tramp14, tramp15};

void rr_renderer_spritesheet_init(struct rr_renderer_spritesheet *spritesheet,
                                  void (*setup)(struct rr_renderer *), ...)
{
    uint32_t index;
    if (spritesheet >= mob_sprites && spritesheet < mob_sprites + rr_mob_id_max)
        index = spritesheet - mob_sprites;
    else
        index = rr_mob_id_max + (spritesheet - friendly_mob_sprites);
    struct sheet_info *s = &sheets[index];
    memset(s, 0, sizeof *s);
    s->known = 1;
    s->mask = setup;
    __builtin_va_list args;
    __builtin_va_start(args, setup);
    while (1)
    {
        float width = __builtin_va_arg(args, int);
        if (width == 0)
            break;
        float height = __builtin_va_arg(args, int);
        struct rr_sprite_bounds *b = &spritesheet->sprites[s->size];
        b->w = width;
        b->h = height;
        b->render = __builtin_va_arg(args, void *);
        s->orig[s->size] = b->render;
        s->w[s->size] = width;
        s->h[s->size] = height;
        b->render = tramps[s->size];
        ++s->size;
    }
    __builtin_va_end(args);
    spritesheet->size = s->size;
}

// only reached with the cache flag, which the sheet never sets
void render_sprite_from_cache(struct rr_renderer *renderer,
                              struct rr_renderer_spritesheet *spritesheet,
                              uint32_t pos)
{
    ++skipped_images;
}

// ----------------------------------------------------------------- the output
static void print_ops(char *ops)
{
    fputs(ops, stdout);
    free(ops);
}

static void identity(struct rr_renderer *r)
{
    rr_renderer_init(r);
    rr_renderer_set_transform(r, 1, 0, 0, 0, 1, 0);
}

// a mob the way the world draws it: before the rarity scale of the mob.
// flags: 4 is what every mob but the head of a centipede gets, 0 is the head
// (RenderMob.c: is_centi_body)
static void composite(struct rr_renderer *r, uint32_t id, float tick,
                      uint8_t flags)
{
    current_sheet = id;
    identity(r);
    if (sheets[id].mask)
        sheets[id].mask(r);
    rr_renderer_draw_mob(r, id, tick, 0.0f, flags);
}

int main(int argc, char **argv)
{
    float tick = argc > 1 ? (float)atof(argv[1]) : 0.0f;
    rr_renderer_mob_cache_init();
    struct rr_renderer r;
    printf("{\"rarities\":[");
    for (uint32_t i = 0; i < rr_rarity_id_max; ++i)
        printf("%s{\"name\":\"%s\",\"color\":%u}", i ? "," : "",
               RR_RARITY_NAMES[i], (unsigned)RR_RARITY_COLORS[i]);
    printf("],\"mobs\":[");
    for (uint32_t id = 0; id < rr_mob_id_max; ++id)
    {
        struct sheet_info *s = &sheets[id];
        printf("%s{\"id\":%u,\"name\":\"%s\",\"radius\":%g,\"min_rarity\":%u,"
               "\"max_rarity\":%u,\"has_sheet\":%u,",
               id ? "," : "", id, RR_MOB_NAMES[id], RR_MOB_DATA[id].radius,
               RR_MOB_DATA[id].min_rarity, RR_MOB_DATA[id].max_rarity,
               s->known);
        memset(s->used, 0, sizeof s->used);
        skipped_images = 0;
        rec_begin();
        if (s->known)
            composite(&r, id, tick, 4);
        printf("\"composite\":");
        print_ops(rec_end());
        printf(",\"images_skipped\":%u,", skipped_images);
        // the same mob drawn with the other flags, when it uses other sprites
        memcpy(s->used_alt, s->used, sizeof s->used);
        memset(s->used, 0, sizeof s->used);
        rec_begin();
        if (s->known)
            composite(&r, id, tick, 0);
        char *alt = rec_end();
        uint8_t differs = memcmp(s->used, s->used_alt, sizeof s->used) != 0;
        printf("\"composite_alt\":");
        if (differs)
            print_ops(alt);
        else
        {
            printf("null");
            free(alt);
        }
        {
            uint32_t tmp[MAX_SLOTS];
            memcpy(tmp, s->used, sizeof tmp);
            memcpy(s->used, s->used_alt, sizeof tmp);
            memcpy(s->used_alt, tmp, sizeof tmp);
        }
        printf(",\"parts\":[");
        for (uint32_t k = 0; k < s->size; ++k)
        {
            current_sheet = id;
            rec_begin();
            identity(&r);
            if (s->mask)
                s->mask(&r);
            s->orig[k](&r);
            printf("%s{\"slot\":%u,\"w\":%g,\"h\":%g,\"used\":%u,"
                   "\"used_alt\":%u,\"ops\":",
                   k ? "," : "", k, s->w[k], s->h[k], s->used[k],
                   s->used_alt[k]);
            print_ops(rec_end());
            printf("}");
        }
        printf("]}");
    }
    printf("],\"petals\":[");
    for (uint32_t id = 0; id < rr_petal_id_max; ++id)
    {
        printf("%s{\"id\":%u,\"name\":\"%s\",\"min_rarity\":%u,\"counts\":[",
               id ? "," : "", id, RR_PETAL_NAMES[id],
               RR_PETAL_DATA[id].min_rarity);
        for (uint32_t i = 0; i < rr_rarity_id_max; ++i)
            printf("%s%u", i ? "," : "", RR_PETAL_DATA[id].count[i]);
        skipped_images = 0;
        rec_begin();
        identity(&r);
        rr_renderer_draw_petal(&r, id, 0);
        printf("],\"raw\":");
        print_ops(rec_end());
        printf(",\"by_rarity\":[");
        for (uint32_t i = 0; i < rr_rarity_id_max; ++i)
        {
            rec_begin();
            identity(&r);
            rr_renderer_draw_static_petal(&r, id, i, 0);
            printf("%s", i ? "," : "");
            print_ops(rec_end());
        }
        printf("],\"images_skipped\":%u}", skipped_images);
    }
    printf("]}\n");
    return 0;
}
