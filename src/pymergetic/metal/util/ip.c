/*
 * pm_metal_util_ip4_* — impl: common (see util/ip.h; wasm32 mods reach
 * this same code via wasi-style import registration below).
 */
#include "pymergetic/metal/util/ip.h"

int
pm_metal_util_ip4_parse(const char *s, uint32_t *out_host)
{
	unsigned a, b, c, d, v;
	unsigned *n;
	const char *p;

	if (s == NULL) {
		return -1;
	}

	a = b = c = d = 0;
	p = s;
	n = &a;
	v = 0;
	for (;;) {
		if (*p >= '0' && *p <= '9') {
			v = v * 10u + (unsigned)(*p - '0');
			if (v > 255u) {
				return -1;
			}

			p++;
			continue;
		}

		*n = v;
		if (*p == '.') {
			if (n == &a) {
				n = &b;
			} else if (n == &b) {
				n = &c;
			} else if (n == &c) {
				n = &d;
			} else {
				return -1;
			}

			v = 0;
			p++;
			continue;
		}

		if (*p == '\0') {
			if (n != &d) {
				return -1;
			}

			*n = v;
			if (out_host != NULL) {
				*out_host = ((uint32_t)a << 24) | ((uint32_t)b << 16)
					    | ((uint32_t)c << 8) | (uint32_t)d;
			}

			return 0;
		}

		return -1;
	}
}

static int
Ip4PutOctet(char *out, size_t cap, size_t *off, unsigned v)
{
	char tmp[3];
	unsigned n;
	unsigned i;

	n = 0;
	if (v >= 100u) {
		tmp[n++] = (char)('0' + (v / 100u));
	}

	if (v >= 10u) {
		tmp[n++] = (char)('0' + ((v / 10u) % 10u));
	}

	tmp[n++] = (char)('0' + (v % 10u));

	if (*off + n >= cap) {
		return -1;
	}

	for (i = 0; i < n; i++) {
		out[(*off)++] = tmp[i];
	}

	return 0;
}

int
pm_metal_util_ip4_format(uint32_t host, char *out, size_t cap)
{
	size_t off;
	unsigned o[4];
	unsigned i;

	if (out == NULL || cap == 0) {
		return -1;
	}

	o[0] = (host >> 24) & 0xffu;
	o[1] = (host >> 16) & 0xffu;
	o[2] = (host >> 8) & 0xffu;
	o[3] = host & 0xffu;

	off = 0;
	for (i = 0; i < 4u; i++) {
		if (i > 0u) {
			if (off + 1u >= cap) {
				return -1;
			}

			out[off++] = '.';
		}

		if (Ip4PutOctet(out, cap, &off, o[i]) != 0) {
			return -1;
		}
	}

	if (off >= cap) {
		return -1;
	}

	out[off] = '\0';
	return (int)off;
}

#if !defined(__wasm__)
#include "wasm_export.h"

static int32_t
pm_metal_util_ip4_parse_native(wasm_exec_env_t exec_env, char *s, uint32_t *out_host)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_ip4_parse(s, out_host);
}

static int32_t
pm_metal_util_ip4_format_native(wasm_exec_env_t exec_env, uint32_t host, char *out,
				uint32_t cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_ip4_format(host, out, (size_t)cap);
}

static NativeSymbol g_pm_metal_util_ip_native_symbols[] = {
	{ "pm_metal_util_ip4_parse", (void *)pm_metal_util_ip4_parse_native, "($*)i",
	  NULL },
	{ "pm_metal_util_ip4_format", (void *)pm_metal_util_ip4_format_native, "(i*~)i",
	  NULL },
};

int
pm_metal_util_ip_native_register(void)
{
	if (!wasm_runtime_register_natives(
		    PM_METAL_UTIL_IP_WASI_MODULE, g_pm_metal_util_ip_native_symbols,
		    sizeof(g_pm_metal_util_ip_native_symbols)
			    / sizeof(g_pm_metal_util_ip_native_symbols[0]))) {
		return -1;
	}

	return 0;
}
#endif
