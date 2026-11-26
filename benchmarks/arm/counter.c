/* Native ARM instruction-slot counting and optional block profiles. */
#include <stdint.h>

typedef struct {
    uint64_t total, jit;
    uint64_t import_start, import_end;
    uint64_t profile_start, profile_end, profile_jit, profile_other;
    uint64_t *profile;
    uint64_t *jit_blocks;
    uint32_t *jit_sizes;
    uint64_t pipeline_setup;
    uint64_t setup_entries[8];
    uint32_t setup_entry_count, setup_return;
    int (*read_register)(void *, int, void *);
    int link_register;
} Counter;

static void count(void *uc, uint64_t address, uint32_t instructions, Counter *c)
{
    uint32_t i;
    if (address == c->setup_return)
        c->setup_return = 0;
    if (!c->setup_return)
        for (i = 0; i < c->setup_entry_count; ++i)
            if (address == c->setup_entries[i])
            {
                c->read_register(uc, c->link_register, &c->setup_return);
                break;
            }
    /* Imported OS operations and the harness vtable stubs are host-emulated. */
    if ((address >= c->import_start && address < c->import_end) ||
        address >= 0x7f000000u)
        return;
    c->total += instructions;
    if (address >= 0x30000000u && address < 0x30800000u)
        c->jit += instructions;
    if (c->setup_return)
        c->pipeline_setup += instructions;
}

void count_instruction(void *uc, uint64_t address, uint32_t size, void *opaque)
{
    (void)size;
    count(uc, address, 1, opaque);
}

/* The benchmark ELFs and their JIT use ARM mode throughout. Unicorn splits
 * blocks at branches, so size / 4 includes failed conditional instructions. */
void count_block(void *uc, uint64_t address, uint32_t size, void *opaque)
{
    count(uc, address, size / 4, opaque);
}

/* Block counts are attributed to their entry address. A null profile leaves
 * only aggregate counting enabled. */
void profile_block(void *uc, uint64_t address, uint32_t size, void *opaque)
{
    Counter *c = opaque;
    count_block(uc, address, size, opaque);
    if (!c->profile ||
        (address >= c->import_start && address < c->import_end) ||
        address >= 0x7f000000u)
        return;
    if (address >= c->profile_start && address < c->profile_end)
        c->profile[(address - c->profile_start) / 4] += size / 4;
    else if (address >= 0x30000000u && address < 0x30800000u) {
        c->profile_jit += size / 4;
        if (c->jit_blocks) {
            uint32_t index = (uint32_t)(address - 0x30000000u) / 4;
            c->jit_blocks[index] += size / 4;
            c->jit_sizes[index] = size;
        }
    } else
        c->profile_other += size / 4;
}
