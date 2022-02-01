#undef TRACE_SYSTEM
#define TRACE_SYSTEM modtimerlat

#if !defined(_TRACE_MODTIMERLAT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MODTIMERLAT_H

#include <linux/tracepoint.h>

DECLARE_TRACE(modtimerlat_latency,
        TP_PROTO(u64 latency),
        TP_ARGS(latency));

DECLARE_TRACE(modtimerlat_latency_exceeded,
        TP_PROTO(u64 latency),
        TP_ARGS(latency));

#endif /* _TRACE_MODTIMERLAT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

