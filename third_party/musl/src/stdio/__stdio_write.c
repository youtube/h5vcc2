#include "stdio_impl.h"
#include <sys/uio.h>

#if defined(STARBOARD)
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "starboard/log.h"
#endif  // defined(STARBOARD)

size_t __stdio_write(FILE *f, const unsigned char *buf, size_t len)
{
#if defined(STARBOARD)
	// Starboard routes stdout/stderr through SbLogRaw so that platforms that
	// handle them specially don't miss any output. Coalesce the buffered bytes
	// with the new data and emit one line at a time, since some log sinks
	// truncate at a per-entry size limit.
	if (f->fd == STDOUT_FILENO || f->fd == STDERR_FILENO) {
		size_t buffered = f->wpos - f->wbase;
		size_t total = buffered + len;
		char *data = malloc(total + 1);
		if (data) {
			char *p = data, *end = data + total;
			memcpy(data, f->wbase, buffered);
			memcpy(data + buffered, buf, len);
			data[total] = '\0';
			while (p < end) {
				char *nl = memchr(p, '\n', end - p);
				char *line_end = nl ? nl + 1 : end;
				char saved = *line_end;
				*line_end = '\0';
				SbLogRaw(p);
				*line_end = saved;
				p = line_end;
			}
			free(data);
		}
		f->wend = f->buf + f->buf_size;
		f->wpos = f->wbase = f->buf;
		return len;
	}
#endif  // defined(STARBOARD)
	struct iovec iovs[2] = {
		{ .iov_base = f->wbase, .iov_len = f->wpos-f->wbase },
		{ .iov_base = (void *)buf, .iov_len = len }
	};
	struct iovec *iov = iovs;
	size_t rem = iov[0].iov_len + iov[1].iov_len;
	int iovcnt = 2;
	ssize_t cnt;
	for (;;) {
		cnt = syscall(SYS_writev, f->fd, iov, iovcnt);
		if (cnt == rem) {
			f->wend = f->buf + f->buf_size;
			f->wpos = f->wbase = f->buf;
			return len;
		}
		if (cnt < 0) {
			f->wpos = f->wbase = f->wend = 0;
			f->flags |= F_ERR;
			return iovcnt == 2 ? 0 : len-iov[0].iov_len;
		}
		rem -= cnt;
		if (cnt > iov[0].iov_len) {
			cnt -= iov[0].iov_len;
			iov++; iovcnt--;
		}
		iov[0].iov_base = (char *)iov[0].iov_base + cnt;
		iov[0].iov_len -= cnt;
	}
}
