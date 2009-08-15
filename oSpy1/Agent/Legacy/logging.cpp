/**
 * Copyright (C) 2006  Ole Andr� Vadla Ravn�s <oleavr@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "stdafx.h"
#include "logging.h"
#include "util.h"
#include <stdio.h>
#include <strsafe.h>

#pragma managed(push, off)

#pragma warning( disable : 4996 )

static MessageLoggerSubmitFunc message_logger_submit = NULL;

void
message_logger_init(MessageLoggerSubmitFunc submit_func)
{
    message_logger_submit = submit_func;
}

static void
message_element_init(MessageQueueElement *el,
                     const char *function_name,
                     void *bt_address,
                     DWORD resource_id,
                     MessageType msg_type)
{
    /* timestamp */
    GetLocalTime(&el->time);

    /* process name, id and thread id */
    get_process_name(el->process_name, sizeof(el->process_name));
    el->process_id = GetCurrentProcessId();      
    el->thread_id = GetCurrentThreadId();

	std::string s;

    /* function name and return address */
    strcpy_s(el->function_name, sizeof(el->function_name), function_name);
	if (bt_address != NULL)
	{
		OString backtrace = CUtil::CreateBackTrace(bt_address);
		strncpy(el->backtrace, backtrace.c_str(), sizeof(el->backtrace));
		el->backtrace[sizeof(el->backtrace) - 1] = '\0';
	}

    /* underlying resource id */
    if (resource_id == 0)
    {
        resource_id = ospy_rand();
    }

    el->resource_id = resource_id;

    /* message type */
    el->type = msg_type;
}

// FIXME: this should be dynamically allocated instead
#define LOG_BUFFER_SIZE 2048

void
message_logger_log_message(const char *function_name,
                           void *bt_address,
                           MessageContext context,
                           const char *message,
                           ...)
{
    va_list args;
    char buf[LOG_BUFFER_SIZE];

    va_start(args, message);
    StringCbVPrintfA(buf, sizeof(buf), message, args);

	message_logger_log_full(function_name, bt_address, 0,
		MESSAGE_TYPE_MESSAGE, context, PACKET_DIRECTION_INVALID,
		NULL, NULL, NULL, 0, buf, 0, 0);
}

void
message_logger_log_packet(const char *function_name,
                          void *bt_address,
                          DWORD resource_id,
                          PacketDirection direction,
                          const sockaddr_in *local_addr,
                          const sockaddr_in *peer_addr,
                          const char *buf,
                          int len)
{
    message_logger_log(function_name, bt_address, resource_id,
                       MESSAGE_TYPE_PACKET, MESSAGE_CTX_INFO, direction,
                       local_addr, peer_addr, buf, len, NULL);
}

void
message_logger_log_full(const char *function_name,
						void *bt_address,
						DWORD resource_id,
						MessageType msg_type,
						MessageContext context,
						PacketDirection direction,
						const sockaddr_in *local_addr,
						const sockaddr_in *peer_addr,
						const char *buf,
						int len,
						const char *message,
					    DWORD domain,
					    DWORD severity)
{
    MessageQueueElement el;
    int read_len;

    memset(&el, 0, sizeof(MessageQueueElement));

    /* fill in basic fields */
    message_element_init(&el, function_name, bt_address, resource_id, msg_type);

	el.domain = domain;
	el.severity = severity;

    /* context */
    el.context = context;

    /* direction */
    el.direction = direction;

    /* fill in local address and port */
    if (local_addr)
    {
        strcpy_s(el.local_address, sizeof(el.local_address), inet_ntoa(local_addr->sin_addr));
        el.local_port = ntohs(local_addr->sin_port);
    }

    /* fill in peer address and port */
    if (peer_addr)
    {
        strcpy_s(el.peer_address, sizeof(el.peer_address), inet_ntoa(peer_addr->sin_addr));
        el.peer_port = ntohs(peer_addr->sin_port);
    }

    /* copy the buffer */
    if (len > PACKET_BUFSIZE)
    {
        read_len = PACKET_BUFSIZE;
        message_logger_log_message("message_logger_log_packet", 0,
          MESSAGE_CTX_WARNING, "packet read clamped to %d bytes, needed %d",
          read_len, len);
    }
    else
    {
        read_len = len;
    }

    if (buf != NULL)
    {
        memcpy(el.buf, buf, read_len);
        el.len = read_len;
    }

	if (message != NULL)
	{
		strncpy(el.message, message, sizeof(el.message));
		el.message[sizeof(el.message) - 1] = '\0';
	}

    /* submit the message */
    message_logger_submit(&el);
}

