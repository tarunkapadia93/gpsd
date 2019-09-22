#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""aiogps.py -- Asyncio Python interface to GPSD.

This module adds asyncio functionality to the Python gps interface. It can also
manage connections over unreliable networks through timeouts, keepalive and
automatic reconnection.

Examples:
    import logging
    import gps

    # configuring logging
    logging.basicConfig()
    logging.root.setLevel(logging.INFO)
    # Example of setting up logging level for the aiogps logger
    logging.getLogger('gps.aiogps').setLevel(logging.ERROR)

    # using default parameters
    async with gps.aiogps() as gpsd:
        async for msg in gpsd:
            # Log last message
            logging.info(f'Received: {msg}')
            # Log updated GPS status
            logging.info(f'\nGPS status:\n{gpsd}')

    # using custom parameters
    try:
        async with gps.aiogps(
                connection_args = {
                    'host': '192.168.10.116',
                    'port': 2947
                },
                connection_timeout = 5,
                reconnect = 0,   # do not try to reconnect, raise exceptions
                alive_opts = {
                    'rx_timeout': 5
                }
            ) as gpsd:
            async for msg in gpsd:
                logging.info(msg)
    except asyncio.CancelledError:
        return
    except asyncio.IncompleteReadError:
        logging.info('Connection closed by server')
    except asyncio.TimeoutError:
        logging.error('Timeout waiting for gpsd to respond')
    except Exception as exc:
        logging.error(f'Error: {exc}')

