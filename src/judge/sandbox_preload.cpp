#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int DenyInt() {
	errno = EPERM;
	return -1;
}

FILE* DenyFile() {
	errno = EPERM;
	return nullptr;
}

}  // namespace

extern "C" {

int open(const char* /*path*/, int /*flags*/, ...) {
	return DenyInt();
}

int open64(const char* /*path*/, int /*flags*/, ...) {
	return DenyInt();
}

int openat(int /*dirfd*/, const char* /*path*/, int /*flags*/, ...) {
	return DenyInt();
}

int openat64(int /*dirfd*/, const char* /*path*/, int /*flags*/, ...) {
	return DenyInt();
}

int creat(const char* /*path*/, mode_t /*mode*/) {
	return DenyInt();
}

FILE* fopen(const char* /*path*/, const char* /*mode*/) {
	return DenyFile();
}

FILE* fopen64(const char* /*path*/, const char* /*mode*/) {
	return DenyFile();
}

FILE* freopen(const char* /*path*/, const char* /*mode*/, FILE* /*stream*/) {
	return DenyFile();
}

FILE* freopen64(const char* /*path*/, const char* /*mode*/, FILE* /*stream*/) {
	return DenyFile();
}

int system(const char* /*command*/) {
	return DenyInt();
}

FILE* popen(const char* /*command*/, const char* /*mode*/) {
	return DenyFile();
}

int socket(int /*domain*/, int /*type*/, int /*protocol*/) {
	return DenyInt();
}

int connect(int /*sockfd*/, const struct sockaddr* /*addr*/, socklen_t /*addrlen*/) {
	return DenyInt();
}

int bind(int /*sockfd*/, const struct sockaddr* /*addr*/, socklen_t /*addrlen*/) {
	return DenyInt();
}

int listen(int /*sockfd*/, int /*backlog*/) {
	return DenyInt();
}

int accept(int /*sockfd*/, struct sockaddr* /*addr*/, socklen_t* /*addrlen*/) {
	return DenyInt();
}

int accept4(int /*sockfd*/, struct sockaddr* /*addr*/, socklen_t* /*addrlen*/, int /*flags*/) {
	return DenyInt();
}

pid_t fork(void) {
	return static_cast<pid_t>(DenyInt());
}

pid_t vfork(void) {
	return static_cast<pid_t>(DenyInt());
}

}  // extern "C"
