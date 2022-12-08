#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "list.h"

#define PROG_NAME "csgo_linux64"

#define LINE_LEN  20

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD  "grep '/client_client.so' maps | head -1 | cut -d'-' -f1"

#define PATH_LEN  64
#define PTRSIZE sizeof(uintptr_t)

/* For wireframe WH */
#define INSN_OFFSET             0x7f1463
#define INSN_OPERAND_OFFSET     0x2

/* For glow WH */
#define GLOWOBJMGR_OFFSET       0x2b9cf80
#define ENTITYLIST_OFFSET       0x232e7d8
#define GLOWOBJDEF_SIZE         64
#define ENTITY_LIST_ITEM_SIZE   32
#define ENTITY_OFFSET           16

/* Glow entry info */
#define GLOW_ENTRY_SIZE         64
#define GLOW_ENTRY_IN_USE       -2

/* Entity field offsets */
#define ENT_HEALTH_OFFSET       0x138
#define ENT_TEAM_OFFSET         0x12c
#define TEAM_TERRORISTS         2
#define TEAM_CT                 3

#define handle_error_en(en, msg) \
    do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

pthread_attr_t detached_attr;

struct GlowObjectDefinition_t
{
    int m_nNextFreeSlot;
    uintptr_t m_pEntity;
    float m_vGlowColorX;
    float m_vGlowColorY;
    float m_vGlowColorZ;
    float m_vGlowAlpha;
    unsigned char m_bGlowAlphaCappedByRenderAlpha;
    unsigned char padding[3];
    float m_flGlowAlphaFunctionOfMaxVelocity;
    float m_flGlowAlphaMax;
    float m_flGlowPulseOverdrive;
    unsigned char m_renderWhenOccluded;
    unsigned char m_renderWhenUnoccluded;
    int m_nRenderStyle;
    int m_nSplitScreenSlot;
};

struct Entity
{
    uintptr_t base;
    int team;
    int health;
    /* int glow_list_idx; */

    list_link_t entity_link;
};

struct hacked_glow_obj
{
    uintptr_t glow_obj_addr;
    struct GlowObjectDefinition_t glow_obj_def;
    struct Entity *entity;
};


pid_t csgo_pid;
uintptr_t client_lib_addr;
uintptr_t s_GlowObjectManager;
uintptr_t entityListProbably;
uintptr_t m_GlowObjectDefinitions;
list_t entities;

