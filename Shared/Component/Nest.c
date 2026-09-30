
#include <Shared/Component/Nest.h>

#include <string.h>

#include <Shared/pb.h>

#ifdef RR_SERVER
#include <Server/EntityAllocation.h>
#include <Shared/Component/Ai.h>
#include <Shared/Component/Mob.h>
#include <Shared/Component/Physical.h>
#include <Shared/Component/Relations.h>
#include <Shared/SimulationCommon.h>
#include <Shared/StaticData.h>
#endif

#define RR_BEEHIVE_POST_RELEASE_CAP 15

#define FOR_EACH_PUBLIC_FIELD X(is_beehive_post, uint8)

enum
{
    state_flags_is_beehive_post = 0b000001,
    state_flags_all = 0b000001
};

void rr_component_nest_init(struct rr_component_nest *this,
                            struct rr_simulation *simulation)
{
    memset(this, 0, sizeof *this);
}

void rr_component_nest_free(struct rr_component_nest *this,
                            struct rr_simulation *simulation)
{
#ifdef RR_SERVER
    if (!this->is_beehive_post)
        return;
    uint32_t release_count = this->bees_spawned / 2;
    if (release_count > RR_BEEHIVE_POST_RELEASE_CAP)
        release_count = RR_BEEHIVE_POST_RELEASE_CAP;
    if (release_count == 0)
        return;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, this->parent_id);
    struct rr_component_relations *relations =
        rr_simulation_get_relations(simulation, this->parent_id);
    for (uint32_t i = 0; i < release_count; ++i)
    {
        EntityIdx mob_id = rr_simulation_alloc_mob(
            simulation, physical->arena, physical->x, physical->y,
            rr_mob_id_fighter_bee, this->rarity, relations->team);
        struct rr_component_relations *mob_relations =
            rr_simulation_get_relations(simulation, mob_id);
        rr_component_relations_set_team(mob_relations, relations->team);
        rr_component_relations_set_owner(mob_relations, relations->owner);
        rr_component_relations_update_root_owner(simulation, mob_relations);
        rr_simulation_get_ai(simulation, mob_id)->ai_type = rr_ai_type_aggro;
        rr_component_mob_set_player_spawned(
            rr_simulation_get_mob(simulation, mob_id), 1);
    }
#endif
}

#ifdef RR_SERVER
void rr_component_nest_write(struct rr_component_nest *this,
                             struct proto_bug *encoder, int is_creation,
                             struct rr_component_player_info *client)
{
    uint64_t state = this->protocol_state | (state_flags_all * is_creation);
    proto_bug_write_varuint(encoder, state, "nest component state");
#define X(NAME, TYPE) RR_ENCODE_PUBLIC_FIELD(NAME, TYPE);
    FOR_EACH_PUBLIC_FIELD
#undef X
}

RR_DEFINE_PUBLIC_FIELD(nest, uint8_t, is_beehive_post)
#endif

#ifdef RR_CLIENT
void rr_component_nest_read(struct rr_component_nest *this,
                            struct proto_bug *encoder)
{
    uint64_t state = proto_bug_read_varuint(encoder, "nest component state");
#define X(NAME, TYPE) RR_DECODE_PUBLIC_FIELD(NAME, TYPE);
    FOR_EACH_PUBLIC_FIELD
#undef X
}
#endif
