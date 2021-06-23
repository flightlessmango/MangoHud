//////////////////////////////////////////////////////////////////////////////
// gpuvis_trace_utils.h - v0.10 - public domain
//   no warranty is offered or implied; use this code at your own risk
//
// This is a single header file with useful utilities for gpuvis linux tracing
//
// ============================================================================
// You MUST define GPUVIS_TRACE_IMPLEMENTATION in EXACTLY _one_ C or C++ file
// that includes this header, BEFORE the include, like this:
//
//   #define GPUVIS_TRACE_IMPLEMENTATION
//   #include "gpuvis_trace_utils.h"
//
// All other files should just #include "gpuvis_trace_utils.h" w/o the #define.
// ============================================================================
//
// Credits
//
//    Michael Sartain
//
// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.

//////////////////////////////////////////////////////////////////////////////
//
//       INCLUDE SECTION
//

#ifndef _GPUVIS_TRACE_UTILS_H_
#define _GPUVIS_TRACE_UTILS_H_

#include <stdarg.h>

#if !defined( __linux__ )
#define GPUVIS_TRACE_UTILS_DISABLE
#endif

#if defined( __clang__ ) || defined( __GNUC__ )
// printf-style warnings for user functions.
#define GPUVIS_ATTR_PRINTF( _x, _y ) __attribute__( ( __format__( __printf__, _x, _y ) ) )
#define GPUVIS_MAY_BE_UNUSED __attribute__( ( unused ) )
#define GPUVIS_CLEANUP_FUNC( x ) __attribute__( ( __cleanup__( x ) ) )
#else
#define GPUVIS_ATTR_PRINTF( _x, _y )
#define GPUVIS_MAY_BE_UNUSED
#define GPUVIS_CLEANUP_FUNC( x )
#endif

#if !defined( GPUVIS_TRACE_UTILS_DISABLE )

#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
  #define GPUVIS_EXTERN   extern "C"
  #if __cplusplus>=201103L
    #define THREAD_LOCAL thread_local
  #else
    #define THREAD_LOCAL __thread
  #endif
#else
  #define GPUVIS_EXTERN   extern
#endif

// From kernel/trace/trace.h
#ifndef TRACE_BUF_SIZE
#define TRACE_BUF_SIZE     1024
#endif

// Try to open tracefs trace_marker file for writing. Returns -1 on error.
GPUVIS_EXTERN int gpuvis_trace_init( void );
// Close tracefs trace_marker file.
GPUVIS_EXTERN void gpuvis_trace_shutdown( void );

// Write user event to tracefs trace_marker.
GPUVIS_EXTERN int gpuvis_trace_printf( const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 1, 2 );
GPUVIS_EXTERN int gpuvis_trace_vprintf( const char *fmt, va_list ap ) GPUVIS_ATTR_PRINTF( 1, 0 );

// Write user event (with duration=XXms) to tracefs trace_marker.
GPUVIS_EXTERN int gpuvis_trace_duration_printf( float duration, const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 2, 3 );
GPUVIS_EXTERN int gpuvis_trace_duration_vprintf( float duration, const char *fmt, va_list ap ) GPUVIS_ATTR_PRINTF( 2, 0 );

// Write user event (with begin_ctx=XX) to tracefs trace_marker.
GPUVIS_EXTERN int gpuvis_trace_begin_ctx_printf( unsigned int ctx, const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 2, 3 );
GPUVIS_EXTERN int gpuvis_trace_begin_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap ) GPUVIS_ATTR_PRINTF( 2, 0 );

// Write user event (with end_ctx=XX) to tracefs trace_marker.
GPUVIS_EXTERN int gpuvis_trace_end_ctx_printf( unsigned int ctx, const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 2, 3 );
GPUVIS_EXTERN int gpuvis_trace_end_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap ) GPUVIS_ATTR_PRINTF( 2, 0 );

