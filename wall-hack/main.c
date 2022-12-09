#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "colorio.h"
#include "list.h"


#define PROG_NAME "csgo_linux64"
#define LIB_NAME  "client_client.so"

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD  "grep '/" LIB_NAME "' maps | head -1 | cut -d'-' -f1"

#define LINE_LEN  20
#define PATH_LEN  64
#define PTR_SIZE  sizeof(uintptr_t)

/* one-byte wall-hack offsets */
#define INSN_OFFSET             0x7f1463
#define INSN_OPERAND_OFFSET     0x2

/* for glow wall-hack */
#define GLOWOBJMGR_OFFSET       0x2b9cf80
#define ENTITYLIST_OFFSET       0x232e7d8
#define GLOWOBJDEF_SIZE         64
#define ENTITY_LIST_ITEM_SIZE   32
#define ENTITY_OFFSET           16

/* glow entry info */
#define GLOW_ENTRY_SIZE         64
#define GLOW_ENTRY_IN_USE       -2

/* entity field offsets */
#define ENT_HEALTH_OFFSET       0x138
#define ENT_TEAM_OFFSET         0x12c
#define TEAM_TERRORISTS         2
#define TEAM_CT                 3


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

pthread_attr_t detached_attr;
pid_t csgo_pid;
uintptr_t client_lib_addr;
uintptr_t s_GlowObjectManager;
uintptr_t entityListProbably;
uintptr_t m_GlowObjectDefinitions;
list_t entities;


void read_from_proc(uintptr_t addr_to_read, void *buffer, size_t len)
{
    struct iovec local;
    struct iovec remote;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_read;
    remote.iov_len = local.iov_len;

    nread = process_vm_readv(csgo_pid, &local, 1, &remote, 1, NULL);

    if (nread != local.iov_len)
        handle_error("process_vm_readv");
}

void write_to_proc(uintptr_t addr_to_write, void *buffer, size_t len)
{
    struct iovec local;
    struct iovec remote;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_write;
    remote.iov_len = local.iov_len;

    nread = process_vm_writev(csgo_pid, &local, 1, &remote, 1, 0);

    if (nread != local.iov_len)
        handle_error("process_vm_readv");
}

void print_entity(struct Entity *obj)
{
    info("base:\t0x%" PRIxPTR "\n", obj->base);
    info("team:\t%d\n", obj->team);
    info("health:\t%d\n", obj->health);
}

void print_glow_obj_def(struct GlowObjectDefinition_t *obj)
{
    info("m_nNextFreeSlot:\t%d\n", obj->m_nNextFreeSlot);
    info("m_pEntity:\t0x%" PRIxPTR "\n", obj->m_pEntity);
    info("m_vGlowColorX:\t%.3f\n", obj->m_vGlowColorX);
    info("m_vGlowColorY:\t%.3f\n", obj->m_vGlowColorY);
    info("m_vGlowColorZ:\t%.3f\n", obj->m_vGlowColorZ);
    info("m_vGlowAlpha:\t%.3f\n", obj->m_vGlowAlpha);
    info("m_vGlowAlphaMax:\t%.3f\n", obj->m_flGlowAlphaMax);
    info("m_renderWhenOccluded:\t%d\n", obj->m_renderWhenOccluded);
    info("m_renderWhenUnccluded:\t%d\n", obj->m_renderWhenUnoccluded);
}

