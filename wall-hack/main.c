#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>

#define PROG_NAME "csgo_linux64"

#define LINE_LEN  20

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD  "grep '/client_client.so' maps | head -1 | cut -d'-' -f1"

#define PATH_LEN  64

/* For wireframe WH */
#define INSN_OFFSET         0x7f1463
#define INSN_OPERAND_OFFSET 0x2

/* For glow WH */
#define GLOWOBJMGR_OFFSET 0x2b9cf80
#define GLOWOBJDEF_SIZE   64

#define handle_error_en(en, msg) \
  do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

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

int read_from_proc(pid_t pid, uintptr_t addr_to_read, void *buffer, size_t len)
{
    struct iovec local = {};
    struct iovec remote = {};
    int errsv = 0;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_read;
    remote.iov_len = local.iov_len;

    nread = process_vm_readv(pid, &local, 1, &remote, 1, NULL);

    if (nread != local.iov_len)
    {
        handle_error("process_vm_readv");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int write_to_proc(pid_t pid, uintptr_t addr_to_write, void *buffer, size_t len)
{
    struct iovec local = {};
    struct iovec remote = {};
    int errsv = 0;
    ssize_t nread = 0;

    local.iov_len = len;
    local.iov_base = buffer;

    remote.iov_base = (void *) addr_to_write;
    remote.iov_len = local.iov_len;

    nread = process_vm_writev(pid, &local, 1, &remote, 1, 0);

    if (nread != local.iov_len)
    {
        handle_error("process_vm_readv");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
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


pid_t get_pid(void)
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

uintptr_t get_base_addr(void)
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

unsigned long unpack(unsigned char *buf)
{
    unsigned long res = 0;
	for (int i = 0; i < 8; i++) {
        res += buf[i] << (i * 8);
	}
	return res;
}


int main(void)
{
  pid_t pid;
  uintptr_t base_addr, offset_addr;
  char path[PATH_LEN];

  /* get process pid */
  pid = get_pid();
  printf("Got %s pid: %d\n", PROG_NAME, pid);

  /* change working directory to make accessing files easy */
  if (snprintf(path, PATH_LEN, "/proc/%d", pid) < 0)
    handle_error_en(EINVAL, "snprintf");

  if (chdir(path) == -1)
    handle_error("chdir");

  /* get base address of loaded library */
  base_addr = get_base_addr();
  printf("Library client_client.so located at %#lx\n", base_addr);

  // Deref this to get the list
  uintptr_t s_GlowObjectManager = base_addr + GLOWOBJMGR_OFFSET;
  printf("s_GlowObjectManager located at %#lx\n", s_GlowObjectManager);
  unsigned char buf[9];
  memset(&buf, 0, 9);
  read_from_proc(pid, s_GlowObjectManager, buf, 8);
  print_hex_buf(buf, 9);
  uintptr_t m_GlowObjectDefinitions = unpack(buf);
  printf("GlowObjectDefinisions list located at %#lx\n", m_GlowObjectDefinitions);

  struct GlowObjectDefinition_t glow_obj_def;
  uintptr_t target_glow_obj_addr = m_GlowObjectDefinitions + (5 * 64);
  memset((void *)&glow_obj_def, 0, GLOWOBJDEF_SIZE);
  read_from_proc(pid, target_glow_obj_addr, &glow_obj_def, GLOWOBJDEF_SIZE);
  print_hex_buf((unsigned char *)&glow_obj_def, GLOWOBJDEF_SIZE);
  print_glow_obj_def(&glow_obj_def);

  glow_obj_def.m_vGlowColorX = 1.0f;
  glow_obj_def.m_vGlowAlpha = 1.0f;
  glow_obj_def.m_flGlowAlphaMax = 1.0f;
  glow_obj_def.m_renderWhenOccluded = 1;

  printf("===========================\n");
  print_hex_buf((unsigned char *)&glow_obj_def, GLOWOBJDEF_SIZE);
  print_glow_obj_def(&glow_obj_def);
  while(1) {
      write_to_proc(pid, target_glow_obj_addr, &glow_obj_def, GLOWOBJDEF_SIZE);
  }

  /* /1* get instruction address to patch *1/ */
  /* offset_addr = base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET; */

  /* overwrite the byte to enable wall hacks */
}
