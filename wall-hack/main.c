#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "colorio.h"
#include "list.h"

#define LINE_LEN  20
#define PATH_LEN  64
#define PTR_SIZE  sizeof(uintptr_t)

#define UNUSED(...)	(void)(__VA_ARGS__)

#define PROG_NAME       "csgo_linux64"
#define CLI_LIB_NAME "client_client.so"
#define DRAW_LIB_NAME "libdraw.so"

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD_FMT(lib_name)  "grep '/" lib_name "' %s | " \
                                "head -1 | " \
                                "cut -d'-' -f1"
#define CLI_BASE_CMD_FMT BASE_CMD_FMT(CLI_LIB_NAME)
#define CLI_BASE_CMD_LEN  sizeof(CLI_BASE_CMD_FMT) + PATH_LEN
#define DRAW_BASE_CMD_FMT BASE_CMD_FMT(DRAW_LIB_NAME)
#define DRAW_BASE_CMD_LEN  sizeof(DRAW_BASE_CMD_FMT) + PATH_LEN
#define MAX_CMD_LEN             0x100

/* one-byte wall-hack offsets */
#define INSN_OFFSET             0x7f1683
#define INSN_OPERAND_OFFSET     0x2

/* for glow wall-hack */
#define GLOWOBJMGR_OFFSET       0x2b9cf80 + (16 * 1024)
#define ENTITYLIST_OFFSET       0x232e7d8 + (16 * 1024)
#define LOCAL_PLAYER_PTR_OFFSET 0x22fd128 + (16 * 1024)
#define GLOWOBJDEF_SIZE         64
#define ENTITY_LIST_ITEM_SIZE   32
#define ENTITY_OFFSET           16
#define MAX_HP                  100.f
#define DRAW_NTEAMMATES_OFFSET     0x4118
#define DRAW_TEAMMATES_HPS_OFFSET  0x4100
#define DRAW_NENEMIES_OFFSET       0x4114
#define DRAW_ENEMIES_HPS_OFFSET    0x40e0
#define DRAW_HP_ENABLED            0x411c
#define MAX_PLAYERS                20

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

pid_t csgo_pid;
char csgo_proc_mem_path[PATH_LEN];
char csgo_proc_maps_path[PATH_LEN];
uintptr_t client_client_base_addr;
uintptr_t draw_base_addr;

pthread_t glow_threads[10];
int total_glow_threads = 0;
pthread_t hp_thread;
uintptr_t s_GlowObjectManager;
uintptr_t m_GlowObjectDefinitions;
uintptr_t local_player_addr;
struct Entity* local_player;

list_t entities;
pthread_mutex_t entities_mutex = PTHREAD_MUTEX_INITIALIZER;

/* CLI stuff */
#define CLI_PROMPT      COLOR_FMT("> ", BRIGHT, FG_BLUE)
#define CLI_WELCOME_MSG "CSGO Wall Hacks\n"
#define CLI_GOODBYE_MSG "See you later!\n"
#define CLI_HELP_MSG \
    "+----------------------------------------------------------------------+\n" \
"|                              Help Menu                               |\n" \
"+----------------------------------------------------------------------+\n" \
"| help, h                    Print this list of commands               |\n" \
"| q                          Exit the program                          |\n" \
"| glow <0 | 1>               Disable/enable glow wall-hacks            |\n" \
"| wireframe <0 | 1>          Disable/enable wireframe wall-hacks       |\n" \
"| healthbars <0 | 1>         Disable/enable healthbars                 |\n" \
"+----------------------------------------------------------------------+\n\n"
#define CLI_WIREFRAME_SYNTAX_ERROR_MSG  "invalid command syntax (pass 0 or 1)"
#define CLI_GLOW_SYNTAX_ERROR_MSG  "invalid command syntax (pass 0 or 1)"
#define CLI_HEALTHBARS_SYNTAX_ERROR_MSG  "invalid command syntax (pass 0 or 1)"

