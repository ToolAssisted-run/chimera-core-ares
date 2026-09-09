/* run-wbx - drives core.wbx through the miniBox host on the same schedule as
 * run-native, reporting the same digests, so the two diff directly. Every
 * regular file in the work dir is mounted into the guest under its basename -
 * exactly what the frontend does with a project's files, slot map and settings.
 *
 * usage: run-wbx <core.wbx> <workdir> [run-native's run options] [--rerecord]
 *
 * --rerecord round-trips the WHOLE guest machine through the host's save/load
 * around every frame; the digests must come out identical. That is the leg that
 * catches anything of the machine living outside the arena.
 */
#include "minibox.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "machines-names.h"
#include "gate-harness.h"

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s)
{
	return (intptr_t)fread(d, 1, s, ((freader *)ud)->f);
}

typedef struct { uint8_t *b; size_t len, cap, pos; } membuf;

static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	if (m->len + n > m->cap) { m->cap = (m->len + n) * 2 + 64; m->b = (uint8_t *)realloc(m->b, m->cap); }
	memcpy(m->b + m->len, d, n);
	m->len += n;
	return 0;
}

static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	uintptr_t avail = m->len - m->pos;
	if (n > avail) n = avail;
	memcpy(d, m->b + m->pos, n);
	m->pos += n;
	return (intptr_t)n;
}

typedef int (MB_GUEST_ABI *intfn)(void);
typedef void (MB_GUEST_ABI *framefn)(uint64_t);
typedef void (MB_GUEST_ABI *setfn)(int32_t, int32_t);
typedef void (MB_GUEST_ABI *voidfn_i)(int);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i)(int);
typedef int64_t (MB_GUEST_ABI *i64fn_i)(int);
typedef int (MB_GUEST_ABI *intfn_i)(int);
typedef int (MB_GUEST_ABI *intfn_ii)(int, int);
typedef int64_t (MB_GUEST_ABI *i64fn)(void);

static mb_host *g_host;
static intfn g_Init, g_GetVideoWidth, g_GetVideoHeight, g_GetAudioSampleCount;
static intfn g_InputWasRead, g_GetMemoryDomainCount, g_GetBusCount;
static intfn_i g_IsButtonActive, g_IsAxisActive, g_GetBusWritable;
static ptrfn_i g_GetBusName;
static i64fn_i g_GetBusSize;
static intfn_ii g_PeekBus;
static i64fn g_CaptureState;
static ptrfn g_GetStateBuffer;
static ptrfn g_GetLoadError, g_GetVideoBgra, g_GetAudio;
static setfn g_SetButton, g_SetAxis;
static framefn g_FrameAdvance;
static ptrfn_i g_GetMemoryDomainName, g_GetMemoryDomainPtr;
static i64fn_i g_GetMemoryDomainSize;
static voidfn_i g_SetRenderingEnabled;
static int g_rerecord;
static membuf g_state;

static uintptr_t proc(mb_host *h, const char *n)
{
	mb_return r;
	wbx_get_proc_addr(h, n, &r);
	if (r.error_message[0]) { fprintf(stderr, "proc %s: %s\n", n, r.error_message); exit(2); }
	if (!r.data) { fprintf(stderr, "missing required export %s\n", n); exit(2); }
	return r.data;
}

static int core_init(void) { return 1; }  /* Init ran before Seal */
static int core_button_count(void) { return machines::buttonCount(); }
static const char *core_button_name(int i) { return machines::buttonName(i); }
static int core_axis_count(void) { return machines::axisCount(); }
static const char *core_axis_name(int i) { return machines::axisName(i); }
static int core_bus_count(void) { return g_GetBusCount(); }
static const char *core_bus_name(int i) { return (const char *)g_GetBusName(i); }
static int64_t core_bus_size(int i) { return g_GetBusSize(i); }
static int core_bus_peek(int b, int a) { return g_PeekBus(b, a); }

static int core_state(const uint8_t **data, int64_t *size)
{
	int64_t n = g_CaptureState();
	if (n <= 0) return 0;
	*data = (const uint8_t *)g_GetStateBuffer();
	*size = n;
	return *data != NULL;
}
static const char *core_load_error(void) { return (const char *)g_GetLoadError(); }
static void core_set_button(int32_t i, int32_t s) { g_SetButton(i, s); }
static void core_set_axis(int32_t i, int32_t v) { g_SetAxis(i, v); }
static void core_frame(void) { g_FrameAdvance(0); }
static void core_set_rendering(int on) { g_SetRenderingEnabled(on); }

static const uint32_t *core_video(int *w, int *h)
{
	*w = g_GetVideoWidth();
	*h = g_GetVideoHeight();
	return (const uint32_t *)g_GetVideoBgra();
}

static const int16_t *core_audio(int *n)
{
	*n = g_GetAudioSampleCount();
	return (const int16_t *)g_GetAudio();
}

static int core_input_was_read(void) { return g_InputWasRead(); }
static int core_domain_count(void) { return g_GetMemoryDomainCount(); }
static const char *core_domain_name(int i) { return (const char *)g_GetMemoryDomainName(i); }
static const uint8_t *core_domain_ptr(int i) { return (const uint8_t *)g_GetMemoryDomainPtr(i); }
static int64_t core_domain_size(int i) { return g_GetMemoryDomainSize(i); }

