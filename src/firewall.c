/*******************************************************************************
 * Copyright (C) 2020 - 2021, Mohith Reddy <dev.m0hithreddy@gmail.com>
 *
 * This file is part of Proxifier-For-Linux <https://github.com/m0hithreddy/Proxifier-For-Linux>
 *
 * Proxifier is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Proxifier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/

#include "firewall.h"
#include "proxy.h"
#include "proxy_functions.h"
#include "proxy_socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>

static pthread_mutex_t fwall_lock = PTHREAD_MUTEX_INITIALIZER;

#define execute_rule(file, ...)\
	do{\
		pthread_mutex_lock(&fwall_lock);\
		sigset_t sigmask = *(px_handler->px_opt->sigmask);\
		sigaddset(&sigmask, SIGCHLD);\
		pid_t fk_return = fork();\
		if (fk_return < 0) {\
			pthread_mutex_unlock(&fwall_lock);\
			return PROXY_ERROR_FATAL;\
		}\
		else if (fk_return == 0) {\
			execlp(file, ##__VA_ARGS__);\
		}\
		int signo;\
		if (sigwait(&sigmask, &signo) != 0) {\
			pthread_mutex_unlock(&fwall_lock);\
			return PROXY_ERROR_FATAL;\
		}\
		if (signo != SIGCHLD) {\
			pthread_mutex_unlock(&fwall_lock);\
			return PROXY_ERROR_SIGRCVD;\
		}\
		int wstatus;\
		if (waitpid(fk_return, &wstatus, 0) != fk_return) {\
			pthread_mutex_unlock(&fwall_lock);\
			return PROXY_ERROR_FATAL;\
		}\
		if (WIFEXITED(wstatus) != true || WEXITSTATUS(wstatus) != 0) {\
			pthread_mutex_unlock(&fwall_lock);\
			return PROXY_ERROR_FATAL;\
		}\
		pthread_mutex_unlock(&fwall_lock);\
	}while(0)

int config_fwall(struct proxy_handler* px_handler)
{
	if (px_handler == NULL || px_handler->px_opt == NULL || px_handler->pxl_server == NULL) {
		return PROXY_ERROR_INVAL;
	}

	if (px_handler->px_opt->px_server != NULL) {
		/* Bypass Proxy Server Traffic */

		if (px_handler->pxl_server->type == SOCK_STREAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
		else if (px_handler->pxl_server->type == SOCK_DGRAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_UDP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
		else {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_ALL_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
		
	}
	
	/* Exclusions - enter IP addresses, that must be connected directly, not from proxy*/
	execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					"18.153.178.179", FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);
	execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					"158.160.55.64", FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);
	execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_TOP, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					"212.41.27.34", FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

	if (px_handler->px_opt->rd_ports != NULL && px_handler->px_opt->nrd_ports > 0 && \
			px_handler->pxl_server->hostip != NULL && px_handler->pxl_server->port != NULL) {
		/* Redirects traffic coming from specific ports to server */

		char* ports_string = calloc(1, 1);
		char* tmp_str;

		for (long port_count = 0; port_count < px_handler->px_opt->nrd_ports; \
		port_count++) {
			tmp_str = ports_string;

			if (port_count == px_handler->px_opt->nrd_ports - 1) {
				ports_string = strappend(2, ports_string, \
						px_handler->px_opt->rd_ports[port_count]);
			}
			else {
				ports_string = strappend(3, ports_string, \
						px_handler->px_opt->rd_ports[port_count], ",");
			}

			free(tmp_str);
		}
		if (px_handler->pxl_server->type == SOCK_STREAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_BOTTOM, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);
		}
		else if (px_handler->pxl_server->type == SOCK_DGRAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_BOTTOM, FIREWALL_CONSTANT_UDP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);

		}
		else {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_INSERT_AT_BOTTOM, FIREWALL_CONSTANT_ALL_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);
		}

		free(ports_string);
	}

	return PROXY_ERROR_NONE;
}

int deconfig_fwall(struct proxy_handler* px_handler)
{

	if (px_handler == NULL || px_handler->px_opt == NULL || px_handler->pxl_server == NULL) {
		return PROXY_ERROR_INVAL;
	}

	if (px_handler->px_opt->px_server != NULL) {
		/* Bypass Proxy Server Traffic */

		if (px_handler->pxl_server->type == SOCK_STREAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
		else if (px_handler->pxl_server->type == SOCK_DGRAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_UDP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
		else {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_ALL_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_ADRESS_OPTION, \
					px_handler->px_opt->px_server, FIREWALL_CONSTANT_TARGET_ACCEPT, NULL);

		}
	}

	if (px_handler->px_opt->rd_ports != NULL && px_handler->px_opt->nrd_ports > 0 && \
			px_handler->pxl_server->hostip != NULL && px_handler->pxl_server->port != NULL) {
		/* Remove Redirects traffic coming from specific ports to server Rule */

		char* ports_string = calloc(1, 1);
		char* tmp_str;

		for (long port_count = 0; port_count < px_handler->px_opt->nrd_ports; \
		port_count++) {
			tmp_str = ports_string;

			if (port_count == px_handler->px_opt->nrd_ports - 1) {
				ports_string = strappend(2, ports_string, \
						px_handler->px_opt->rd_ports[port_count]);
			}
			else {
				ports_string = strappend(3, ports_string, \
						px_handler->px_opt->rd_ports[port_count], ",");
			}

			free(tmp_str);
		}

		if (px_handler->pxl_server->type == SOCK_STREAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_TCP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);
		}
		else if (px_handler->pxl_server->type == SOCK_DGRAM) {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_UDP_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);

		}
		else {
			execute_rule(FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_PROGRAM, FIREWALL_CONSTANT_NAT_TABLE, \
					FIREWALL_CONSTANT_DELETE, FIREWALL_CONSTANT_ALL_PROTOCOL, FIREWALL_CONSTANT_DESTINATION_PORTS_OPTION, \
					ports_string, FIREWALL_CONSTANT_TARGET_DNAT, FIREWALL_CONSTANT_REDIRECTION_ADDRESS_OPTION, \
					strappend(3, px_handler->pxl_server->hostip, ":", px_handler->pxl_server->port), NULL);
		}

		free(ports_string);
	}

	return PROXY_ERROR_NONE;
}
