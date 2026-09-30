
#pragma once

#include <Shared/Component/Common.h>
#include <Shared/Entity.h>
#include <Shared/Utilities.h>

struct rr_simulation;
struct proto_bug;
RR_SERVER_ONLY(struct rr_component_player_info;)

struct rr_component_nest
{
    EntityIdx parent_id;
    RR_SERVER_ONLY(uint32_t protocol_state;)
    RR_SERVER_ONLY(uint8_t rarity;)
    RR_SERVER_ONLY(float global_rotation;)
    RR_SERVER_ONLY(uint32_t rotation_pos;)
    RR_SERVER_ONLY(uint32_t rotation_count;)
    uint8_t is_beehive_post;
    RR_SERVER_ONLY(uint32_t bees_spawned;)
};

void rr_component_nest_init(struct rr_component_nest *, struct rr_simulation *);
void rr_component_nest_free(struct rr_component_nest *, struct rr_simulation *);

RR_SERVER_ONLY(void rr_component_nest_write(struct rr_component_nest *,
                                            struct proto_bug *, int,
                                            struct rr_component_player_info *);)
RR_CLIENT_ONLY(void rr_component_nest_read(struct rr_component_nest *,
                                           struct proto_bug *);)

RR_DECLARE_PUBLIC_FIELD(nest, uint8_t, is_beehive_post)
