/*
 * End-to-end test for runtime/process.h's spawn()/wait()/kill() —
 * proves kill() actually stops a genuinely still-running guest (not one
 * that already happened to finish on its own), by racing it against
 * mods/t5_spin.wasm's own infinite loop. Not part of the normal build —
 * see scripts/verify-linux-process.sh. Host harness (built by the Linux
 * CMake today): calls usleep()/sleep() directly (same reasoning as
 * thread_stress_test.c calling pthread — native host test code, not
 * the portable common layer).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pymergetic/metal/port/pipe.h"
#include "pymergetic/metal/runtime/env.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"

int main(void)
{
	const char *vfs_root = getenv("PM_METAL_TEST_VFS_ROOT");
	if (!vfs_root) {
		fprintf(stderr, "PM_METAL_TEST_VFS_ROOT not set\n");
		return 1;
	}

	/* Loopback IPv4 + IPv6 for TCP/UDP socket mods; ns_lookup_pool for
	 * t28_dns_lookup's getaddrinfo("localhost"). See runtime.h. */
	static const char *addr_pool[] = { "127.0.0.1/32", "::1/128" };
	static const char *ns_lookup_pool[] = { "localhost" };
	pm_metal_runtime_config_t cfg = {
		.memory_bytes = 64 * 1024 * 1024,
		.bytecode_bytes = 4 * 1024 * 1024,
		.vfs_root = vfs_root,
		.addr_pool = addr_pool,
		.addr_pool_count = 2,
		.ns_lookup_pool = ns_lookup_pool,
		.ns_lookup_pool_count = 1,
	};

	if (pm_metal_runtime_init(&cfg) != 0) {
		fprintf(stderr, "runtime init failed\n");
		return 1;
	}
	if (pm_metal_process_init() != 0) {
		fprintf(stderr, "process init failed\n");
		pm_metal_runtime_shutdown();
		return 1;
	}

	int rc = 1;

	/* --- getpid()-via-env: spawn() must inject PID=<n> even with no
	 * caller-supplied env at all. --- */
	pm_metal_runtime_handle_t h_getpid;
	if (pm_metal_runtime_load_file("/mods/t4_getpid.wasm", &h_getpid) != 0) {
		fprintf(stderr, "load t4_getpid failed\n");
		goto done;
	}
	char *getpid_argv[1] = { "t4_getpid" };
	pm_metal_process_id_t getpid_pid;
	if (pm_metal_process_spawn(h_getpid, 1, getpid_argv, 0, NULL, -1, -1, -1, NULL, NULL, &getpid_pid) != 0) {
		fprintf(stderr, "spawn t4_getpid failed\n");
		pm_metal_runtime_unload(h_getpid);
		goto done;
	}
	int getpid_exit;
	if (pm_metal_process_wait(getpid_pid, &getpid_exit) != 0 || getpid_exit != 0) {
		fprintf(stderr, "t4_getpid did not exit cleanly (exit=%d)\n", getpid_exit);
		pm_metal_runtime_unload(h_getpid);
		goto done;
	}
	printf("process_test: t4_getpid own pid was %u, exit=%d\n", getpid_pid.pid, getpid_exit);
	pm_metal_runtime_unload(h_getpid);

	/* --- kill(): must stop a genuinely still-running guest. --- */
	pm_metal_runtime_handle_t h_spin;
	if (pm_metal_runtime_load_file("/mods/t5_spin.wasm", &h_spin) != 0) {
		fprintf(stderr, "load t5_spin failed\n");
		goto done;
	}

	char *spin_argv[1] = { "t5_spin" };
	pm_metal_process_id_t spin_pid;
	if (pm_metal_process_spawn(h_spin, 1, spin_argv, 0, NULL, -1, -1, -1, NULL, NULL, &spin_pid) != 0) {
		fprintf(stderr, "spawn t5_spin failed\n");
		pm_metal_runtime_unload(h_spin);
		goto done;
	}

	/* Give the worker thread time to actually reach run_ex()'s
	 * instantiate()+execute_main() — kill() on a pid that hasn't
	 * published its exec token yet is a documented no-op, not a bug,
	 * but the point of this test is to prove the *running* case. */
	usleep(200 * 1000);

	int still_running_check;
	if (pm_metal_process_try_wait(spin_pid, &still_running_check) != 0) {
		fprintf(stderr, "t5_spin was not still running before kill() — test is not exercising kill() at all\n");
		pm_metal_runtime_unload(h_spin);
		goto done;
	}

	/* WAMR prints "Exception: terminated by user" — that is the kill. */
	printf("process_test: killing t5_spin (expected Exception follows)\n");
	if (pm_metal_process_kill(spin_pid) != 0) {
		fprintf(stderr, "kill(t5_spin) failed\n");
		pm_metal_runtime_unload(h_spin);
		goto done;
	}

	int spin_exit;
	if (pm_metal_process_wait(spin_pid, &spin_exit) != 0) {
		fprintf(stderr, "wait(t5_spin) after kill() failed\n");
		pm_metal_runtime_unload(h_spin);
		goto done;
	}

	/* run_ex() reports a terminated instance the same way it reports
	 * any other trap: exit_code == -1 (see runtime.c's own execute_main()
	 * failure path) — never 0, since t5_spin's own main() never returns
	 * on its own. */
	printf("process_test: t5_spin exit_code after kill() = %d (expected)\n", spin_exit);
	if (spin_exit == 0) {
		fprintf(stderr, "t5_spin exited 0 — it must have finished on its own, kill() proved nothing\n");
		pm_metal_runtime_unload(h_spin);
		goto done;
	}

	pm_metal_runtime_unload(h_spin);

	/* --- pipe(): "A's stdout -> B's stdin" via two spawn()s and one
	 * host pipe, no dup()ing (see port/pipe.h's own file header). --- */
	int64_t pipe_read_fd = -1, pipe_write_fd = -1;
	if (pm_metal_port_pipe(&pipe_read_fd, &pipe_write_fd) != 0) {
		fprintf(stderr, "pipe() failed\n");
		goto done;
	}

	pm_metal_runtime_handle_t h_writer, h_reader;
	if (pm_metal_runtime_load_file("/mods/t6_pipe_writer.wasm", &h_writer) != 0) {
		fprintf(stderr, "load t6_pipe_writer failed\n");
		pm_metal_port_close(pipe_read_fd);
		pm_metal_port_close(pipe_write_fd);
		goto done;
	}
	if (pm_metal_runtime_load_file("/mods/t7_pipe_reader.wasm", &h_reader) != 0) {
		fprintf(stderr, "load t7_pipe_reader failed\n");
		pm_metal_runtime_unload(h_writer);
		pm_metal_port_close(pipe_read_fd);
		pm_metal_port_close(pipe_write_fd);
		goto done;
	}

	char *writer_argv[1] = { "t6_pipe_writer" };
	char *reader_argv[1] = { "t7_pipe_reader" };
	pm_metal_process_id_t writer_pid, reader_pid;

	/* Order doesn't matter here — each end is only ever touched by
	 * the one process it was handed to (see process.h's spawn() doc
	 * comment), so there is no "writer must start first" requirement:
	 * the reader's fgets() simply blocks until either bytes or EOF
	 * arrive, whichever spawn() happens to reach instantiate() first. */
	if (pm_metal_process_spawn(h_writer, 1, writer_argv, 0, NULL, -1, pipe_write_fd, -1, NULL, NULL, &writer_pid)
	    != 0) {
		fprintf(stderr, "spawn t6_pipe_writer failed\n");
		pm_metal_runtime_unload(h_writer);
		pm_metal_runtime_unload(h_reader);
		pm_metal_port_close(pipe_read_fd);
		pm_metal_port_close(pipe_write_fd);
		goto done;
	}
	pipe_write_fd = -1; /* handed to writer */
	if (pm_metal_process_spawn(h_reader, 1, reader_argv, 0, NULL, pipe_read_fd, -1, -1, NULL, NULL, &reader_pid)
	    != 0) {
		int writer_exit = -1;

		fprintf(stderr, "spawn t7_pipe_reader failed\n");
		(void)pm_metal_process_kill(writer_pid);
		(void)pm_metal_process_wait(writer_pid, &writer_exit);
		pm_metal_runtime_unload(h_writer);
		pm_metal_runtime_unload(h_reader);
		pm_metal_port_close(pipe_read_fd);
		goto done;
	}
	pipe_read_fd = -1; /* handed to reader */

	int writer_exit, reader_exit;
	int wait_writer = pm_metal_process_wait(writer_pid, &writer_exit);
	int wait_reader = pm_metal_process_wait(reader_pid, &reader_exit);

	if (wait_writer != 0 || wait_reader != 0) {
		fprintf(stderr, "wait() on pipe pair failed\n");
		pm_metal_runtime_unload(h_writer);
		pm_metal_runtime_unload(h_reader);
		goto done;
	}

	pm_metal_runtime_unload(h_writer);
	pm_metal_runtime_unload(h_reader);

	printf("process_test: pipe pair exit codes writer=%d reader=%d\n", writer_exit, reader_exit);
	if (writer_exit != 0 || reader_exit != 0) {
		fprintf(stderr, "pipe pair did not both exit 0\n");
		goto done;
	}

	/* --- export-style env: a "subshell" respawn only ever sees the
	 * exported half of its parent's variable table (runtime/env.h). --- */
	pm_metal_env_var_t vars[2] = {
		{ "LOCAL_ONLY", "should-not-leak", 0 },
		{ "EXPORTED_VAR", "should-be-visible", 1 },
	};
	char **exported_envp;
	int exported_envc;

	if (pm_metal_env_build_exported(vars, 2, &exported_envp, &exported_envc) != 0 || exported_envc != 1) {
		fprintf(stderr, "build_exported failed or wrong count (%d)\n", exported_envc);
		goto done;
	}

	pm_metal_runtime_handle_t h_env;
	if (pm_metal_runtime_load_file("/mods/t2_env.wasm", &h_env) != 0) {
		fprintf(stderr, "load t2_env failed\n");
		pm_metal_env_free_exported(exported_envp, exported_envc);
		goto done;
	}

	char *local_argv[2] = { "t2_env", "LOCAL_ONLY" };
	char *exported_argv[2] = { "t2_env", "EXPORTED_VAR" };
	pm_metal_process_id_t local_pid, exported_pid;
	int local_exit, exported_exit;

	if (pm_metal_process_spawn(h_env, 2, local_argv, exported_envc, (const char **)exported_envp, -1, -1, -1, NULL,
				    NULL, &local_pid)
		    != 0
	    || pm_metal_process_wait(local_pid, &local_exit) != 0) {
		fprintf(stderr, "spawn/wait t2_env LOCAL_ONLY failed\n");
		pm_metal_env_free_exported(exported_envp, exported_envc);
		pm_metal_runtime_unload(h_env);
		goto done;
	}
	if (pm_metal_process_spawn(h_env, 2, exported_argv, exported_envc, (const char **)exported_envp, -1, -1, -1,
				    NULL, NULL, &exported_pid)
		    != 0
	    || pm_metal_process_wait(exported_pid, &exported_exit) != 0) {
		fprintf(stderr, "spawn/wait t2_env EXPORTED_VAR failed\n");
		pm_metal_env_free_exported(exported_envp, exported_envc);
		pm_metal_runtime_unload(h_env);
		goto done;
	}

	pm_metal_env_free_exported(exported_envp, exported_envc);
	pm_metal_runtime_unload(h_env);

	if (local_exit != 0 || exported_exit != 0) {
		fprintf(stderr, "t2_env did not exit 0 (local=%d exported=%d)\n", local_exit, exported_exit);
		goto done;
	}

	/* --- sockets: WASI preview1's own extension (docs/RUNTIME.md
	 * "Sockets"), gated on the "127.0.0.1/32" addr_pool this whole
	 * process was init()ed with above. Both processes' own stdout is
	 * left on -1 (inherit this process's own, same as every earlier
	 * test here) — their real proof is each one's own exit code plus
	 * scripts/verify-linux-process.sh's own grep over the combined
	 * output for t11_socket_client's "got: echo: hello socket" line. --- */
	pm_metal_runtime_handle_t h_sock_server, h_sock_client;
	if (pm_metal_runtime_load_file("/mods/t10_socket_server.wasm", &h_sock_server) != 0) {
		fprintf(stderr, "load t10_socket_server failed\n");
		goto done;
	}
	if (pm_metal_runtime_load_file("/mods/t11_socket_client.wasm", &h_sock_client) != 0) {
		fprintf(stderr, "load t11_socket_client failed\n");
		pm_metal_runtime_unload(h_sock_server);
		goto done;
	}

	char *sock_server_argv[1] = { "t10_socket_server" };
	char *sock_client_argv[1] = { "t11_socket_client" };
	pm_metal_process_id_t sock_server_pid, sock_client_pid;

	if (pm_metal_process_spawn(h_sock_server, 1, sock_server_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &sock_server_pid)
	    != 0) {
		fprintf(stderr, "spawn t10_socket_server failed\n");
		pm_metal_runtime_unload(h_sock_client);
		pm_metal_runtime_unload(h_sock_server);
		goto done;
	}
	/* t11_socket_client's own bounded connect() retry loop (see that
	 * mod's own main.c) is what actually copes with the real race
	 * against t10_socket_server's own listen() — spawned immediately,
	 * not after some fixed delay here. */
	if (pm_metal_process_spawn(h_sock_client, 1, sock_client_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &sock_client_pid)
	    != 0) {
		int sock_server_exit = -1;

		fprintf(stderr, "spawn t11_socket_client failed\n");
		(void)pm_metal_process_kill(sock_server_pid);
		(void)pm_metal_process_wait(sock_server_pid, &sock_server_exit);
		pm_metal_runtime_unload(h_sock_client);
		pm_metal_runtime_unload(h_sock_server);
		goto done;
	}

	int sock_server_exit, sock_client_exit;
	int wait_sock_server = pm_metal_process_wait(sock_server_pid, &sock_server_exit);
	int wait_sock_client = pm_metal_process_wait(sock_client_pid, &sock_client_exit);

	if (wait_sock_server != 0 || wait_sock_client != 0) {
		fprintf(stderr, "wait() on socket pair failed\n");
		pm_metal_runtime_unload(h_sock_client);
		pm_metal_runtime_unload(h_sock_server);
		goto done;
	}

	pm_metal_runtime_unload(h_sock_client);
	pm_metal_runtime_unload(h_sock_server);

	printf("process_test: socket pair exit codes server=%d client=%d\n", sock_server_exit, sock_client_exit);
	if (sock_server_exit != 0 || sock_client_exit != 0) {
		fprintf(stderr, "socket pair did not both exit 0\n");
		goto done;
	}

	/* --- UDP echo: t24_udp_server + t25_udp_client on 127.0.0.1:9933 --- */
	pm_metal_runtime_handle_t h_udp_server, h_udp_client;
	if (pm_metal_runtime_load_file("/mods/t24_udp_server.wasm", &h_udp_server) != 0) {
		fprintf(stderr, "load t24_udp_server failed\n");
		goto done;
	}
	if (pm_metal_runtime_load_file("/mods/t25_udp_client.wasm", &h_udp_client) != 0) {
		fprintf(stderr, "load t25_udp_client failed\n");
		pm_metal_runtime_unload(h_udp_server);
		goto done;
	}

	char *udp_server_argv[1] = { "t24_udp_server" };
	char *udp_client_argv[1] = { "t25_udp_client" };
	pm_metal_process_id_t udp_server_pid, udp_client_pid;

	if (pm_metal_process_spawn(h_udp_server, 1, udp_server_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &udp_server_pid)
	    != 0) {
		fprintf(stderr, "spawn t24_udp_server failed\n");
		pm_metal_runtime_unload(h_udp_client);
		pm_metal_runtime_unload(h_udp_server);
		goto done;
	}
	if (pm_metal_process_spawn(h_udp_client, 1, udp_client_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &udp_client_pid)
	    != 0) {
		int udp_server_exit = -1;

		fprintf(stderr, "spawn t25_udp_client failed\n");
		(void)pm_metal_process_kill(udp_server_pid);
		(void)pm_metal_process_wait(udp_server_pid, &udp_server_exit);
		pm_metal_runtime_unload(h_udp_client);
		pm_metal_runtime_unload(h_udp_server);
		goto done;
	}

	int udp_server_exit, udp_client_exit;
	int wait_udp_server = pm_metal_process_wait(udp_server_pid, &udp_server_exit);
	int wait_udp_client = pm_metal_process_wait(udp_client_pid, &udp_client_exit);

	if (wait_udp_server != 0 || wait_udp_client != 0) {
		fprintf(stderr, "wait() on udp pair failed\n");
		pm_metal_runtime_unload(h_udp_client);
		pm_metal_runtime_unload(h_udp_server);
		goto done;
	}
	pm_metal_runtime_unload(h_udp_client);
	pm_metal_runtime_unload(h_udp_server);
	printf("process_test: udp pair exit codes server=%d client=%d\n", udp_server_exit,
	       udp_client_exit);
	if (udp_server_exit != 0 || udp_client_exit != 0) {
		fprintf(stderr, "udp pair did not both exit 0\n");
		goto done;
	}

	/* --- IPv6 TCP echo: t26_ipv6_server + t27_ipv6_client on ::1:9932 --- */
	pm_metal_runtime_handle_t h_v6_server, h_v6_client;
	if (pm_metal_runtime_load_file("/mods/t26_ipv6_server.wasm", &h_v6_server) != 0) {
		fprintf(stderr, "load t26_ipv6_server failed\n");
		goto done;
	}
	if (pm_metal_runtime_load_file("/mods/t27_ipv6_client.wasm", &h_v6_client) != 0) {
		fprintf(stderr, "load t27_ipv6_client failed\n");
		pm_metal_runtime_unload(h_v6_server);
		goto done;
	}

	char *v6_server_argv[1] = { "t26_ipv6_server" };
	char *v6_client_argv[1] = { "t27_ipv6_client" };
	pm_metal_process_id_t v6_server_pid, v6_client_pid;

	if (pm_metal_process_spawn(h_v6_server, 1, v6_server_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &v6_server_pid)
	    != 0) {
		fprintf(stderr, "spawn t26_ipv6_server failed\n");
		pm_metal_runtime_unload(h_v6_client);
		pm_metal_runtime_unload(h_v6_server);
		goto done;
	}
	if (pm_metal_process_spawn(h_v6_client, 1, v6_client_argv, 0, NULL, -1, -1, -1, NULL, NULL,
				    &v6_client_pid)
	    != 0) {
		int v6_server_exit = -1;

		fprintf(stderr, "spawn t27_ipv6_client failed\n");
		(void)pm_metal_process_kill(v6_server_pid);
		(void)pm_metal_process_wait(v6_server_pid, &v6_server_exit);
		pm_metal_runtime_unload(h_v6_client);
		pm_metal_runtime_unload(h_v6_server);
		goto done;
	}

	int v6_server_exit, v6_client_exit;
	int wait_v6_server = pm_metal_process_wait(v6_server_pid, &v6_server_exit);
	int wait_v6_client = pm_metal_process_wait(v6_client_pid, &v6_client_exit);

	if (wait_v6_server != 0 || wait_v6_client != 0) {
		fprintf(stderr, "wait() on ipv6 pair failed\n");
		pm_metal_runtime_unload(h_v6_client);
		pm_metal_runtime_unload(h_v6_server);
		goto done;
	}
	pm_metal_runtime_unload(h_v6_client);
	pm_metal_runtime_unload(h_v6_server);
	printf("process_test: ipv6 pair exit codes server=%d client=%d\n", v6_server_exit,
	       v6_client_exit);
	if (v6_server_exit != 0 || v6_client_exit != 0) {
		fprintf(stderr, "ipv6 pair did not both exit 0\n");
		goto done;
	}

	/* --- DNS: t28_dns_lookup getaddrinfo("localhost") --- */
	pm_metal_runtime_handle_t h_dns;
	if (pm_metal_runtime_load_file("/mods/t28_dns_lookup.wasm", &h_dns) != 0) {
		fprintf(stderr, "load t28_dns_lookup failed\n");
		goto done;
	}
	char *dns_argv[1] = { "t28_dns_lookup" };
	pm_metal_process_id_t dns_pid;
	if (pm_metal_process_spawn(h_dns, 1, dns_argv, 0, NULL, -1, -1, -1, NULL, NULL, &dns_pid)
	    != 0) {
		fprintf(stderr, "spawn t28_dns_lookup failed\n");
		pm_metal_runtime_unload(h_dns);
		goto done;
	}
	int dns_exit;
	if (pm_metal_process_wait(dns_pid, &dns_exit) != 0 || dns_exit != 0) {
		fprintf(stderr, "t28_dns_lookup did not exit 0 (exit=%d)\n", dns_exit);
		pm_metal_runtime_unload(h_dns);
		goto done;
	}
	pm_metal_runtime_unload(h_dns);
	printf("process_test: dns lookup exit=%d\n", dns_exit);

	rc = 0;
	printf("process_test: OK\n");

done:
	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return rc;
}