void
message_logger_log(const char *function_name,
                   void *bt_address,
                   DWORD resource_id,
                   MessageType msg_type,
                   MessageContext context,
                   PacketDirection direction,
                   const sockaddr_in *local_addr,
                   const sockaddr_in *peer_addr,
                   const char *buf,
                   int len,
                   const char *message,
				   ...)
{
    va_list args;
    char msg_buf[LOG_BUFFER_SIZE];

	if (message != NULL)
	{
		va_start(args, message);
		StringCbVPrintfA(msg_buf, sizeof(msg_buf), message, args);

		message = msg_buf;
	}

	message_logger_log_full(function_name, bt_address, resource_id,
		msg_type, context, direction, local_addr, peer_addr, buf, len,
		message, 0, 0);
}


/****************************************************************************
 * Logging utility functions                                                *
 ****************************************************************************/

void
log_tcp_listening(const char *function_name,
                  void *bt_address,
                  SOCKET server_socket)
{
	struct sockaddr_in sin;
	int sin_len;

    /* local address */
    sin_len = sizeof(sin);
    getsockname(server_socket, (struct sockaddr *) &sin, &sin_len);

    message_logger_log(function_name, bt_address, server_socket,
                       MESSAGE_TYPE_MESSAGE, MESSAGE_CTX_SOCKET_LISTENING,
                       PACKET_DIRECTION_INCOMING, &sin, NULL, NULL, 0,
                       "%s:%d: listening for connections",
                       inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
}

void
log_tcp_connecting(const char *function_name,
                   void *bt_address,
                   SOCKET socket,
                   const struct sockaddr *name)
{
	struct sockaddr_in sin;
	int sin_len;
    const sockaddr_in *peer_addr = (const sockaddr_in *) name;
    char local_addr_str[16], peer_addr_str[16];

    /* local address */
    sin_len = sizeof(sin);
    getsockname(socket, (struct sockaddr *) &sin, &sin_len);

    strcpy_s(local_addr_str, sizeof(local_addr_str), inet_ntoa(sin.sin_addr));
    strcpy_s(peer_addr_str, sizeof(peer_addr_str), inet_ntoa(peer_addr->sin_addr));

    message_logger_log(function_name, bt_address, socket,
                       MESSAGE_TYPE_MESSAGE, MESSAGE_CTX_SOCKET_CONNECTING,
                       PACKET_DIRECTION_OUTGOING, &sin, peer_addr, NULL, 0,
                       "%s:%d: connecting to %s:%d",
                       local_addr_str, ntohs(sin.sin_port),
                       peer_addr_str, ntohs(peer_addr->sin_port));
}

void
log_tcp_connected(const char *function_name,
                  void *bt_address,
                  SOCKET socket,
                  const struct sockaddr *name)
{
	struct sockaddr_in sin;
	int sin_len;
    const sockaddr_in *peer_addr = (const sockaddr_in *) name;
    char local_addr_str[16], peer_addr_str[16];

    /* local address */
    sin_len = sizeof(sin);
    getsockname(socket, (struct sockaddr *) &sin, &sin_len);

    strcpy_s(local_addr_str, sizeof(local_addr_str), inet_ntoa(sin.sin_addr));
    strcpy_s(peer_addr_str, sizeof(peer_addr_str), inet_ntoa(peer_addr->sin_addr));

    message_logger_log(function_name, bt_address, socket,
                       MESSAGE_TYPE_MESSAGE, MESSAGE_CTX_SOCKET_CONNECTED,
                       PACKET_DIRECTION_OUTGOING, &sin, peer_addr, NULL, 0,
                       "%s:%d: connected to %s:%d",
                       local_addr_str, ntohs(sin.sin_port),
                       peer_addr_str, ntohs(peer_addr->sin_port));
}

void
log_tcp_client_connected(const char *function_name,
                         void *bt_address,
                         SOCKET server_socket,
                         SOCKET client_socket)
{
    struct sockaddr_in sin_local, sin_peer;
    int sin_len;
    char local_addr_str[16], peer_addr_str[16];

    /* local address
     *
     * FIXME: maybe client_socket should be used instead?
     */
    sin_len = sizeof(sin_local);
    getsockname(server_socket, (struct sockaddr *) &sin_local, &sin_len);
    strcpy_s(local_addr_str, sizeof(local_addr_str), inet_ntoa(sin_local.sin_addr));

    /* peer address */
    sin_len = sizeof(sin_peer);
    getpeername(client_socket, (struct sockaddr *) &sin_peer, &sin_len);
    strcpy_s(peer_addr_str, sizeof(peer_addr_str), inet_ntoa(sin_peer.sin_addr));

    message_logger_log(function_name, bt_address, client_socket,
                       MESSAGE_TYPE_MESSAGE, MESSAGE_CTX_SOCKET_CONNECTED,
                       PACKET_DIRECTION_INCOMING, &sin_local, &sin_peer, NULL,
                       0, "%s:%d: client connected from %s:%d",
                       local_addr_str, ntohs(sin_local.sin_port),
                       peer_addr_str, ntohs(sin_peer.sin_port));
}

void
log_tcp_disconnected(const char *function_name,
                     void *bt_address,
                     SOCKET s,
                     DWORD *last_error)
{
    struct sockaddr_in sin_local, sin_peer;
    int sin_len;
    char local_addr_str[16], peer_addr_str[16];

    /* local address */
    sin_len = sizeof(sin_local);
    getsockname(s, (struct sockaddr *) &sin_local, &sin_len);
    strcpy_s(local_addr_str, sizeof(local_addr_str), inet_ntoa(sin_local.sin_addr));

    /* peer address */
    sin_len = sizeof(sin_peer);
    getpeername(s, (struct sockaddr *) &sin_peer, &sin_len);
    strcpy_s(peer_addr_str, sizeof(peer_addr_str), inet_ntoa(sin_peer.sin_addr));

    message_logger_log(function_name, bt_address, s, MESSAGE_TYPE_MESSAGE,
                       (last_error == NULL) ? MESSAGE_CTX_SOCKET_DISCONNECTED : MESSAGE_CTX_SOCKET_RESET,
                       PACKET_DIRECTION_INVALID, &sin_local, &sin_peer, (const char *) last_error,
                       (last_error == NULL) ? 0 : 4, "%s:%d: connection to %s:%d %s",
                       local_addr_str, ntohs(sin_local.sin_port),
                       peer_addr_str, ntohs(sin_peer.sin_port),
                       (last_error == NULL) ? "closed" : "reset");
}

void
log_tcp_packet(const char *function_name,
               void *bt_address,
               PacketDirection direction,
               SOCKET s, const char *buf,
               int len)
{
    struct sockaddr_in local_addr, peer_addr;
    int sin_len;

    sin_len = sizeof(local_addr);
    getsockname(s, (struct sockaddr *) &local_addr, &sin_len);

    sin_len = sizeof(peer_addr);
    getpeername(s, (struct sockaddr *) &peer_addr, &sin_len);

    message_logger_log_packet(function_name, bt_address, s, direction,
                              &local_addr, &peer_addr, buf, len);
}

void
log_udp_packet(const char *function_name,
               void *bt_address,
               PacketDirection direction,
               SOCKET s,
               const struct sockaddr *peer,
               const char *buf,
               int len)
{
	struct sockaddr_in local_addr, peer_addr;
	int sin_len;

    sin_len = sizeof(local_addr);
    getsockname(s, (struct sockaddr *) &local_addr, &sin_len);

    if (peer == NULL)
    {
        sin_len = sizeof(peer_addr);
        getsockname(s, (struct sockaddr *) &peer_addr, &sin_len);    
    }
    else
    {
        peer_addr = *((const struct sockaddr_in *) peer);
    }

    message_logger_log_packet(function_name, bt_address, s, direction,
                              &local_addr, &peer_addr, buf, len);
}

void
log_debug_w(const char *source,
            void *bt_address,
            const LPWSTR format,
            va_list args,
			DWORD domain,
			DWORD severity)
{
    WCHAR wide_buf[LOG_BUFFER_SIZE];
    char buf[LOG_BUFFER_SIZE];

    StringCbVPrintfW(wide_buf, sizeof(wide_buf), format, args);

    WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, buf, sizeof(buf), NULL, NULL);

    message_logger_log_full(source, bt_address, 0, MESSAGE_TYPE_MESSAGE,
        MESSAGE_CTX_INFO, PACKET_DIRECTION_INVALID, NULL, NULL,
        buf, strlen(buf), buf, domain, severity);
}

void
log_debug(const char *source,
          void *bt_address,
          const char *format,
          va_list args,
		  DWORD domain,
		  DWORD severity)
{
    char buf[LOG_BUFFER_SIZE];

    StringCbVPrintfA(buf, sizeof(buf), format, args);

	message_logger_log_full(source, bt_address, 0, MESSAGE_TYPE_MESSAGE,
		MESSAGE_CTX_INFO, PACKET_DIRECTION_INVALID, NULL, NULL, NULL, 0, buf,
		domain, severity);
}

#pragma managed(pop)
