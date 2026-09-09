#include "stdio_impl.h"
#include <sys/uio.h>

#if defined(STARBOARD)
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "starboard/log.h"
#endif  // defined(STARBOARD)

size_t __stdio_write(FILE *f, const unsigned char *buf, size_t len)
{
#if defined(STARBOARD)
	// Starboard routes stdout/stderr through SbLogRaw so platforms whose console
	// fd goes nowhere (e.g. Android, where an app's fd 1/2 are /dev/null) don't
	// lose output. That path is taken only for a character device (a tty or
	// /dev/null); when those fds instead point at a real destination, honor it
	// via the normal writev path below, so that a regular file, pipe, or socket
	// works correctly.
	if (f->fd == STDOUT_FILENO || f->fd == STDERR_FILENO) {
		struct stat st;
		int saved_errno = errno;
		if (fstat(f->fd, &st) != 0 || S_ISCHR(st.st_mode)) {
			errno = saved_errno;
			// Two spans to drain, in write order: the bytes already pending in
			// the stream's own buffer (f->wbase..f->wpos) from earlier writes,
			// then the newly written data (buf/len).
			const unsigned char *spans[2] = {f->wbase, buf};
			size_t span_lens[2] = {f->wpos - f->wbase, len};
			// Accumulate into a fixed on-stack buffer and flush a line at a time, to
			// avoid truncation. One byte is kept for the NUL.
			char line[4096];
			size_t n = 0;
			for (size_t i = 0; i < sizeof(spans) / sizeof(spans[0]); i++) {
				const unsigned char *p = spans[i];
				for (size_t j = 0; j < span_lens[i]; j++) {
					line[n++] = p[j];
					// Flush at a newline or when the buffer is full.
					if (p[j] == '\n' || n == sizeof line - 1) {
						line[n] = '\0';
						SbLogRaw(line);
						n = 0;
					}
				}
			}
			// Flush a trailing partial line that had no terminating newline.
			if (n) {
				line[n] = '\0';
				SbLogRaw(line);
			}
			f->wend = f->buf + f->buf_size;
			f->wpos = f->wbase = f->buf;
			return len;
		}
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