// Execute "trace-cmd start -b 2000 -D -i -e sched:sched_switch -e ..."
GPUVIS_EXTERN int gpuvis_start_tracing( unsigned int kbuffersize );
// Execute "trace-cmd extract"
GPUVIS_EXTERN int gpuvis_trigger_capture_and_keep_tracing( char *filename, size_t size );
// Execute "trace-cmd reset"
GPUVIS_EXTERN int gpuvis_stop_tracing( void );

// -1: tracing not setup, 0: tracing disabled, 1: tracing enabled.
GPUVIS_EXTERN int gpuvis_tracing_on( void );

// Get tracefs directory. Ie: /sys/kernel/tracing. Returns "" on error.
GPUVIS_EXTERN const char *gpuvis_get_tracefs_dir( void );

// Get tracefs file path in buf. Ie: /sys/kernel/tracing/trace_marker. Returns NULL on error.
GPUVIS_EXTERN const char *gpuvis_get_tracefs_filename( char *buf, size_t buflen, const char *file );

// Internal function used by GPUVIS_COUNT_HOT_FUNC_CALLS macro
GPUVIS_EXTERN void gpuvis_count_hot_func_calls_internal_( const char *func );

struct GpuvisTraceBlock;
static inline void gpuvis_trace_block_begin( struct GpuvisTraceBlock *block, const char *str );
static inline void gpuvis_trace_block_end( struct GpuvisTraceBlock *block );

struct GpuvisTraceBlockf;
static inline void gpuvis_trace_blockf_vbegin( struct GpuvisTraceBlockf *block, const char *fmt, va_list ap );
static inline void gpuvis_trace_blockf_begin( struct GpuvisTraceBlockf *block, const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 2, 3 );
static inline void gpuvis_trace_blockf_end( struct GpuvisTraceBlockf *block );

#define LNAME3( _name, _line ) _name ## _line
#define LNAME2( _name, _line ) LNAME3( _name, _line )
#define LNAME( _name ) LNAME2( _name, __LINE__ )

struct GpuvisTraceBlock
{
    uint64_t m_t0;
    const char *m_str;

#ifdef __cplusplus
    GpuvisTraceBlock( const char *str )
    {
        gpuvis_trace_block_begin( this, str );
    }

    ~GpuvisTraceBlock()
    {
        gpuvis_trace_block_end( this );
    }
#endif
};

struct GpuvisTraceBlockf
{
    uint64_t m_t0;
    char m_buf[ TRACE_BUF_SIZE ];

#ifdef __cplusplus
    GpuvisTraceBlockf( const char *fmt, ... ) GPUVIS_ATTR_PRINTF( 2, 3 )
    {
        va_list args;
        va_start( args, fmt );
        gpuvis_trace_blockf_vbegin( this, fmt, args );
        va_end( args );
    }

    ~GpuvisTraceBlockf()
    {
        gpuvis_trace_blockf_end( this );
    }
#endif
};

#ifdef __cplusplus

#define GPUVIS_TRACE_BLOCK( _conststr ) GpuvisTraceBlock LNAME( gpuvistimeblock )( _conststr )
#define GPUVIS_TRACE_BLOCKF( _fmt, ...  ) GpuvisTraceBlockf LNAME( gpuvistimeblock )( _fmt, __VA_ARGS__ )

#else

#if defined( __clang__ ) || defined( __GNUC__ )

#define GPUVIS_TRACE_BLOCKF_INIT( _unique, _fmt, ...  ) \
    ({ \
        struct GpuvisTraceBlockf _unique; \
        gpuvis_trace_blockf_begin( & _unique, _fmt, __VA_ARGS__ ); \
        _unique; \
     })

#define GPUVIS_TRACE_BLOCKF( _fmt,  ...) \
    GPUVIS_CLEANUP_FUNC( gpuvis_trace_blockf_end ) GPUVIS_MAY_BE_UNUSED struct GpuvisTraceBlockf LNAME( gpuvistimeblock ) = \
        GPUVIS_TRACE_BLOCKF_INIT( LNAME( gpuvistimeblock_init ), _fmt, __VA_ARGS__ )

