/*
 * Runtime — export-style env inheritance. See env.h.
 */
#include "pymergetic/metal/runtime/env.h"

#include <stdlib.h>
#include <string.h>

void pm_metal_env_free_exported(char **envp, int envc)
{
	int i;

	for (i = 0; i < envc; i++) {
		free(envp[i]);
	}
	free(envp);
}

int pm_metal_env_build_exported(const pm_metal_env_var_t *vars, int count, char ***out_envp, int *out_envc)
{
	if (!out_envp || !out_envc || count < 0 || (count > 0 && !vars)) {
		return -1;
	}

	int exported_count = 0;
	int i;

	for (i = 0; i < count; i++) {
		if (vars[i].exported) {
			exported_count++;
		}
	}

	if (exported_count == 0) {
		*out_envp = NULL;
		*out_envc = 0;
		return 0;
	}

	char **envp = malloc(sizeof(char *) * (size_t)exported_count);

	if (!envp) {
		return -1;
	}

	int built = 0;

	for (i = 0; i < count; i++) {
		if (!vars[i].exported) {
			continue;
		}
		if (!vars[i].name || !vars[i].value) {
			pm_metal_env_free_exported(envp, built);
			return -1;
		}

		size_t name_len = strlen(vars[i].name);
		size_t value_len = strlen(vars[i].value);
		char *entry = malloc(name_len + 1 + value_len + 1);

		if (!entry) {
			pm_metal_env_free_exported(envp, built);
			return -1;
		}
		memcpy(entry, vars[i].name, name_len);
		entry[name_len] = '=';
		memcpy(entry + name_len + 1, vars[i].value, value_len + 1);
		envp[built++] = entry;
	}

	*out_envp = envp;
	*out_envc = built;
	return 0;
}
