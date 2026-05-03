#pragma once

#ifndef HAVE_SYS_SDT_H

/* Disable SDT/DTrace probes on systems without sys/sdt.h */
#define DTRACE_PROBE(provider, name)               ((void)0)
#define DTRACE_PROBE1(provider, name, arg1)        ((void)0)
#define DTRACE_PROBE2(provider, name, arg1, arg2)  ((void)0)
#define DTRACE_PROBE3(provider, name, arg1, arg2, arg3) ((void)0)
#define DTRACE_PROBE4(provider, name, arg1, arg2, arg3, arg4) ((void)0)
#define DTRACE_PROBE5(provider, name, arg1, arg2, arg3, arg4, arg5) ((void)0)

/* SystemTap-compatible */
#define STAP_PROBE(provider, name) DTRACE_PROBE(provider, name)

#endif /* HAVE_SYS_SDT_H */