#define GPUVIS_TRACE_BLOCK( _conststr ) \
    GPUVIS_CLEANUP_FUNC( gpuvis_trace_block_end ) GPUVIS_MAY_BE_UNUSED struct GpuvisTraceBlock LNAME( gpuvistimeblock ) = \
        {\
            .m_t0 = gpuvis_gettime_u64(), \
            .m_str = _conststr \
        }

#else

#define GPUVIS_TRACE_BLOCKF( _fmt,  ... )
#define GPUVIS_TRACE_BLOCK( _conststr )

#endif // __clang__ || __GNUC__

#endif // __cplusplus

static inline uint64_t gpuvis_gettime_u64( void )
{
    struct timespec ts;

    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ( ( uint64_t )ts.tv_sec * 1000000000LL) + ts.tv_nsec;
}

static inline void gpuvis_trace_block_finalize( uint64_t m_t0, const char *str )
{
    uint64_t dt = gpuvis_gettime_u64() - m_t0;

    // The cpu clock_gettime() functions seems to vary compared to the
    // ftrace event timestamps. If we don't reduce the duration here,
    // scopes oftentimes won't stack correctly when they're drawn.
    if ( dt > 11000 )
        dt -= 11000;

    gpuvis_trace_printf( "%s (lduration=-%lu)", str, dt );
}

static inline void gpuvis_trace_block_begin( struct GpuvisTraceBlock* block, const char *str )
{
    block->m_str = str;
    block->m_t0 = gpuvis_gettime_u64();
}

static inline void gpuvis_trace_block_end( struct GpuvisTraceBlock *block )
{
    gpuvis_trace_block_finalize(block->m_t0, block->m_str);
}

static inline void gpuvis_trace_blockf_vbegin( struct GpuvisTraceBlockf *block, const char *fmt, va_list ap)
{
    vsnprintf(block->m_buf, sizeof(block->m_buf), fmt, ap);
    block->m_t0 = gpuvis_gettime_u64();
}

static inline void gpuvis_trace_blockf_begin( struct GpuvisTraceBlockf *block, const char *fmt, ... )
{
    va_list args;

    va_start( args, fmt );
    gpuvis_trace_blockf_vbegin( block, fmt, args );
    va_end( args );
}

static inline void gpuvis_trace_blockf_end( struct GpuvisTraceBlockf *block )
{
    gpuvis_trace_block_finalize( block->m_t0, block->m_buf );
}

#define GPUVIS_COUNT_HOT_FUNC_CALLS() gpuvis_count_hot_func_calls_internal_( __func__ );

#else

static inline int gpuvis_trace_init() { return -1; }
static inline void gpuvis_trace_shutdown() {}

static inline int gpuvis_trace_printf( const char *fmt, ... ) { return 0; }
static inline int gpuvis_trace_vprintf( const char *fmt, va_list ap ) { return 0; }

static inline int gpuvis_trace_duration_printf( float duration, const char *fmt, ... ) { return 0; }
static inline int gpuvis_trace_duration_vprintf( float duration, const char *fmt, va_list ap ) { return 0; }

static inline int gpuvis_trace_begin_ctx_printf( unsigned int ctx, const char *fmt, ... ) { return 0; }
static inline int gpuvis_trace_begin_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap ) { return 0; }

static inline int gpuvis_trace_end_ctx_printf( unsigned int ctx, const char *fmt, ... ) { return 0; }
static inline int gpuvis_trace_end_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap ) { return 0; }

static inline int gpuvis_start_tracing( unsigned int kbuffersize ) { return 0; }
static inline int gpuvis_trigger_capture_and_keep_tracing( char *filename, size_t size ) { return 0; }
static inline int gpuvis_stop_tracing() { return 0; }