struct cli_cmd {
    const char *cmdstr;
    void (*handler)(const char *);
};


void read_from_proc(uintptr_t addr_to_read, void *buffer, size_t len)
{
    struct iovec local;
    struct iovec remote;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_read;
    remote.iov_len = local.iov_len;

    nread = process_vm_readv(csgo_pid, &local, 1, &remote, 1, 0);

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
        handle_error("process_vm_writev");
}

void write_proc_mem(uintptr_t addr_to_write, const void *buffer, size_t len)
{
    int mem_fd;

    /* open /proc/<pid>/mem to access process' memory */
    if ((mem_fd = open(csgo_proc_mem_path, O_LARGEFILE | O_WRONLY)) == -1)
        handle_error("open");
    dbg("opened file: %s\n", csgo_proc_mem_path);

    /* seek to byte we want to overwrite */
    if (lseek64(mem_fd, (off64_t)addr_to_write, SEEK_SET) == -1)
        handle_error("lseek64");
    dbg("moved file offset: 0x0 -> 0x%" PRIxPTR "\n", addr_to_write);

    /* overwrite the byte to enable/disable wall hacks */
    if (write(mem_fd, buffer, len) == -1)
        handle_error("write");
    dbg("wrote %zu bytes to to 0x%" PRIxPTR "\n", len, addr_to_write);

    /* cleanup */
    if (close(mem_fd) == -1)
        handle_error("close");
    dbg("closed file: %s\n", csgo_proc_mem_path);
}

void print_entity(struct Entity *obj)
{
    dbg("base:\t0x%" PRIxPTR "\n", obj->base);
    dbg("team:\t%d\n", obj->team);
    dbg("health:\t%d\n", obj->health);
}

