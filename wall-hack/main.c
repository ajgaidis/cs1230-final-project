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


#define PROG_NAME "csgo_linux64"
#define LIB_NAME  "client_client.so"

#define PIDOF_CMD "pidof " PROG_NAME
#define BASE_CMD  "grep '/" LIB_NAME "' maps | head -1 | cut -d'-' -f1"

#define LINE_LEN  20
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

int main(void)
{
  pid_t pid;
  uintptr_t base_addr, offset_addr;
  char path[PATH_LEN];
  int mem_fd;

  /* get process pid */
  pid = get_pid();

#ifdef VERBOSE
  printf("[+] %s pid: %jd\n", PROG_NAME, (intmax_t)pid);
#endif

  /* change working directory to make accessing files easy */
  if (snprintf(path, PATH_LEN, "/proc/%.10jd", (intmax_t)pid) < 0)
    handle_error_en(EINVAL, "snprintf");

  if (chdir(path) == -1)
    handle_error("chdir");

#ifdef VERBOSE
  printf("[+] cwd changed: %s\n", path);
#endif

  /* get base address of loaded library */
  base_addr = get_base_addr();

  /* get instruction address to patch */
  offset_addr = base_addr + INSN_OFFSET + INSN_OPERAND_OFFSET;

#ifdef VERBOSE
  printf("[+] %s base address: %" PRIxPTR "\n", LIB_NAME, base_addr);
  printf("[+] instruction address: %" PRIxPTR "\n", offset_addr);
#endif

  /* open /proc/<pid>/mem to access process' memory */
  if ((mem_fd = open("mem", O_LARGEFILE | O_WRONLY)) == -1)
    handle_error("open");

#ifdef VERBOSE
  printf("[+] opened file: %s/mem\n", path);
#endif

  /* seek to byte we want to overwrite */
  if (lseek64(mem_fd, (off64_t)offset_addr, SEEK_SET) == -1)
    handle_error("lseek64");

#ifdef VERBOSE
  printf("[+] moved file offset: 0x0 -> %" PRIxPTR "\n", offset_addr);
#endif

  /* overwrite the byte to enable wall hacks */
  if (write(mem_fd, "\x01", 1) == -1)
    handle_error("write");

#ifdef VERBOSE
  printf("[+] pwned! wrote 0x01 at %" PRIxPTR "\n", offset_addr);
#endif

  /* cleanup */
  if (close(mem_fd) == -1)
    handle_error("close");
}