static inline int gpuvis_tracing_on() { return -1; }

static inline const char *gpuvis_get_tracefs_dir() { return ""; }
static inline const char *gpuvis_get_tracefs_filename( char *buf, size_t buflen, const char *file ) { return NULL; }

struct GpuvisTraceBlock;
static inline void gpuvis_trace_block_begin( struct GpuvisTraceBlock *block, const char *str ) {}
static inline void gpuvis_trace_block_end( struct GpuvisTraceBlock *block ) {}

struct GpuvisTraceBlockf;
static inline void gpuvis_trace_blockf_vbegin( struct GpuvisTraceBlockf *block, const char *fmt, va_list ap ) {}
static inline void gpuvis_trace_blockf_begin( struct GpuvisTraceBlockf *block, const char *fmt, ... ) {}
static inline void gpuvis_trace_blockf_end( struct GpuvisTraceBlockf *block ) {}

#define GPUVIS_TRACE_BLOCK( _conststr )
#define GPUVIS_TRACE_BLOCKF( _fmt, ...  )

#define GPUVIS_COUNT_HOT_FUNC_CALLS()

#endif // !GPUVIS_TRACE_UTILS_DISABLE

#if defined( GPUVIS_TRACE_IMPLEMENTATION ) && !defined( GPUVIS_TRACE_UTILS_DISABLE )

//////////////////////////////////////////////////////////////////////////////
//
//     IMPLEMENTATION SECTION
//

#define _GNU_SOURCE 1
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <sys/syscall.h>

#undef GPUVIS_EXTERN
#ifdef __cplusplus
#define GPUVIS_EXTERN extern "C"
#else
#define GPUVIS_EXTERN
#endif

#ifndef TRACEFS_MAGIC
#define TRACEFS_MAGIC      0x74726163
#endif

#define GPUVIS_STR( x ) #x
#define GPUVIS_STR_VALUE( x ) GPUVIS_STR( x )

static int g_trace_fd = -2;
static int g_tracefs_dir_inited = 0;
static char g_tracefs_dir[ PATH_MAX ];

#ifdef __cplusplus
#include <unordered_map>

struct funcinfo_t
{
    uint64_t tfirst = 0;
    uint64_t tlast = 0;
    uint32_t count = 0;
};
static std::unordered_map< pid_t, std::unordered_map< const char *, funcinfo_t > > g_hotfuncs;
#endif // __cplusplus

static pid_t gpuvis_gettid()
{
    return ( pid_t )syscall( SYS_gettid );
}