"""

# Copyright (c) 2019 Grand Joldes (grandwork2@yahoo.com)
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# This file is Copyright (c) 2019 by the GPSD project
# BSD terms apply: see the file COPYING in the distribution root for details.

# This code run compatibly under  Python 3.x for x >= 6.

__all__ = ['aiogps',]

import logging
import asyncio
import socket
from typing import Optional, Union, Awaitable

from .client import gpsjson, dictwrapper
from .gps import gps, gpsdata, WATCH_ENABLE, PACKET_SET
from .misc import polystr, polybytes


class aiogps(gps):  # pylint: disable=R0902
    """An asyncio gps client.

    Reimplements all gps IO methods using asyncio coros.
    Adds connection management, an asyncio context manager and an asyncio iterator.

    The class uses a logger named 'gps.aiogps' to record events. The logger is
    configured with a NullHandler to disable any message logging until the application
    configures another handler.
    """

    def __init__(self, connection_args: Optional[dict] = None,  # pylint: disable=W0231
                 connection_timeout: Optional[float] = None,
                 reconnect: Optional[float] = 2,
                 alive_opts: Optional[dict] = None) -> None:
        """
        Arguments:
            connection_args: dictionary with arguments needed for opening a connection.
                These will be passed directly to asyncio.open_connection. If set to None,
                a connection to the default gps host and port will be attempded.
            connection_timeout: time to wait for a connection to complete (seconds).
                Set to None to disable.
            reconnect: configures automatic reconnections:
                - 0: reconnection is not attempted in case of an error and the error
                is raised to the user;
                - number > 0: delay until next reconnection attempt (seconds).
            alive_opts: dictionary with options related to detection of disconnections.
                Two mecanisms are supported: TCP keepalive (default, may not be available
                on all platforms) and Rx timeout, through the following options:
                - rx_timeout: Rx timeout (seconds). Set to None to disable.
                - SO_KEEPALIVE: socket keepalive and related parameters:
                - TCP_KEEPIDLE
                - TCP_KEEPINTVL
                - TCP_KEEPCNT
        """
        # If connection_args are not specified use defaults
        self.connection_args = connection_args or {
                'host': self.host,
                'port': self.port
            }
        self.connection_timeout = connection_timeout
        assert reconnect >= 0
        self.reconnect = reconnect
        # If alive_opts are not specified use defaults
        self.alive_opts = alive_opts or {
                'rx_timeout': None,
                'SO_KEEPALIVE': 1,
                'TCP_KEEPIDLE': 2,
                'TCP_KEEPINTVL': 2,
                'TCP_KEEPCNT': 3
            }
        # Connection access streams
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        # Set up logging
        self.logger = logging.getLogger(__name__)
        # Set the Null handler - prevents logging message handling unless the application
        # sets up a handler.
        self.logger.addHandler(logging.NullHandler())
        # Init gps parents
        gpsdata.__init__(self) # pylint: disable=W0233
        gpsjson.__init__(self) # pylint: disable=W0233
        # Provide the response in both 'str' and 'bytes' form
        self.bresponse = b''
        self.response = polystr(self.bresponse)
        # Default stream command
        self.stream_command = self.generate_stream_command(WATCH_ENABLE)
        self.loop = self.connection_args.get('loop', asyncio.get_event_loop())

    def __del__(self) -> None:
        """ Destructor """
        self.close()

    async def _open_connection(self) -> None:
        """ Opens a connection to the GPSD server and configures the TCP socket """
        self.logger.info(f"Connecting to gpsd at {self.connection_args['host']}" +
                     (f":{self.connection_args['port']}" if self.connection_args['port'] else ''))
        self.reader, self.writer = await asyncio.wait_for(
            asyncio.open_connection(**self.connection_args),
            self.connection_timeout,
            loop=self.loop)
        # Set socket options
        sock = self.writer.get_extra_info('socket')
        if sock is not None:
            if 'SO_KEEPALIVE' in self.alive_opts:
                sock.setsockopt(socket.SOL_SOCKET,
                                socket.SO_KEEPALIVE,
                                self.alive_opts['SO_KEEPALIVE'])
            if hasattr(sock, 'TCP_KEEPIDLE') and 'TCP_KEEPIDLE' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPIDLE,    # pylint: disable=E1101
                                self.alive_opts['TCP_KEEPIDLE'])
            if hasattr(sock, 'TCP_KEEPINTVL') and 'TCP_KEEPINTVL' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPINTVL,   # pylint: disable=E1101
                                self.alive_opts['TCP_KEEPINTVL'])
            if hasattr(sock, 'TCP_KEEPCNT') and 'TCP_KEEPCNT' in self.alive_opts:
                sock.setsockopt(socket.IPPROTO_TCP,
                                socket.TCP_KEEPCNT,
                                self.alive_opts['TCP_KEEPCNT'])

    def close(self) -> None:
        """ Closes connection to GPSD server """
        if self.writer:
            try:
                self.writer.close()
            except Exception:   # pylint: disable=W0703
                pass
            self.writer = None

    def waiting(self) -> bool:   # pylint: disable=W0221
        """ Mask the blocking waiting method from gpscommon """
        return True

    async def read(self) -> Union[dictwrapper, str]:
        """ Reads data from GPSD server """
        while True:
            await self.connect()
            try:
                rx_timeout = self.alive_opts.get('rx_timeout', None)
                reader = self.reader.readuntil(separator=b'\n')
                self.bresponse = await asyncio.wait_for(reader,
                                                        rx_timeout,
                                                        loop=self.loop)
                self.response = polystr(self.bresponse)
                if self.response.startswith("{") and self.response.endswith("}\r\n"):
                    self.unpack(self.response)
                    self._oldstyle_shim()
                    self.valid |= PACKET_SET
                    return self.data
                return self.response
            except asyncio.CancelledError:
                self.close()
                raise
            except Exception as exc:    # pylint: disable=W0703
                error = 'timeout' if isinstance(exc, asyncio.TimeoutError) else exc
                self.logger.warning(f'Failed to get message from GPSD: {error}')
                self.close()
                if self.reconnect:
                    # Try again later
                    await asyncio.sleep(self.reconnect)
                else:
                    raise

    async def connect(self) -> None:    # pylint: disable=W0221
        """ Connects to GPSD server and starts streaming data """
        while not self.writer:
            try:
                await self._open_connection()
                await self.stream()
                self.logger.info('Connected to gpsd')
            except asyncio.CancelledError:
                self.close()
                raise
            except Exception as exc:    # pylint: disable=W0703
                error = 'timeout' if isinstance(exc, asyncio.TimeoutError) else exc
                self.logger.error(f'Failed to connect to GPSD: {error}')
                self.close()
                if self.reconnect:
                    # Try again later
                    await asyncio.sleep(self.reconnect)
                else:
                    raise

    async def send(self, commands) -> None:
        """ Sends commands """
        bcommands = polybytes(commands + "\n")
        if self.writer:
            self.writer.write(bcommands)
            await self.writer.drain()

    async def stream(self, flags: Optional[int] = 0, devpath: Optional[str] = None) -> None:
        """ Creates and sends the stream command """
        if flags > 0:
            # Update the stream command
            self.stream_command = self.generate_stream_command(flags, devpath)

        if self.stream_command:
            self.logger.info(f'Sent stream as: {self.stream_command}')
            await self.send(self.stream_command)
        else:
            raise TypeError(f'Invalid streaming command: {flags}')

    async def __aenter__(self) -> 'aiogps':
        """ Context manager entry """
        return self

    async def __aexit__(self, exc_type, exc, traceback) -> None:
        """ Context manager exit: close connection """
        self.close()

    def __aiter__(self) -> 'aiogps':
        """ Async iterator interface """
        return self

    async def __anext__(self) -> Union[dictwrapper, str]:
        """ Returns next message from GPSD """
        data = await self.read()
        return data

    def __next__(self) -> Awaitable:
        """
        Reimplementation of the blocking iterator from gps.
        Returns an awaitable which returns the next message from GPSD.
        """
        return self.read()