int read_from_proc(uintptr_t addr_to_read, void *buffer, size_t len)
{
    struct iovec local = {};
    struct iovec remote = {};
    int errsv = 0;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_read;
    remote.iov_len = local.iov_len;

    nread = process_vm_readv(csgo_pid, &local, 1, &remote, 1, NULL);

    if (nread != local.iov_len)
    {
        handle_error("process_vm_readv");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int write_to_proc(uintptr_t addr_to_write, void *buffer, size_t len)
{
    struct iovec local = {};
    struct iovec remote = {};
    int errsv = 0;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_write;
    remote.iov_len = local.iov_len;

    nread = process_vm_writev(csgo_pid, &local, 1, &remote, 1, 0);

    if (nread != local.iov_len)
    {
        handle_error("process_vm_readv");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void print_entity(struct Entity *obj)
{
    printf("base: %#lx\n", obj->base);
    printf("team: %d\n", obj->team);
    printf("health: %d\n", obj->health);
}

void print_glow_obj_def(struct GlowObjectDefinition_t *obj)
{
    printf("m_nNextFreeSlot: %d\n", obj->m_nNextFreeSlot);
    printf("m_pEntity: %#lx\n", obj->m_pEntity);
    printf("m_vGlowColorX: %.3f\n", obj->m_vGlowColorX);
    printf("m_vGlowColorY: %.3f\n", obj->m_vGlowColorY);
    printf("m_vGlowColorZ: %.3f\n", obj->m_vGlowColorZ);
    printf("m_vGlowAlpha: %.3f\n", obj->m_vGlowAlpha);
    printf("m_vGlowAlphaMax: %.3f\n", obj->m_flGlowAlphaMax);
    printf("m_renderWhenOccluded: %d\n", obj->m_renderWhenOccluded);
    printf("m_renderWhenUnccluded: %d\n", obj->m_renderWhenUnoccluded);
}


pid_t get_csgo_pid(void)
{
    pid_t pid;
    char line[LINE_LEN];
    FILE *cmd_stream;

    /* open stream to pidof */
    if ((cmd_stream = popen(PIDOF_CMD, "r")) == NULL)
        handle_error("popen");

    /* read output and process csgo_pid */
    if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
        handle_error_en(EINVAL, "fgets");

    if ((pid = strtoul(line, NULL, /*base */10)) == ULONG_MAX)
        handle_error("strtoul");

    /* cleanup */
    if (pclose(cmd_stream) == -1)
        handle_error("pclose");

    return pid;
}

uintptr_t get_client_lib_addr(void)
{
    uintptr_t client_lib_addr;
    char line[LINE_LEN];
    FILE *cmd_stream;

    /* open stream to grep command */
    if ((cmd_stream = popen(BASE_CMD, "r")) == NULL)
        handle_error("popen");

    /* read output and process csgo_pid */
    if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
        handle_error_en(EINVAL, "fgets");

    if ((client_lib_addr = strtoull(line, NULL, /*base */16)) == ULLONG_MAX)
        handle_error("strtoul");

    /* cleanup */
    if (pclose(cmd_stream) == -1)
        handle_error("pclose");

    return client_lib_addr;
}

void print_hex_buf(unsigned char *buf, int sz)
{
    int i;
    for (i = 0; i < sz; i++)
    {
        if (i > 0) printf(":");
        printf("%02x", buf[i]);
    }
    printf("\n");
}

unsigned long unpack(unsigned char *buf, int size)
{
    unsigned long res = 0;
    for (int i = 0; i < size; i++) {
        res += buf[i] << (i * size);
    }
    return res;
}

uintptr_t get_glowobj_def_list(uintptr_t s_GlowObjectManager)
{

    unsigned char buf[PTRSIZE + 1];
    memset(&buf, 0, PTRSIZE + 1);
    read_from_proc(s_GlowObjectManager, buf, PTRSIZE);
    /* print_hex_buf(buf, PTRSIZE); */
    return unpack(buf, sizeof(uintptr_t));

}

struct Entity *get_new_entity()
{
    struct Entity *entity = malloc(sizeof(struct Entity));
    memset(entity, 0, sizeof(struct Entity));
    list_link_init(&entity->entity_link);
    return entity;
}

int get_entity_int_field(struct Entity *entity, int offset)
{
    unsigned char buf[sizeof(int) + 1];
    int value;
    read_from_proc(entity->base + offset, buf, sizeof(int));
    value = unpack(buf, sizeof(int));
    return value;
}

void get_entities(void)
{
    uintptr_t entity_ptr = -1;
    uintptr_t cur_entity_entry = entityListProbably;
    unsigned char buf[PTRSIZE + 1];
    struct Entity *entity;
    /* print_hex_buf(buf, PTRSIZE); */
    while (entity_ptr) {
        memset(&buf, 0, PTRSIZE + 1);
        read_from_proc(cur_entity_entry + ENTITY_OFFSET, buf, PTRSIZE);
        entity_ptr = unpack(buf, sizeof(uintptr_t));
        if (entity_ptr) {
            printf("Found entity at: %#lx\n", entity_ptr);
            // TODO cleanup
            entity = get_new_entity();
            entity->base = entity_ptr;
            entity->health = get_entity_int_field(entity, ENT_HEALTH_OFFSET);
            entity->team = get_entity_int_field(entity, ENT_TEAM_OFFSET);
            // TODO get other fields
            list_insert_head(&entities, &entity->entity_link);

            print_entity(entity);
        }
        cur_entity_entry += ENTITY_LIST_ITEM_SIZE;
    }
}

void get_global_addresses(void)
{
    /* get base address of client_client.so library */
    client_lib_addr = get_client_lib_addr();
    printf("Library client_client.so located at %#lx\n", client_lib_addr);

    s_GlowObjectManager = client_lib_addr + GLOWOBJMGR_OFFSET;
    printf("s_GlowObjectManager located at %#lx\n", s_GlowObjectManager);
    entityListProbably = client_lib_addr + ENTITYLIST_OFFSET;
    printf("Entity list located at %#lx\n", entityListProbably);
    m_GlowObjectDefinitions = get_glowobj_def_list(s_GlowObjectManager);
    printf("GlowObjectDefinitions list located at %#lx\n", m_GlowObjectDefinitions);

    get_entities();
}

struct Entity *get_entity_for_glow_entry(struct GlowObjectDefinition_t *glow_obj)
{
    struct Entity *entity;
    list_iterate_begin(&entities, entity, struct Entity, entity_link) {

        if (entity->base == glow_obj->m_pEntity) {
            return entity;
        }

    } list_iterate_end();

    return NULL;
}

void *write_glow_obj(void *data)
{

    struct hacked_glow_obj *hacked_obj = (struct hacked_glow_obj *) data;
    if (hacked_obj->entity->team == TEAM_CT) {
        hacked_obj->glow_obj_def.m_vGlowColorZ = 1.0f;
    } else if (hacked_obj->entity->team == TEAM_CT) {
        hacked_obj->glow_obj_def.m_vGlowColorX = 1.0f;
    }
    hacked_obj->glow_obj_def.m_vGlowAlpha = 1.0f;
    hacked_obj->glow_obj_def.m_flGlowAlphaMax = 1.0f;
    hacked_obj->glow_obj_def.m_renderWhenOccluded = 1;

    puts("Modified glow object:\n");
    print_hex_buf((unsigned char *)&hacked_obj->glow_obj_def, GLOWOBJDEF_SIZE);
    print_glow_obj_def(&hacked_obj->glow_obj_def);
    while(1) {
        write_to_proc(hacked_obj->glow_obj_addr, &hacked_obj->glow_obj_def, GLOWOBJDEF_SIZE);
    }

    pthread_exit(NULL);
}

void apply_glows(void)
{
    struct GlowObjectDefinition_t glow_obj_def;
    struct hacked_glow_obj *hacked_obj;
    pthread_t thread;
    struct Entity *entity = NULL;

    int nextFreeSlot = GLOW_ENTRY_IN_USE;
    uintptr_t cur_glow_entry = m_GlowObjectDefinitions;

    while (nextFreeSlot == GLOW_ENTRY_IN_USE) {
        memset((void *)&glow_obj_def, 0, GLOWOBJDEF_SIZE);
        read_from_proc(cur_glow_entry, &glow_obj_def, GLOWOBJDEF_SIZE);
        nextFreeSlot = glow_obj_def.m_nNextFreeSlot;

        entity = get_entity_for_glow_entry(&glow_obj_def);
        if (nextFreeSlot == GLOW_ENTRY_IN_USE && entity) {
            puts("Found glow entry for entity:");
            /* print_hex_buf((unsigned char *)&glow_obj_def, GLOWOBJDEF_SIZE); */
            print_glow_obj_def(&glow_obj_def);
            // TODO cleanup
            hacked_obj = malloc(sizeof(struct hacked_glow_obj));
            memset(hacked_obj, 0, sizeof(struct hacked_glow_obj));
            hacked_obj->glow_obj_addr = cur_glow_entry;
            memcpy((void *) &hacked_obj->glow_obj_def, &glow_obj_def, sizeof(struct GlowObjectDefinition_t));
            hacked_obj->entity = entity;
            pthread_create(&thread, &detached_attr, &write_glow_obj, hacked_obj);
        }

        cur_glow_entry += GLOW_ENTRY_SIZE;
    }

    //TODO integrate interface
    while (1);
}

int main(void)
{
    char path[PATH_LEN];

    list_init(&entities);
    pthread_attr_init(&detached_attr);
    pthread_attr_setdetachstate(&detached_attr, PTHREAD_CREATE_DETACHED);

    /* get pid of csgo process */
    csgo_pid = get_csgo_pid();
    printf("Got %s csgo_pid: %d\n", PROG_NAME, csgo_pid);

    /* change working directory to make accessing files easy */
    if (snprintf(path, PATH_LEN, "/proc/%d", csgo_pid) < 0)
        handle_error_en(EINVAL, "snprintf");

    if (chdir(path) == -1)
        handle_error("chdir");

    get_global_addresses();
    apply_glows();

}