static int exec_tracecmd( const char *cmd )
{
    int ret;

    FILE *fh = popen( cmd, "r" );
    if ( !fh )
    {
        //$ TODO: popen() failed: errno
        ret = -1;
    }
    else
    {
        char buf[ 8192 ];

        while ( fgets( buf, sizeof( buf ), fh ) )
        {
            //$ TODO
            printf( "%s: %s", __func__, buf );
        }

        if ( feof( fh ) )
        {
            int pclose_ret = pclose( fh );

            ret = WEXITSTATUS( pclose_ret );
        }
        else
        {
            //$ TODO: Failed to read pipe to end: errno
            pclose( fh );
            ret = -1;
        }
    }

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_init()
{
    if ( g_trace_fd == -2 )
    {
        char filename[ PATH_MAX ];

        // The "trace_marker" file allows userspace to write into the ftrace buffer.
        if ( !gpuvis_get_tracefs_filename( filename, sizeof( filename ), "trace_marker" ) )
            g_trace_fd = -1;
        else
            g_trace_fd = open( filename, O_WRONLY );
    }

    return g_trace_fd;
}

#if !defined( __cplusplus )
static void flush_hot_func_calls()
{
    //$ TODO: hot func calls for C
}
#else
static void flush_hot_func_calls()
{
    if ( g_hotfuncs.empty() )
        return;

    uint64_t t0 = gpuvis_gettime_u64();

    for ( auto &x : g_hotfuncs )
    {
        for ( auto &y : x.second )
        {
            if ( y.second.count )
            {
                pid_t tid = x.first;
                const char *func = y.first;
                uint64_t offset = t0 - y.second.tfirst;
                uint64_t duration = y.second.tlast - y.second.tfirst;

                gpuvis_trace_printf( "%s calls:%u (lduration=%lu tid=%d offset=-%lu)\n",
                                     func, y.second.count, duration, tid, offset );
            }
        }
    }

    g_hotfuncs.clear();
}

GPUVIS_EXTERN void gpuvis_count_hot_func_calls_internal_( const char *func )
{
    static THREAD_LOCAL pid_t s_tid = gpuvis_gettid();

    uint64_t t0 = gpuvis_gettime_u64();
    auto &x = g_hotfuncs[ s_tid ];
    auto &y = x[ func ];

    if ( !y.count )
    {
        y.count = 1;
        y.tfirst = t0;
        y.tlast = t0 + 1;
    }
    else if ( t0 - y.tlast >= 3 * 1000000 ) // 3ms
    {
        gpuvis_trace_printf( "%s calls:%u (lduration=%lu offset=-%lu)\n",
                             func, y.count, y.tlast - y.tfirst, t0 - y.tfirst );

        y.count = 1;
        y.tfirst = t0;
        y.tlast = t0 + 1;
    }
    else
    {
        y.tlast = t0;
        y.count++;
    }
}
#endif // __cplusplus

GPUVIS_EXTERN void gpuvis_trace_shutdown()
{
    flush_hot_func_calls();

    if ( g_trace_fd >= 0 )
        close( g_trace_fd );
    g_trace_fd = -2;

    g_tracefs_dir_inited = 0;
    g_tracefs_dir[ 0 ] = 0;
}

static int trace_printf_impl( const char *keystr, const char *fmt, va_list ap ) GPUVIS_ATTR_PRINTF( 2, 0 );
static int trace_printf_impl( const char *keystr, const char *fmt, va_list ap )
{
    int ret = -1;

    if ( gpuvis_trace_init() >= 0 )
    {
        int n;
        char buf[ TRACE_BUF_SIZE ];

        n = vsnprintf( buf, sizeof( buf ), fmt, ap );

        if ( ( n > 0 ) || ( !n && keystr ) )
        {
            if ( ( size_t )n >= sizeof( buf ) )
                n = sizeof( buf ) - 1;

            if ( keystr && keystr[ 0 ] )
            {
                int keystrlen = strlen( keystr );

                if ( ( size_t )n + keystrlen >= sizeof( buf ) )
                    n = sizeof( buf ) - keystrlen - 1;

                strcpy( buf + n, keystr );

                n += keystrlen;
            }

            ret = write( g_trace_fd, buf, n );
        }
    }

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_printf( const char *fmt, ... )
{
    int ret;
    va_list ap;

    va_start( ap, fmt );
    ret = gpuvis_trace_vprintf( fmt, ap );
    va_end( ap );

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_vprintf( const char *fmt, va_list ap )
{
    return trace_printf_impl( NULL, fmt, ap );
}

GPUVIS_EXTERN int gpuvis_trace_duration_printf( float duration, const char *fmt, ... )
{
    int ret;
    va_list ap;

    va_start( ap, fmt );
    ret = gpuvis_trace_duration_vprintf( duration, fmt, ap );
    va_end( ap );

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_duration_vprintf( float duration, const char *fmt, va_list ap )
{
    char keystr[ 128 ];

    snprintf( keystr, sizeof( keystr ), " (duration=%f)", duration ); //$ TODO: Try this with more precision?

    return trace_printf_impl( keystr, fmt, ap );
}

GPUVIS_EXTERN int gpuvis_trace_begin_ctx_printf( unsigned int ctx, const char *fmt, ... )
{
    int ret;
    va_list ap;

    va_start( ap, fmt );
    ret = gpuvis_trace_begin_ctx_vprintf( ctx, fmt, ap );
    va_end( ap );

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_begin_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap )
{
    char keystr[ 128 ];

    snprintf( keystr, sizeof( keystr ), " (begin_ctx=%u)", ctx );

    return trace_printf_impl( keystr, fmt, ap );
}

GPUVIS_EXTERN int gpuvis_trace_end_ctx_printf( unsigned int ctx, const char *fmt, ... )
{
    int ret;
    va_list ap;

    va_start( ap, fmt );
    ret = gpuvis_trace_end_ctx_vprintf( ctx, fmt, ap );
    va_end( ap );

    return ret;
}

GPUVIS_EXTERN int gpuvis_trace_end_ctx_vprintf( unsigned int ctx, const char *fmt, va_list ap )
{
    char keystr[ 128 ];

    snprintf( keystr, sizeof( keystr ), " (end_ctx=%u)", ctx );

    return trace_printf_impl( keystr, fmt, ap );
}

GPUVIS_EXTERN int gpuvis_start_tracing( unsigned int kbuffersize )
{
    static const char fmt[] =
        "trace-cmd start -b %u -D -i "
        // https://github.com/mikesart/gpuvis/wiki/TechDocs-Linux-Scheduler
        " -e sched:sched_switch"
        " -e sched:sched_process_fork"
        " -e sched:sched_process_exec"
        " -e sched:sched_process_exit"
        " -e drm:drm_vblank_event"
        " -e drm:drm_vblank_event_queued"
        " -e drm:drm_vblank_event_delivered"
        // https://github.com/mikesart/gpuvis/wiki/TechDocs-AMDGpu
        " -e amdgpu:amdgpu_vm_flush"
        " -e amdgpu:amdgpu_cs_ioctl"
        " -e amdgpu:amdgpu_sched_run_job"
        " -e *fence:*fence_signaled"
        // https://github.com/mikesart/gpuvis/wiki/TechDocs-Intel
        " -e i915:i915_flip_request"
        " -e i915:i915_flip_complete"
        " -e i915:intel_gpu_freq_change"
        " -e i915:i915_gem_request_add"
        " -e i915:i915_gem_request_submit"  // Require CONFIG_DRM_I915_LOW_LEVEL_TRACEPOINTS
        " -e i915:i915_gem_request_in"      // Kconfig option to be enabled.
        " -e i915:i915_gem_request_out"     //
        " -e i915:intel_engine_notify"
        " -e i915:i915_gem_request_wait_begin"
        " -e i915:i915_gem_request_wait_end 2>&1";
    char cmd[ 8192 ];

    if ( !kbuffersize )
        kbuffersize = 16 * 1024;

    snprintf( cmd, sizeof( cmd ), fmt, kbuffersize );

    return exec_tracecmd( cmd );
}

GPUVIS_EXTERN int gpuvis_trigger_capture_and_keep_tracing( char *filename, size_t size )
{
    int ret = -1;

    if ( filename )
        filename[ 0 ] = 0;

    flush_hot_func_calls();

    if ( gpuvis_tracing_on() )
    {
        char datetime[ 128 ];
        char cmd[ PATH_MAX ];
        char exebuf[ PATH_MAX ];
        const char *exename = NULL;
        time_t t = time( NULL );
        struct tm *tmp = localtime( &t );

        strftime( datetime, sizeof( datetime ), "%Y-%m-%d_%H-%M-%S", tmp );
        datetime[ sizeof( datetime ) - 1 ] = 0;

        ssize_t cbytes = readlink( "/proc/self/exe", exebuf, sizeof( exebuf ) - 1 );
        if ( cbytes > 0 )
        {
            exebuf[ cbytes ] = 0;
            exename = strrchr( exebuf, '/' );
        }
        exename = exename ? ( exename + 1 ) : "trace";

        // Stop tracing
        exec_tracecmd( "trace-cmd stop 2>&1" );

        // Save the trace data to something like "glxgears_2017-10-13_17-52-56.dat"
        snprintf( cmd, sizeof( cmd ),
                  "trace-cmd extract -k -o \"%s_%s.dat\" > /tmp/blah.log 2>&1 &",
                  exename, datetime );
        cmd[ sizeof( cmd ) - 1 ] = 0;

        ret = system( cmd );

        if ( filename && !ret )
            snprintf( filename, size, "%s_%s.dat", exename, datetime );

        // Restart tracing
        exec_tracecmd( "trace-cmd restart 2>&1" );
    }

    return ret;
}

GPUVIS_EXTERN int gpuvis_stop_tracing()
{
    flush_hot_func_calls();

    int ret = exec_tracecmd( "trace-cmd reset 2>&1");

    // Try freeing any snapshot buffers as well
    exec_tracecmd( "trace-cmd snapshot -f 2>&1" );

    return ret;
}

GPUVIS_EXTERN int gpuvis_tracing_on()
{
    int ret = -1;
    char buf[ 32 ];
    char filename[ PATH_MAX ];

    if ( gpuvis_get_tracefs_filename( filename, PATH_MAX, "tracing_on" ) )
    {
        int fd = open( filename, O_RDONLY );

        if ( fd >= 0 )
        {
            if ( read( fd, buf, sizeof( buf ) ) > 0 )
                ret = atoi( buf );

            close( fd );
        }
    }

    return ret;
}

static int is_tracefs_dir( const char *dir )
{
    struct statfs stat;

    return !statfs( dir, &stat ) && ( stat.f_type == TRACEFS_MAGIC );
}

GPUVIS_EXTERN const char *gpuvis_get_tracefs_dir()
{
    if ( !g_tracefs_dir_inited )
    {
        size_t i;
        static const char *tracefs_dirs[] =
        {
            "/sys/kernel/tracing",
            "/sys/kernel/debug/tracing",
            "/tracing",
            "/trace",
        };

        for ( i = 0; i < sizeof( tracefs_dirs ) / sizeof( tracefs_dirs[ 0 ] ); i++ )
        {
            if ( is_tracefs_dir( tracefs_dirs[ i ] ) )
            {
                strncpy( g_tracefs_dir, tracefs_dirs[ i ], PATH_MAX );
                g_tracefs_dir[ PATH_MAX - 1 ] = 0;
                break;
            }
        }

        if ( !g_tracefs_dir[ 0 ] )
        {
            FILE *fp;
            char type[ 128 ];
            char dir[ PATH_MAX + 1 ];

            fp = fopen( "/proc/mounts", "r" );
            if ( fp )
            {
                while ( fscanf( fp, "%*s %" GPUVIS_STR_VALUE( PATH_MAX ) "s %127s %*s %*d %*d\n", dir, type ) == 2 )
                {
                    if ( !strcmp( type, "tracefs" ) && is_tracefs_dir( dir ) )
                    {
                        strncpy( g_tracefs_dir, dir, PATH_MAX );
                        g_tracefs_dir[ PATH_MAX - 1 ] = 0;
                        break;
                    }
                }

                fclose( fp );
            }
        }

        g_tracefs_dir_inited = 1;
    }

    return g_tracefs_dir;
}

GPUVIS_EXTERN const char *gpuvis_get_tracefs_filename( char *buf, size_t buflen, const char *file )
{
    const char *tracefs_dir = gpuvis_get_tracefs_dir();

    if ( tracefs_dir[ 0 ] )
    {
        snprintf( buf, buflen, "%s/%s", tracefs_dir, file );
        buf[ buflen - 1 ] = 0;

        return buf;
    }

    return NULL;
}

#endif // GPUVIS_TRACE_IMPLEMENTATION

#endif // _GPUVIS_TRACE_UTILS_H_
