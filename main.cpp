#include <assert.h>
#include <memory.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

// https://github.com/derekmulcahy/xvcpi/blob/e4df3cd5eaa6ca248b93b0c076ed21503d0abaf9/xvcpi.c#L147
static int sread(int fd, char *target, int len) {
  char *t = target;
  while (len) {
    int r = read(fd, t, len);
    if (r <= 0)
      return r;
    t += r;
    len -= r;
  }
  return 0;
}

static int swrite(int fd, char *target, int len) {
  char *t = target;
  while (len) {
    int r = write(fd, t, len);
    if (r <= 0)
      return r;
    t += r;
    len -= r;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in listen_addr = {};
  listen_addr.sin_addr.s_addr = INADDR_ANY;
  listen_addr.sin_port = htons(2542);
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_len = sizeof(sockaddr_in);

  int res = bind(fd, (sockaddr *)&listen_addr, sizeof(listen_addr));
  assert(res >= 0);

  res = listen(fd, 0);
  assert(res >= 0);
  printf("Listing at port 2542\n");

  while (true) {
    sockaddr client_addr = {};
    socklen_t size = sizeof(client_addr);
    int client_fd = accept(fd, (sockaddr *)&client_addr, &size);
    assert(client_fd >= 0);

    printf("Accepted client\n");

    while (true) {
      char buffer[256];
      char tms[256];
      char tdi[256];
      char tdo[256] = {};

      assert(sread(client_fd, buffer, 2) >= 0);
      if (memcmp(buffer, "ge", 2) == 0) {
        // getinfo
        printf("getinfo:\n");
        assert(sread(client_fd, buffer, strlen("tinfo:")) >= 0);

        char info[] = "xvcServer_v1.0:2048\n";
        assert(swrite(client_fd, (char *)info, strlen(info)) >= 0);
      } else if (memcmp(buffer, "se", 2) == 0) {
        printf("settck:");
        assert(sread(client_fd, buffer, strlen("ttck:")) >= 0);

        uint32_t tck = 0;
        assert(sread(client_fd, (char *)&tck, sizeof(tck)) >= 0);
        printf("%d\n", tck);

        assert(swrite(client_fd, (char *)&tck, sizeof(tck)) >= 0);
      } else if (memcmp(buffer, "sh", 2) == 0) {
        printf("shift:");
        assert(sread(client_fd, buffer, strlen("ift:")) >= 0);

        uint32_t bits = 0;
        assert(sread(client_fd, (char *)&bits, sizeof(bits)) >= 0);

        uint32_t bytes = (bits + 7) / 8;
        assert(sread(client_fd, tms, bytes) >= 0);
        assert(sread(client_fd, tdi, bytes) >= 0);
        printf(" tms:");
        for (int i = 0; i < bits; i++) {
          int off = i % 8;
          int bit = ((tms[i / 8]) >> off) & 1;
	  printf("%c", bit ? '1' : '0');
        }
	printf(" tdi:");
        for (int i = 0; i < bits; i++) {
          int off = i % 8;
          int bit = ((tdi[i / 8]) >> off) & 1;
	  printf("%c", bit ? '1' : '0');
        }
	printf("\n");

        assert(swrite(client_fd, tdo, bytes) >= 0);
      } else {
        printf("Unsupported command\n");
        close(fd);
        break;
      }
    }

    close(client_fd);
  }

  return 0;
}