void print_hex_buf(unsigned char *buf, int sz)
{
    int i;

    for (i = 0; i < sz; i++) {
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
    unsigned char buf[PTR_SIZE];

    read_from_proc(s_GlowObjectManager, buf, PTRSIZE);

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

    read_from_proc(entity->base + offset, buf, sizeof(int));

    return unpack(buf, sizeof(int));
}

void get_entities(void)
{
    uintptr_t entity_ptr = -1;
    uintptr_t cur_entity_entry = entityListProbably;
    unsigned char buf[PTR_SIZE];
    struct Entity *entity;

    while (entity_ptr) {
        read_from_proc(cur_entity_entry + ENTITY_OFFSET, buf, PTRSIZE);
        entity_ptr = unpack(buf, sizeof(uintptr_t));

        if (entity_ptr) {
            verbose("found entity at: 0x%" PRIxPTR "\n", entity_ptr);

            entity = get_new_entity();
            entity->base = entity_ptr;
            entity->health = get_entity_int_field(entity, ENT_HEALTH_OFFSET);
            entity->team = get_entity_int_field(entity, ENT_TEAM_OFFSET);
            list_insert_head(&entities, &entity->entity_link);

            print_entity(entity);
        }

        cur_entity_entry += ENTITY_LIST_ITEM_SIZE;
    }
}

void get_global_addresses(void)
{
    set_client_client_base_addr();

    s_GlowObjectManager = client_lib_addr + GLOWOBJMGR_OFFSET;
    verbose("s_GlowObjectManager address: 0x%" PRIxPTR "\n",
            s_GlowObjectManager);

    entityListProbably = client_lib_addr + ENTITYLIST_OFFSET;
    verbose("entity list address: 0x%" PRIxPTR "\n", entityListProbably);

    m_GlowObjectDefinitions = get_glowobj_def_list(s_GlowObjectManager);
    printf("GlowObjectDefinitions list address: 0x%" PRIxPTR "\n",
            m_GlowObjectDefinitions);

    get_entities();
}

struct Entity *get_glow_entry_entity(struct GlowObjectDefinition_t *glow_obj)
{
    struct Entity *entity = NULL;

    list_iterate_begin(&entities, entity, struct Entity, entity_link) {

        if (entity->base == glow_obj->m_pEntity)
            break;

    } list_iterate_end();

    return entity;
}

void *write_glow_obj(void *data)
{
    uintptr_t cur_entity_addr;
    unsigned char buf[PTRSIZE + 1];
    memset(buf, 0, PTRSIZE + 1);
    struct hacked_glow_obj *hacked_obj = (struct hacked_glow_obj *) data;

    if (hacked_obj->entity->team == TEAM_CT)
        hacked_obj->glow_obj_def.m_vGlowColorZ = 1.0f;
    else if (hacked_obj->entity->team == TEAM_CT)
        hacked_obj->glow_obj_def.m_vGlowColorX = 1.0f;
    hacked_obj->glow_obj_def.m_vGlowAlpha = 1.0f;
    hacked_obj->glow_obj_def.m_flGlowAlphaMax = 1.0f;
    hacked_obj->glow_obj_def.m_renderWhenOccluded = 1;
    /* hacked_obj->glow_obj_def.m_renderWhenUnoccluded = 1; */

    info("modified glow object:\n");
    print_glow_obj_def(&hacked_obj->glow_obj_def);

    read_from_proc(hacked_obj->glow_obj_addr + sizeof(int), buf, PTRSIZE);
    cur_entity_addr = unpack(buf, PTRSIZE);

    while(cur_entity_addr == hacked_obj->entity->base) {
        write_to_proc(hacked_obj->glow_obj_addr,
                &hacked_obj->glow_obj_def, GLOWOBJDEF_SIZE);
        read_from_proc(hacked_obj->glow_obj_addr + sizeof(int), buf, PTRSIZE);

        cur_entity_addr = unpack(buf, PTRSIZE);
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
        read_from_proc(cur_glow_entry, &glow_obj_def, GLOWOBJDEF_SIZE);
        nextFreeSlot = glow_obj_def.m_nNextFreeSlot;

        entity = get_entity_for_glow_entry(&glow_obj_def);
        if (nextFreeSlot == GLOW_ENTRY_IN_USE && entity) {
            info("found glow entry for entity:");
            print_glow_obj_def(&glow_obj_def);

            hacked_obj = malloc(sizeof(struct hacked_glow_obj));
            memset(hacked_obj, 0, sizeof(struct hacked_glow_obj));
            hacked_obj->glow_obj_addr = cur_glow_entry;
            hacked_obj->entity = entity;
            memcpy((void *) &hacked_obj->glow_obj_def, &glow_obj_def,
                    sizeof(struct GlowObjectDefinition_t));

            pthread_create(&thread, &detached_attr,
                    &write_glow_obj, hacked_obj);
        }

        cur_glow_entry += GLOW_ENTRY_SIZE;
    }

    //TODO integrate interface
    while (1);
}

void set_csgo_pid(void)
{
  pid_t pid;
  char line[LINE_LEN];
  FILE *cmd_stream;

  /* open stream to pidof */
  if ((cmd_stream = popen(PIDOF_CMD, "r")) == NULL)
    handle_error("popen");

  /* read output and process pid */
  if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
    handle_error_en(EINVAL, "fgets");

  if ((pid = strtoul(line, NULL, /*base */10)) == ULONG_MAX)
    handle_error("strtoul");

  /* cleanup */
  if (pclose(cmd_stream) == -1)
    handle_error("pclose");

  return pid;
}

void set_client_client_base_addr(void)
{
  uintptr_t base_addr;
  char line[LINE_LEN];
  FILE *cmd_stream;

  /* open stream to grep command */
  if ((cmd_stream = popen(BASE_CMD, "r")) == NULL)
    handle_error("popen");

  /* read output and process pid */
  if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
    handle_error_en(EINVAL, "fgets");

  if ((base_addr = strtoull(line, NULL, /*base */16)) == ULLONG_MAX)
    handle_error("strtoul");

  /* cleanup */
  if (pclose(cmd_stream) == -1)
    handle_error("pclose");

  return base_addr;
}

int main(void)
{
  pid_t pid;
  uintptr_t base_addr, offset_addr;
  char path[PATH_LEN];
  int mem_fd;

  list_init(&entities);
  pthread_attr_init(&detached_attr);
  pthread_attr_setdetachstate(&detached_attr, PTHREAD_CREATE_DETACHED);

  /* get process pid */
  pid = get_pid();
  verbose("%s pid: %jd\n", PROG_NAME, (intmax_t)pid);

  /* change working directory to make accessing files easy */
  if (snprintf(path, PATH_LEN, "/proc/%jd", (intmax_t)pid) < 0)
    handle_error_en(EINVAL, "snprintf");

  if (chdir(path) == -1)
    handle_error("chdir");
  verbose("cwd changed: %s\n", path);

  /* get base address of loaded library */
  base_addr = get_base_addr();
  verbose("%s base address: 0x%" PRIxPTR "\n", LIB_NAME, base_addr);

  /* get instruction address to patch */
  offset_addr = base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET;
  verbose("instruction address: 0x%" PRIxPTR "\n", offset_addr);

  /* open /proc/<pid>/mem to access process' memory */
  if ((mem_fd = open("mem", O_LARGEFILE | O_WRONLY)) == -1)
    handle_error("open");
  verbose("opened file: %s/mem\n", path);

  /* seek to byte we want to overwrite */
  if (lseek64(mem_fd, (off64_t)offset_addr, SEEK_SET) == -1)
    handle_error("lseek64");
  verbose("moved file offset: 0x0 -> 0x%" PRIxPTR "\n", offset_addr);

  /* overwrite the byte to enable wall hacks */
  if (write(mem_fd, "\x01", 1) == -1)
    handle_error("write");
  verbose("pwned! wrote 0x01 at 0x%" PRIxPTR "\n", offset_addr);

  // integrate!
  // get_global_addresses();
  // apply_glows();

  /* cleanup */
  if (close(mem_fd) == -1)
    handle_error("close");
}