void print_glow_obj_def(struct GlowObjectDefinition_t *obj)
{
    dbg("m_nNextFreeSlot:\t%d\n", obj->m_nNextFreeSlot);
    dbg("m_pEntity:\t0x%" PRIxPTR "\n", obj->m_pEntity);
    dbg("m_vGlowColorX:\t%.3f\n", obj->m_vGlowColorX);
    dbg("m_vGlowColorY:\t%.3f\n", obj->m_vGlowColorY);
    dbg("m_vGlowColorZ:\t%.3f\n", obj->m_vGlowColorZ);
    dbg("m_vGlowAlpha:\t%.3f\n", obj->m_vGlowAlpha);
    dbg("m_vGlowAlphaMax:\t%.3f\n", obj->m_flGlowAlphaMax);
    dbg("m_renderWhenOccluded:\t%d\n", obj->m_renderWhenOccluded);
    dbg("m_renderWhenUnccluded:\t%d\n", obj->m_renderWhenUnoccluded);
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

uintptr_t get_glowobj_def_list(uintptr_t glowobj_manager)
{
    unsigned char buf[PTR_SIZE];

    read_from_proc(glowobj_manager, buf, PTR_SIZE);

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

void cleanup_entities()
{

    struct Entity *entity;

    while (!list_empty(&entities)) {
        entity = list_head(&entities, struct Entity, entity_link);
        list_remove_head(&entities);
        free(entity);
    }
}

void *set_entities(void *arg)
{
    uintptr_t cur_entity_entry;
    unsigned char buf[PTR_SIZE];
    struct Entity *entity;
    uintptr_t entity_ptr = -1;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* while (1) { */

        pthread_mutex_lock(&entities_mutex);
        cleanup_entities();

        read_from_proc(client_client_base_addr + LOCAL_PLAYER_PTR_OFFSET,
                        buf, PTR_SIZE);
        local_player_addr = unpack(buf, PTR_SIZE);
        verbose("Local player at: 0x%" PRIxPTR "\n", local_player_addr);

        cur_entity_entry = client_client_base_addr + ENTITYLIST_OFFSET;
        verbose("entity list address: 0x%" PRIxPTR "\n", cur_entity_entry);

        for (int i  = 0; i <  MAX_PLAYERS; i++) {
        /* while (entity_ptr) { */
            read_from_proc(cur_entity_entry + ENTITY_OFFSET, buf, PTR_SIZE);
            entity_ptr = unpack(buf, sizeof(uintptr_t));

            if (entity_ptr) {
                verbose("found entity at: 0x%" PRIxPTR "\n", entity_ptr);

                entity = get_new_entity();
                entity->base = entity_ptr;
                entity->health = get_entity_int_field(entity, ENT_HEALTH_OFFSET);
                entity->team = get_entity_int_field(entity, ENT_TEAM_OFFSET);
                list_insert_head(&entities, &entity->entity_link);

                if (entity_ptr == local_player_addr) {
                    local_player =  entity;
                }
                print_entity(entity);
            }

            cur_entity_entry += ENTITY_LIST_ITEM_SIZE;
        }
        pthread_mutex_unlock(&entities_mutex);

        /* sleep for 10 seconds */
        /* sleep(12000); */
    /* } */
        pthread_exit(NULL);
}

struct Entity *get_glow_entry_entity(struct GlowObjectDefinition_t *glow_obj)
{
    struct Entity *entity;
    struct Entity *ret = NULL;

    pthread_mutex_lock(&entities_mutex);
    list_iterate_begin(&entities, entity, struct Entity, entity_link) {

        if (entity->base == glow_obj->m_pEntity) {
            ret = entity;
            break;
        }

    } list_iterate_end();
    pthread_mutex_unlock(&entities_mutex);

    return ret;
}

void cleanup_glows(void *data)
{
    free(data);
}

void disable_glows(void)
{
    for (int i = 0; i < total_glow_threads; i++) {
        pthread_cancel(glow_threads[i]);
    }

    total_glow_threads = 0;
}

void *write_glow_obj(void *data)
{
    uintptr_t cur_entity_addr;
    float perc_hp;
    unsigned char buf[PTR_SIZE + 1];
    memset(buf, 0, PTR_SIZE + 1);
    struct hacked_glow_obj *hacked_obj = (struct hacked_glow_obj *) data;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push(cleanup_glows, data);

    hacked_obj->glow_obj_def.m_vGlowAlpha = 1.0f;
    hacked_obj->glow_obj_def.m_flGlowAlphaMax = 1.0f;
    hacked_obj->glow_obj_def.m_renderWhenOccluded = 1;
    /* hacked_obj->glow_obj_def.m_renderWhenUnoccluded = 1; */

    dbg("modified glow object:\n");
    print_glow_obj_def(&hacked_obj->glow_obj_def);

    read_from_proc(hacked_obj->glow_obj_addr + sizeof(int), buf, PTR_SIZE);
    cur_entity_addr = unpack(buf, PTR_SIZE);

    while(cur_entity_addr == hacked_obj->entity->base) {

        if (hacked_obj->entity->team == local_player->team) {
            /* Players on our team just have a blue glow */
            hacked_obj->glow_obj_def.m_vGlowColorZ = 1.f;
        }
        else {
            /*
             * For enemy players, glow based on their current health
             * green = high hp, red = low hp
             */
            perc_hp = hacked_obj->entity->health / MAX_HP;
            hacked_obj->glow_obj_def.m_vGlowColorY = perc_hp;
            hacked_obj->glow_obj_def.m_vGlowColorX = 1.f - perc_hp;
        }

        write_to_proc(hacked_obj->glow_obj_addr,
                      &hacked_obj->glow_obj_def, GLOWOBJDEF_SIZE);
        read_from_proc(hacked_obj->glow_obj_addr + sizeof(int), buf, PTR_SIZE);
        cur_entity_addr = unpack(buf, PTR_SIZE);

        /* Update health */
        memset(buf, 0, PTR_SIZE);
        read_from_proc(hacked_obj->entity->base + ENT_HEALTH_OFFSET, buf, sizeof(int));
        hacked_obj->entity->health = unpack(buf, sizeof(int));
    }

    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

void set_glow_info(void)
{
    s_GlowObjectManager = client_client_base_addr + GLOWOBJMGR_OFFSET;
    verbose("s_GlowObjectManager address: 0x%" PRIxPTR "\n", s_GlowObjectManager);

    m_GlowObjectDefinitions = get_glowobj_def_list(s_GlowObjectManager);
    verbose("GlowObjectDefinitions list address: 0x%" PRIxPTR "\n",
            m_GlowObjectDefinitions);
}


void enable_glows(void)
{
    struct GlowObjectDefinition_t glow_obj_def;
    struct hacked_glow_obj *hacked_obj;
    struct Entity *entity = NULL;
    int nextFreeSlot = GLOW_ENTRY_IN_USE;
    uintptr_t cur_glow_entry;

    cur_glow_entry = m_GlowObjectDefinitions;

    while (nextFreeSlot == GLOW_ENTRY_IN_USE) {
        read_from_proc(cur_glow_entry, &glow_obj_def, GLOWOBJDEF_SIZE);
        nextFreeSlot = glow_obj_def.m_nNextFreeSlot;

        entity = get_glow_entry_entity(&glow_obj_def);
        if (nextFreeSlot == GLOW_ENTRY_IN_USE && (entity != NULL)) {
            dbg("found glow entry for entity:\n");
            print_glow_obj_def(&glow_obj_def);

            hacked_obj = malloc(sizeof(struct hacked_glow_obj));
            memset(hacked_obj, 0, sizeof(struct hacked_glow_obj));
            hacked_obj->glow_obj_addr = cur_glow_entry;
            hacked_obj->entity = entity;
            memcpy((void *) &hacked_obj->glow_obj_def, &glow_obj_def,
                   sizeof(struct GlowObjectDefinition_t));

            pthread_create(&glow_threads[total_glow_threads], NULL, &write_glow_obj, hacked_obj);
            pthread_detach(glow_threads[total_glow_threads]);
            total_glow_threads++;
        }

        cur_glow_entry += GLOW_ENTRY_SIZE;
    }
}

void *write_healthbars(void *data)
{
    UNUSED(data);
    uintptr_t num_teammates_addr = draw_base_addr + DRAW_NTEAMMATES_OFFSET;
    uintptr_t teammates_hps_addr = draw_base_addr + DRAW_TEAMMATES_HPS_OFFSET;
    uintptr_t num_enemies_addr = draw_base_addr + DRAW_NENEMIES_OFFSET;
    uintptr_t enemies_hps_addr = draw_base_addr + DRAW_ENEMIES_HPS_OFFSET;
    int nteammates, nenemies;
    uintptr_t cur_teammate_hp, cur_enemy_hp;
    struct Entity *entity;
    unsigned char buf[PTR_SIZE];

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_cleanup_push(cleanup_glows, data);

    while (1) {
        nteammates = 0;
        nenemies = 0;
        cur_teammate_hp = teammates_hps_addr;
        cur_enemy_hp = enemies_hps_addr;
        pthread_mutex_lock(&entities_mutex);
        list_iterate_begin(&entities, entity, struct Entity, entity_link) {
            /* Update health */
            memset(buf, 0, PTR_SIZE);
            read_from_proc(entity->base + ENT_HEALTH_OFFSET, buf, sizeof(int));
            entity->health = unpack(buf, sizeof(int));
            if (entity->team == local_player->team) {
                write_to_proc(cur_teammate_hp, &entity->health, sizeof(int));
                cur_teammate_hp += sizeof(int);
                nteammates++;
            }
            else {
                write_to_proc(cur_enemy_hp, &entity->health, sizeof(int));
                cur_enemy_hp += sizeof(int);
                nenemies++;
            }
        } list_iterate_end();
        pthread_mutex_unlock(&entities_mutex);
        write_to_proc(num_teammates_addr, &nteammates, sizeof(int));
        write_to_proc(num_enemies_addr, &nenemies, sizeof(int));
    }

    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

void enable_healthbars(void)
{
    uintptr_t hp_enabled_ptr = draw_base_addr + DRAW_HP_ENABLED;
    int one = 1;
    write_to_proc(hp_enabled_ptr, &one, sizeof(int));
    pthread_create(&hp_thread, NULL, &write_healthbars, NULL);
    pthread_detach(hp_thread);
}

void disable_healthbars(void)
{
    uintptr_t hp_enabled_ptr = draw_base_addr + DRAW_HP_ENABLED;
    uintptr_t num_teammates_addr = draw_base_addr + DRAW_NTEAMMATES_OFFSET;
    uintptr_t num_enemies_addr = draw_base_addr + DRAW_NENEMIES_OFFSET;
    int zero = 0;
    write_to_proc(hp_enabled_ptr, &zero, sizeof(int));
    write_to_proc(num_teammates_addr, &zero, sizeof(int));
    write_to_proc(num_enemies_addr, &zero, sizeof(int));
    pthread_cancel(hp_thread);
}


void enable_wireframes(void)
{
    uintptr_t addr_to_write;

    /* get instruction address to patch */
    addr_to_write = client_client_base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET;

    /* patch it! */
    write_proc_mem(addr_to_write, "\x01", 1);

    verbose("wireframes enabled! wrote 0x01 to 0x%" PRIxPTR "\n", addr_to_write);
}

void disable_wireframes(void)
{
    uintptr_t addr_to_write;

    /* get instruction address to patch */
    addr_to_write = client_client_base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET;

    /* patch it! */
    write_proc_mem(addr_to_write, "\x02", 1);

    verbose("wireframes disabled! wrote 0x02 to 0x%" PRIxPTR "\n", addr_to_write);
}

void set_csgo_pid(void)
{
    char line[LINE_LEN];
    FILE *cmd_stream;

    /* open stream to pidof */
    if ((cmd_stream = popen(PIDOF_CMD, "r")) == NULL)
        handle_error("popen");

    /* read output and process pid */
    if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
        handle_error_en(EINVAL, "fgets");

    if ((csgo_pid = strtoul(line, NULL, /*base */10)) == ULONG_MAX)
        handle_error("strtoul");

    /* cleanup */
    if (pclose(cmd_stream) == -1)
        handle_error("pclose");

    verbose("%s pid: %jd\n", PROG_NAME, (intmax_t)csgo_pid);
}

void set_csgo_proc_path(void)
{
    if (snprintf(csgo_proc_mem_path, PATH_LEN,
                 "/proc/%jd/mem", (intmax_t)csgo_pid) < 0)
        handle_error_en(EINVAL, "snprintf");

    if (snprintf(csgo_proc_maps_path, PATH_LEN,
                 "/proc/%jd/maps", (intmax_t)csgo_pid) < 0)
        handle_error_en(EINVAL, "snprintf");
}

void set_shared_lib_base_addr(char *lib_name, char* cmd_fmt,
                              int cmd_len, uintptr_t *base_addr_ptr)
{
    char line[LINE_LEN];
    char cmd_string[MAX_CMD_LEN];
    FILE *cmd_stream;

    /* create the command to pass to popen */
    if (snprintf(cmd_string, cmd_len, cmd_fmt, csgo_proc_maps_path) < 0)
        handle_error_en(EINVAL, "snprintf");

    /* open stream to grep command */
    if ((cmd_stream = popen(cmd_string, "r")) == NULL)
        handle_error("popen");

    /* read output and process pid */
    if ((fgets(line, LINE_LEN, cmd_stream)) == NULL)
        handle_error_en(EINVAL, "fgets");

    *base_addr_ptr = strtoull(line, NULL, /*base */16);
    if (*base_addr_ptr == ULLONG_MAX)
        handle_error("strtoul");

    /* cleanup */
    if (pclose(cmd_stream) == -1)
        handle_error("pclose");

    verbose("%s base address: 0x%" PRIxPTR "\n", lib_name,
            *base_addr_ptr);
}

static void help_cmd(const char *line)
{
    UNUSED(line);
    cli(CLI_HELP_MSG);
}

static void glow_cmd(const char *line)
{
    int enable_glow;

    if ((sscanf(line, "%*s %u", &enable_glow)) != 1) {
        warn(CLI_GLOW_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_glow != 0 && enable_glow != 1) {
        warn(CLI_GLOW_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_glow == 0)
        disable_glows();
    else
        enable_glows();
}

static void wireframe_cmd(const char *line)
{
    int enable_wireframe;

    if ((sscanf(line, "%*s %u", &enable_wireframe)) != 1) {
        warn(CLI_WIREFRAME_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_wireframe != 0 && enable_wireframe != 1) {
        warn(CLI_WIREFRAME_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_wireframe == 0)
        disable_wireframes();
    else
        enable_wireframes();
}


static void healthbars_cmd(const char *line)
{
    int enable_hp;

    if ((sscanf(line, "%*s %u", &enable_hp)) != 1) {
        warn(CLI_HEALTHBARS_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_hp != 0 && enable_hp != 1) {
        warn(CLI_HEALTHBARS_SYNTAX_ERROR_MSG);
        return;
    }

    if (enable_hp == 0)
        disable_healthbars();
    else
        enable_healthbars();
}

struct cli_cmd cmd_table[] = {
    {"help",        help_cmd},
    {"h",           help_cmd},
    {"glow",        glow_cmd},
    {"g",           glow_cmd},
    {"wireframe",   wireframe_cmd},
    {"wireframes",  wireframe_cmd},
    {"wf",          wireframe_cmd},
    {"healthbars",  healthbars_cmd},
    {"hp",          healthbars_cmd},
};

int main(int argc, char *argv[])
{
    char *line;
    rl_bind_key('\t', rl_complete);
    char cmd[LINE_MAX];
    int num_cmds, ret, i;
    pthread_t entities_thr;

    /* init entity list */
    list_init(&entities);

    /* set global values */
    set_csgo_pid();
    set_csgo_proc_path();
    set_shared_lib_base_addr(CLI_LIB_NAME, CLI_BASE_CMD_FMT,
                             CLI_BASE_CMD_LEN,  &client_client_base_addr);
    set_shared_lib_base_addr(DRAW_LIB_NAME, DRAW_BASE_CMD_FMT,
                             DRAW_BASE_CMD_LEN, &draw_base_addr);
    set_glow_info();

    /* start entity fetching thread */
    pthread_create(&entities_thr, NULL, &set_entities, NULL);
    pthread_detach(entities_thr);

    /* get number of CLI commands */
    num_cmds = sizeof(cmd_table) / sizeof(struct cli_cmd);

    /* let's welcome our guests :) */
    cli(CLI_WELCOME_MSG);

    /* begin the CLI */
    while (1) {

        /* collect user input */
        if (!(line = readline(CLI_PROMPT)))
            break;

        /* process user input */
        if ((ret = sscanf(line, "%s", cmd)) != 1) {
            /* got something weird; print help menu */
            if (ret != EOF)
                help_cmd(line);
            continue;
        }

        /* quit */
        if (!strcmp(cmd, "q"))
            break;

        /* other commands */
        for (i = 0; i < num_cmds; i++) {
            if (!strcmp(cmd, cmd_table[i].cmdstr)) {
                cmd_table[i].handler(line);
                break;
            }
        }

        /* no command match; print help menu */
        if (i == num_cmds) {
            warn("error: no valid command specified\n");
            help_cmd(line);
            continue;
        }

        add_history(line);
        free(line);
    }

    cli(CLI_GOODBYE_MSG);

    /* cleanup */
    pthread_mutex_destroy(&entities_mutex);
    pthread_cancel(entities_thr);

    exit(EXIT_SUCCESS);
}