static void core_pre_frame(void)
{
	if (!g_rerecord) return;
	mb_return r;
	g_state.len = 0;
	wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
	if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
	g_state.pos = 0;
	wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
	if (r.error_message[0]) { fprintf(stderr, "load_state: %s\n", r.error_message); exit(1); }
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: run-wbx <core.wbx> <workdir> [options] [--rerecord]\n");
		gate_usage();
		return 2;
	}
	const char *wbxPath = argv[1];
	const char *workdir = argv[2];
	const char *machineId = "N64";
	for (int i = 3; i < argc; i++)
	{
		if (!strcmp(argv[i], "--rerecord")) g_rerecord = 1;
		if (!strcmp(argv[i], "--machine") && i + 1 < argc) machineId = argv[i + 1];
	}
	/* The guest knows its buttons by index; the gate says --hold Start. Both
	 * read the same generated table, so the two agree by construction. */
	machines::useMachine(machineId);

	struct gate_opts opts;
	if (!gate_parse_opts(argc, argv, 3, &opts)) return 2;

	FILE *wf = fopen(wbxPath, "rb");
	if (!wf) { perror(wbxPath); return 1; }

	/* matches waterbox.config memoryLayoutMiB */
	mb_memory_layout_template layout = { 16u << 20, 4u << 20, 16u << 20, 4u << 20, 384u << 20 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); return 1; }
	g_host = (mb_host *)r.data;

	DIR *d = opendir(workdir);
	if (!d) { perror(workdir); return 1; }
	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", workdir, de->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		/* Things the DRIVER wrote, not the guest's. */
		const char *dot = strrchr(de->d_name, '.');
		if (dot && (!strcmp(dot, ".ppm") || !strcmp(dot, ".png") || !strcmp(dot, ".txt"))) continue;
		FILE *f = fopen(path, "rb");
		if (!f) { perror(path); return 1; }
		freader rd = { f };
		wbx_mount_file(g_host, de->d_name, file_read, (uintptr_t)&rd, false, &r);
		fclose(f);
		if (r.error_message[0]) { fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message); return 1; }
	}
	closedir(d);

	wbx_activate_host(g_host, &r);

	g_Init = (intfn)proc(g_host, "Init");
	g_GetLoadError = (ptrfn)proc(g_host, "GetLoadError");
	g_SetButton = (setfn)proc(g_host, "SetButton");
	g_SetAxis = (setfn)proc(g_host, "SetAxis");
	g_FrameAdvance = (framefn)proc(g_host, "FrameAdvance");
	g_GetVideoBgra = (ptrfn)proc(g_host, "GetVideoBgra");
	g_GetVideoWidth = (intfn)proc(g_host, "GetVideoWidth");
	g_GetVideoHeight = (intfn)proc(g_host, "GetVideoHeight");
	g_GetAudio = (ptrfn)proc(g_host, "GetAudio");
	g_GetAudioSampleCount = (intfn)proc(g_host, "GetAudioSampleCount");
	g_InputWasRead = (intfn)proc(g_host, "InputWasRead");
	g_GetMemoryDomainCount = (intfn)proc(g_host, "GetMemoryDomainCount");
	g_GetMemoryDomainName = (ptrfn_i)proc(g_host, "GetMemoryDomainName");
	g_GetMemoryDomainPtr = (ptrfn_i)proc(g_host, "GetMemoryDomainPtr");
	g_GetMemoryDomainSize = (i64fn_i)proc(g_host, "GetMemoryDomainSize");
	g_SetRenderingEnabled = (voidfn_i)proc(g_host, "SetRenderingEnabled");
	g_GetBusCount = (intfn)proc(g_host, "GetBusCount");
	g_GetBusName = (ptrfn_i)proc(g_host, "GetBusName");
	g_GetBusSize = (i64fn_i)proc(g_host, "GetBusSize");
	g_GetBusWritable = (intfn_i)proc(g_host, "GetBusWritable");
	g_PeekBus = (intfn_ii)proc(g_host, "PeekBus");
	g_CaptureState = (i64fn)proc(g_host, "CaptureState");
	g_GetStateBuffer = (ptrfn)proc(g_host, "GetStateBuffer");

	/* Init runs before Seal - the loaded machine is the sealed baseline. */
	if (g_Init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", (const char *)g_GetLoadError());
		return 1;
	}

	wbx_deactivate_host(g_host, &r);
	wbx_seal(g_host, &r);
	if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); return 1; }
	wbx_activate_host(g_host, &r);

	struct gate_core core = {
		core_init, core_load_error, core_button_count, core_button_name,
		core_axis_count, core_axis_name, core_set_button, core_set_axis, core_frame,
		core_video, core_audio, core_input_was_read,
		core_domain_count, core_domain_name, core_domain_ptr, core_domain_size,
		core_bus_count, core_bus_name, core_bus_size, core_bus_peek, core_state,
		core_set_rendering, core_pre_frame,
	};
	int ret = gate_run(&core, &opts);

	wbx_deactivate_host(g_host, &r);
	wbx_destroy_host(g_host, &r);
	free(g_state.b);
	return ret;
}
