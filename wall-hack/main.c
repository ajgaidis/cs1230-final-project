#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#define PROG_NAME "csgo_linux64"

#define LINE_LEN  20

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD  "grep '/client_client.so' maps | head -1 | cut -d'-' -f1"

#define PATH_LEN  64

#define INSN_OFFSET         0x7f1463
#define INSN_OPERAND_OFFSET 0x2

#define handle_error_en(en, msg) \
  do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)


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
  if (pclose(cmd_stream) == NULL)
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
  if (pclose(cmd_stream) == NULL)
    handle_error("pclose");

  return base_addr;
}

int main(void)
{
  pid_t pid;
  uintptr_t base_addr, offset_addr;
  char path[PATH_LEN];

  /* get process pid */
  pid = get_pid();

  /* change working directory to make accessing files easy */
  if (snprintf(path, PATH_LEN, "/proc/%.10s", pid) < 0)
    handle_error_en(EINVAL, "snprintf");

  if (chdir(path) == -1)
    handle_error("chdir");

  /* get base address of loaded library */
  base_addr = get_base_addr();

  /* get instruction address to patch */
  offset_addr = base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET;

  /* overwrite the byte to enable wall hacks */
}
