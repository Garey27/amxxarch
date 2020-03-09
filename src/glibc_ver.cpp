#ifndef WIN32
extern "C" {

#include "force_link_glibc_2.5.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <errno.h>
#include <glob.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>
#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if !(__x86_64__)
// 32bit
// exp2 & exp2f undefined (?) in old glibc:i686
//__asm__(".symver __real_exp2,exp2@GLIBC_2.0");
__asm__(".symver __real_exp,exp@GLIBC_2.0");
__asm__(".symver __real_pow,pow@GLIBC_2.0");
__asm__(".symver __real_log,log@GLIBC_2.0");
//__asm__(".symver __real_exp2f,exp2f@GLIBC_2.0");
__asm__(".symver __real_expf,expf@GLIBC_2.0");
__asm__(".symver __real_powf,powf@GLIBC_2.0");
__asm__(".symver __real_logf,logf@GLIBC_2.0");
__asm__(".symver __real_fcntl,fcntl@GLIBC_2.0");
#else
__asm__(".symver __real_exp2,exp2@GLIBC_2.2.5");
__asm__(".symver __real_exp,exp@GLIBC_2.2.5");
__asm__(".symver __real_pow,pow@GLIBC_2.2.5");
__asm__(".symver __real_log,log@GLIBC_2.2.5");
__asm__(".symver __real_exp2f,exp2f@GLIBC_2.2.5");
__asm__(".symver __real_expf,expf@GLIBC_2.2.5");
__asm__(".symver __real_powf,powf@GLIBC_2.2.5");
__asm__(".symver __real_logf,logf@GLIBC_2.2.5");
__asm__(".symver __real_fcntl,fcntl@GLIBC_2.2.5");
#endif

double __real_exp2(double);
double __real_exp(double, double);
double __real_pow(double, double);
double __real_log(double, double);
float __real_exp2f(float);
float __real_expf(float, float);
float __real_powf(float, float);
float __real_logf(float, float);
int __real_fcntl(int fd, int cmd, ...);

int __wrap_fcntl(int fd, int cmd, ...);
int __wrap_fcntl64(int fd, int cmd, ...);


double __wrap_exp2(double a) {
#if !(__x86_64__)
	return __real_pow(2, a);
#endif
	return __real_exp2(a);
}
double __wrap_exp(double a, double b) {
	return __real_exp(a, b);
}
double __wrap_pow(double a, double b) {
	return __real_pow(a, b);
}
double __wrap_log(double a, double b) {
	return __real_log(a, b);
}

float __wrap_exp2f(float a) {
#if !(__x86_64__)
	return __real_powf(2, a);
#endif
	return __real_exp2f(a);
}
float __wrap_expf(float a, float b) {
	return __real_expf(a, b);
}
float __wrap_powf(float a, float b) {
	return __real_powf(a, b);
}
float __wrap_logf(float a, float b) {
	return __real_logf(a, b);
}

} //extern "C"

extern "C" int fcntl_old(int fd, int cmd, ...);
#ifdef __i386__
__asm(".symver fcntl_old,fcntl@GLIBC_2.0");
#elif defined(__amd64__)
__asm(".symver fcntl_old,fcntl@GLIBC_2.2.5");
#elif defined(__arm__)
__asm(".symver fcntl_old,fcntl@GLIBC_2.4");
#elif defined(__aarch64__)
__asm(".symver fcntl_old,fcntl@GLIBC_2.17");
#endif

extern "C" int __wrap_fcntl(int fd, int cmd, ...) {
    int ret;
    va_list vargs;
    va_start(vargs, cmd);
    ret = fcntl_old(fd, cmd, va_arg(vargs, void *));
    va_end(vargs);
    return ret;
}
extern "C" int __wrap_fcntl64(int fd, int cmd, ...) {
    int ret;
    va_list vargs;
    va_start(vargs, cmd);
    ret = fcntl_old(fd, cmd, va_arg(vargs, void *));
    va_end(vargs);
    return ret;
}

extern "C" int getentropy(void *buf, size_t len)
{
    int pre_errno = errno;
    int ret;
    if (len > 256)
        return (-1);
    do {
        ret = syscall(SYS_getrandom, buf, len, 0);
    } while (ret == -1 && errno == EINTR);

    if (ret != (int)len)
        return (-1);
    errno = pre_errno;
    return (0);
}

#endif //ifndef 
